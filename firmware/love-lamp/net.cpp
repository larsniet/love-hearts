#include "net.h"

#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <esp_random.h>
#include <esp_system.h>
#include <time.h>

#include "button.h"
#include "config.h"
#include "defaults.h"
#include "lamp.h"
#include "protocol.h"
#include "state.h"
#include "store.h"

// The Mozilla root bundle is already linked into the core, so the broker is
// validated against public CAs with no PEM in the source and nothing to renew.
// This permanently retires the inline-cert-with-a-hidden-expiry pattern that
// bit this project once already.
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

static WiFiClientSecure espClient;
static PubSubClient mqtt(espClient);
static WiFiManager wm;

static char tState[128];
static char tStatus[176];
static char tStatusWild[160];
static char tTele[176];

// Wi-Fi events run on the arduino_events task. They may only set flags --
// calling WiFi.begin() from in there can deadlock against the very queue it
// needs to drain.
static volatile bool sWifiUp = false;
static volatile bool sWantRearm = false;

static uint8_t sWifiStage = 0;
static uint8_t sMqttStage = 0;
static uint32_t sBackoffUntil = 0;
static uint32_t sWifiStartMs = 0;
static uint32_t sLastGoodMs = 0;
static uint8_t sLadderStep = 0;
static uint8_t sHeapStrikes = 0;
static uint32_t sLastHeapCheckMs = 0;
static uint32_t sReconnects = 0;

// A local toggle that has not made it onto the wire yet.
static bool sPendValid = false;
static bool sPendOn = false;
static uint32_t sPendSeq = 0;
static uint32_t sPendTs = 0;

// QoS 0 publish + the broker's own echo used as an ACK.
static bool sAwaitEcho = false;
static uint32_t sEchoSeq = 0;
static uint32_t sEchoDeadline = 0;
static uint8_t sEchoTries = 0;

static uint32_t sSubProbeDeadline = 0;
static bool sSawOwnStatus = false;

static char sStateOrigin[24] = {0};
static uint32_t sLastRefreshMs = 0;
static uint32_t sLastTeleMs = 0;

// Portal fields. These must outlive startConfigPortal(), hence file scope.
static char pHost[64];
static char pPort[8];
static char pUser[32];
static char pPass[48];
static char pGroup[32];
static WiFiManagerParameter *wpHost;
static WiFiManagerParameter *wpPort;
static WiFiManagerParameter *wpUser;
static WiFiManagerParameter *wpPass;
static WiFiManagerParameter *wpGroup;
static bool sPortalParamsBuilt = false;

// ---------------------------------------------------------------- helpers

const char *netStateName(uint8_t s) {
  switch (s) {
    case NET_BOOT: return "boot";
    case NET_WIFI_CONNECTING: return "wifi-connecting";
    case NET_WIFI_BACKOFF: return "wifi-backoff";
    case NET_MQTT_CONNECTING: return "mqtt-connecting";
    case NET_MQTT_BACKOFF: return "mqtt-backoff";
    case NET_ONLINE: return "online";
    case NET_PORTAL: return "portal";
    case NET_RECOVERING: return "recovering";
    default: return "?";
  }
}

const char *netFailName(uint8_t f) {
  switch (f) {
    case FAIL_WIFI_NO_AP: return "no-ap";
    case FAIL_WIFI_AUTH: return "auth-fail";
    case FAIL_DNS: return "dns";
    case FAIL_CAPTIVE_PORTAL: return "captive-portal";
    case FAIL_HEAP: return "heap";
    case FAIL_TLS: return "tls";
    case FAIL_MQTT: return "mqtt";
    case FAIL_SUBSCRIBE: return "subscribe-denied";
    default: return "none";
  }
}

// Full jitter. Matters even with two lamps: after a power cut both boot within
// milliseconds of each other and would otherwise hammer the broker in lockstep
// forever, and the free tiers rate-limit connections.
static uint32_t nextDelay(uint8_t &stage, uint32_t base, uint32_t cap) {
  uint8_t shift = stage > 8 ? 8 : stage;
  uint32_t window = base << shift;
  if (window > cap) window = cap;
  if (stage < 8) stage++;
  uint32_t lo = window / 4;
  return lo + (esp_random() % (window - lo + 1));
}

static void buildTopics() {
  const char *g = gBroker.group[0] ? gBroker.group : DEFAULT_GROUP_ID;
  snprintf(tState, sizeof(tState), TOPIC_ROOT "/%s/state", g);
  snprintf(tStatus, sizeof(tStatus), TOPIC_ROOT "/%s/heart/%s/status", g,
           gDeviceId);
  snprintf(tStatusWild, sizeof(tStatusWild), TOPIC_ROOT "/%s/heart/+/status", g);
  snprintf(tTele, sizeof(tTele), TOPIC_ROOT "/%s/heart/%s/tele", g, gDeviceId);
}

static void applyState(bool on, const char *origin, uint32_t seq, uint32_t ts) {
  logicalSet(on);
  snprintf(sStateOrigin, sizeof(sStateOrigin), "%s", origin ? origin : "");
  storeQueueState(on, ts, seq);
}

// ---------------------------------------------------------------- wifi

static void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      // The real "up". STA_CONNECTED is association only -- there is no IP yet,
      // and starting MQTT there is the classic mistake.
      sWifiUp = true;
      sWifiStage = 0;
      sMqttStage = 0;
      gWifiAuthFailStreak = 0;
      configTime(0, 0, "pool.ntp.org", "time.nist.gov");
      break;

    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      // DHCP lease gone while still associated. Easy to miss, and it leaves a
      // permanently half-open socket if unhandled.
      sWifiUp = false;
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
      sWifiUp = false;
      uint8_t reason = info.wifi_sta_disconnected.reason;
      if (reason == WIFI_REASON_NO_AP_FOUND) {
        gLastFail = FAIL_WIFI_NO_AP;
        if (sWifiStage < 4) sWifiStage = 4;
      } else if (reason == WIFI_REASON_AUTH_FAIL ||
                 reason == WIFI_REASON_AUTH_EXPIRE ||
                 reason == WIFI_REASON_HANDSHAKE_TIMEOUT) {
        // Configuration-class: the driver stops retrying on its own, so back
        // off hard and light the "re-pair me" pattern after a few.
        gLastFail = FAIL_WIFI_AUTH;
        if (gWifiAuthFailStreak < 255)
          gWifiAuthFailStreak = (uint8_t)(gWifiAuthFailStreak + 1);
        if (sWifiStage < 4) sWifiStage = 4;
      }
      sWantRearm = true;
      break;
    }

    case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE:
      sWantRearm = true;
      break;

    default:
      break;
  }
}

static void enterWifiBackoff() {
  uint32_t d = nextDelay(sWifiStage, WIFI_BACKOFF_BASE_MS, WIFI_BACKOFF_CAP_MS);
  sBackoffUntil = millis() + d;
  gNetState = NET_WIFI_BACKOFF;
  Serial.printf("[net] wifi backoff %lums (%s)\n", (unsigned long)d,
                netFailName(gLastFail));
}

static void startWifi() {
  // The IDF driver refuses a fresh config while an attempt is still in flight
  // ("E wifi:sta is connecting, cannot set config"), which would silently turn
  // our backoff retry into a no-op -- it keeps scanning on the driver's own
  // schedule instead of ours. Drop the in-flight attempt first. eraseap MUST
  // stay false or this wipes the stored credentials.
  if (WiFi.status() != WL_CONNECTED) WiFi.disconnect(false, false);

  // No arguments: with persistent(true) a begin() carrying credentials rewrites
  // the Wi-Fi NVS namespace every time, which at one retry a minute is
  // thousands of flash writes a day. Only WiFiManager passes credentials.
  WiFi.begin();
  sWifiStartMs = millis();
  gNetState = NET_WIFI_CONNECTING;
}

static void rearmSta() {
  Serial.println(F("[net] ladder: re-arming STA"));
  WiFi.disconnect(false, false);
  vTaskDelay(pdMS_TO_TICKS(500));
  startWifi();
}

static void radioCycle() {
  Serial.println(F("[net] ladder: full radio re-init"));
  WiFi.mode(WIFI_OFF);
  vTaskDelay(pdMS_TO_TICKS(1000));
  WiFi.mode(WIFI_STA);
  startWifi();
}

// ---------------------------------------------------------------- mqtt

static bool publishState(bool on, uint32_t seq, uint32_t ts) {
  char buf[PAYLOAD_MAX];
  size_t n = protoEncode(buf, sizeof(buf), on, gDeviceId, seq, ts);
  bool ok = mqtt.publish(tState, (const uint8_t *)buf, n, true);
  if (ok) {
    sAwaitEcho = true;
    sEchoSeq = seq;
    sEchoDeadline = millis() + ECHO_ACK_TIMEOUT_MS;
  }
  return ok;
}

static void onMqttMessage(char *topic, byte *payload, unsigned int len) {
  // Our own retained status coming back proves publish, subscribe and the ACL
  // all work -- one message instead of three separate checks.
  if (strcmp(topic, tStatus) == 0) {
    sSawOwnStatus = true;
    return;
  }
  if (strcmp(topic, tState) != 0) return;

  StateMsg m;
  if (!protoDecode(payload, len, m)) {
    Serial.println(F("[net] undecodable state payload, ignored"));
    return;
  }

  // Own echo: never apply, never republish. Used only to close the loop.
  if (m.ver > 0 && m.origin[0] && strcmp(m.origin, gDeviceId) == 0) {
    if (sAwaitEcho && m.seq == sEchoSeq) {
      sAwaitEcho = false;
      sEchoTries = 0;
    }
    if (sPendValid && m.seq == sPendSeq) sPendValid = false;
    seqObserve(m.seq);
    return;
  }

  ApplyAction a =
      protoDecide(m, gDeviceId, seqCurrent(), logicalOn(), epochOrZero());
  seqObserve(m.seq);

  Serial.printf("[net] rx state=%s origin=%s seq=%lu -> %s\n",
                m.on ? "ON" : "OFF", m.ver ? m.origin : "(legacy)",
                (unsigned long)m.seq, protoActionName(a));

  switch (a) {
    case APPLY_SET:
      applyState(m.on, m.ver ? m.origin : "", m.seq, m.ts);
      break;

    case APPLY_SUPPRESS_STALE: {
      // The one deliberate exception to "receiving never publishes": adopt OFF
      // and clear the retained landmine so the next heart to wake does not
      // inherit it. Terminal by construction -- an OFF cannot be suppressed.
      uint32_t seq = seqNext();
      uint32_t ts = epochOrZero();
      applyState(false, gDeviceId, seq, ts);
      sPendOn = false;
      sPendSeq = seq;
      sPendTs = ts;
      sPendValid = true;
      break;
    }

    default:
      break;
  }
}

static void mqttTeardown(bool graceful) {
  if (graceful && mqtt.connected()) {
    // MQTT suppresses the will on a clean DISCONNECT, so a planned teardown
    // must say "offline" itself or the topic stays "online" forever.
    mqtt.publish(tStatus, "offline", true);
    mqtt.disconnect();
  }
  espClient.stop();
  sAwaitEcho = false;
  sSubProbeDeadline = 0;
}

static void enterMqttBackoff() {
  uint32_t d = nextDelay(sMqttStage, MQTT_BACKOFF_BASE_MS, MQTT_BACKOFF_CAP_MS);
  sBackoffUntil = millis() + d;
  gNetState = NET_MQTT_BACKOFF;
  Serial.printf("[net] mqtt backoff %lums (%s)\n", (unsigned long)d,
                netFailName(gLastFail));
}

static bool mqttConnectStep() {
  IPAddress ip;
  if (!WiFi.hostByName(gBroker.host, ip)) {
    gLastFail = FAIL_DNS;
    return false;
  }

  // A broker hostname resolving to a private address means someone else's DNS
  // answered -- a hotel or café portal. We cannot defeat that from a headless
  // lamp, so name it and stop burning TLS handshakes on it.
  if (ip[0] == 10 || (ip[0] == 192 && ip[1] == 168) ||
      (ip[0] == 172 && (ip[1] & 0xF0) == 16)) {
    gLastFail = FAIL_CAPTIVE_PORTAL;
    return false;
  }

  // Largest contiguous block, not free heap: the failure mode is fragmentation
  // against two static 16 KB TLS record buffers.
  if (ESP.getMaxAllocHeap() < MIN_MAX_ALLOC_BYTES) {
    gLastFail = FAIL_HEAP;
    return false;
  }

  // Re-applied every attempt: the portal can change host or port after
  // netInit() ran, and PubSubClient keeps the pointer we hand it (gBroker.host
  // is a stable global buffer, so that is safe -- but the port is a copy).
  mqtt.setServer(gBroker.host, gBroker.port);

  espClient.setHandshakeTimeout(TLS_HANDSHAKE_TIMEOUT_S); // seconds
  // Connect by hostname so SNI and the CN check work. The timeout here is in
  // MILLISECONDS while setHandshakeTimeout is in seconds -- an inconsistency in
  // the core that is very easy to get wrong.
  if (!espClient.connect(gBroker.host, gBroker.port,
                         (int32_t)TCP_CONNECT_TIMEOUT_MS)) {
    gLastFail = FAIL_TLS;
    return false;
  }

  // PubSubClient reuses an already-open socket, so the TLS session above is
  // what it lands on. cleanSession = true deliberately: a persistent session
  // would queue a week of someone else's toggles and replay them as a strobe.
  bool ok = mqtt.connect(gDeviceId, gBroker.user[0] ? gBroker.user : nullptr,
                         gBroker.pass[0] ? gBroker.pass : nullptr, tStatus, 1,
                         true, "offline", true);
  if (!ok) {
    Serial.printf("[net] mqtt connect failed rc=%d\n", mqtt.state());
    gLastFail = FAIL_MQTT;
    espClient.stop();
    return false;
  }

  // PubSubClient does not wait for SUBACK -- it returns true if the bytes went
  // out. An ACL denial would leave us "connected" and deaf forever, so prove it
  // with the retained-status echo below.
  mqtt.subscribe(tState, 1);
  mqtt.subscribe(tStatusWild, 0);
  sSawOwnStatus = false;
  mqtt.publish(tStatus, "online", true);
  sSubProbeDeadline = millis() + SUB_PROBE_TIMEOUT_MS;

  gLastFail = FAIL_NONE;
  sReconnects++;
  return true;
}

// ---------------------------------------------------------------- portal

static void onPortalSave() {
  BrokerConfig c;
  memset(&c, 0, sizeof(c));
  snprintf(c.host, sizeof(c.host), "%s", wpHost->getValue());
  c.port = (uint16_t)atoi(wpPort->getValue());
  if (c.port == 0) c.port = 8883;
  snprintf(c.user, sizeof(c.user), "%s", wpUser->getValue());
  snprintf(c.pass, sizeof(c.pass), "%s", wpPass->getValue());
  snprintf(c.group, sizeof(c.group), "%s", wpGroup->getValue());
  if (!c.group[0]) snprintf(c.group, sizeof(c.group), "%s", DEFAULT_GROUP_ID);

  if (c.host[0]) {
    storeSaveBroker(c);
    gBroker = c;
    buildTopics();
    Serial.printf("[net] broker saved: %s:%u group=%s\n", gBroker.host,
                  gBroker.port, gBroker.group);
  }
}

static void buildPortalParams() {
  if (sPortalParamsBuilt) return;
  snprintf(pHost, sizeof(pHost), "%s", gBroker.host);
  snprintf(pPort, sizeof(pPort), "%u", gBroker.port ? gBroker.port : 8883);
  snprintf(pUser, sizeof(pUser), "%s", gBroker.user);
  snprintf(pPass, sizeof(pPass), "%s", gBroker.pass);
  snprintf(pGroup, sizeof(pGroup), "%s",
           gBroker.group[0] ? gBroker.group : DEFAULT_GROUP_ID);

  wpHost = new WiFiManagerParameter("bhost", "Broker host", pHost, 63);
  wpPort = new WiFiManagerParameter("bport", "Broker port", pPort, 7);
  wpUser = new WiFiManagerParameter("buser", "Broker user", pUser, 31);
  wpPass = new WiFiManagerParameter("bpass", "Broker password", pPass, 47);
  wpGroup = new WiFiManagerParameter("group", "Group id", pGroup, 31);

  wm.addParameter(wpHost);
  wm.addParameter(wpPort);
  wm.addParameter(wpUser);
  wm.addParameter(wpPass);
  wm.addParameter(wpGroup);
  sPortalParamsBuilt = true;
}

static void startPortal() {
  // The portal's WebServer and DNSServer need RAM, and a live TLS session is
  // holding ~40 KB of it.
  mqttTeardown(true);

  buildPortalParams();
  wm.setConfigPortalBlocking(false);
  // Without a timeout, configPortalHasTimeout() returns false forever and the
  // portal never exits. That, combined with resetSettings(), is exactly how the
  // old firmware bricked itself on an accidental 5-second hold.
  wm.setConfigPortalTimeout(PORTAL_TIMEOUT_S);
  wm.setConfigPortalTimeoutCallback([]() {
    Serial.println(F("[net] portal timed out, credentials left intact"));
    gNetState = NET_WIFI_CONNECTING;
  });
  wm.setCaptivePortalEnable(true);
  wm.setSaveParamsCallback(onPortalSave);

  // Note what is NOT here: wm.resetSettings(). WiFiManager overwrites stored
  // credentials only when new ones are saved, so a timeout leaves a working
  // configuration behind.
  Serial.printf("[net] portal up: %s\n", gPortalSsid);
  wm.startConfigPortal(gPortalSsid, SECRET_PORTAL_PASS);
  gNetState = NET_PORTAL;
}

static void doFactoryReset() {
  Serial.println(F("[net] FACTORY RESET"));
  lampFlashes(3, 800, 200);
  mqttTeardown(true);
  storeClear();
  wm.resetSettings();
  vTaskDelay(pdMS_TO_TICKS(2500));
  ESP.restart();
}

// ---------------------------------------------------------------- recovery

static void plannedRestart(const char *why) {
  Serial.printf("[net] restart: %s\n", why);
  storeSetFailCycles(storeFailCycles() + 1);
  mqttTeardown(true);
  storeFlush();
  lampFlashes(5, 100, 100);
  vTaskDelay(pdMS_TO_TICKS(1200));
  ESP.restart();
}

static void ladderTick() {
  if (gNetState == NET_ONLINE || gNetState == NET_PORTAL) {
    sLadderStep = 0;
    return;
  }

  uint32_t since = millis() - sLastGoodMs;
  uint32_t restartAt = (storeFailCycles() >= LADDER_MAX_CYCLES)
                           ? LADDER_RESTART_SLOW_MS
                           : LADDER_RESTART_MS;

  if (since > restartAt) {
    plannedRestart("ladder exhausted");
  } else if (since > LADDER_RADIO_AGAIN_MS && sLadderStep < 3) {
    sLadderStep = 3;
    radioCycle();
  } else if (since > LADDER_RADIO_CYCLE_MS && sLadderStep < 2) {
    sLadderStep = 2;
    radioCycle();
  } else if (since > LADDER_REARM_STA_MS && sLadderStep < 1) {
    sLadderStep = 1;
    rearmSta();
  }
}

static void heapTick() {
  if (millis() - sLastHeapCheckMs < 60000) return;
  sLastHeapCheckMs = millis();

  if (ESP.getMaxAllocHeap() < MIN_MAX_ALLOC_BYTES ||
      ESP.getFreeHeap() < MIN_FREE_HEAP_BYTES) {
    sHeapStrikes++;
    Serial.printf("[net] heap strike %u (max_alloc=%lu free=%lu)\n", sHeapStrikes,
                  (unsigned long)ESP.getMaxAllocHeap(),
                  (unsigned long)ESP.getFreeHeap());
    if (sHeapStrikes >= HEAP_STRIKES_BEFORE_RESTART) plannedRestart("heap");
  } else {
    sHeapStrikes = 0;
  }
}

static void telemetryTick() {
  if (millis() - sLastTeleMs < TELEMETRY_MS) return;
  sLastTeleMs = millis();

  char buf[220];
  snprintf(buf, sizeof(buf),
           "rssi=%d uptime=%lu heap=%lu maxalloc=%lu reconnects=%lu "
           "reset=%d state=%s fail=%s",
           WiFi.RSSI(), (unsigned long)(millis() / 1000),
           (unsigned long)ESP.getFreeHeap(),
           (unsigned long)ESP.getMaxAllocHeap(), (unsigned long)sReconnects,
           (int)esp_reset_reason(), netStateName(gNetState),
           netFailName(gLastFail));
  mqtt.publish(tTele, buf, false);
}

// Keep an ON that someone is actually holding from decaying, while letting an
// abandoned one expire. Only the heart that owns the state refreshes it.
static void refreshTick() {
  if (!logicalOn()) return;
  if (strcmp(sStateOrigin, gDeviceId) != 0) return;
  if (millis() - sLastRefreshMs < STATE_REFRESH_MS) return;
  sLastRefreshMs = millis();
  publishState(true, seqNext(), epochOrZero());
}

// ---------------------------------------------------------------- intents

static void handleIntents() {
  ToggleIntent it;
  while (xQueueReceive(gIntentQ, &it, 0) == pdTRUE) {
    sPendOn = it.on;
    sPendSeq = it.seq;
    sPendTs = it.ts;
    sPendValid = true;
    snprintf(sStateOrigin, sizeof(sStateOrigin), "%s", gDeviceId);
    sLastRefreshMs = millis();
    storeQueueState(it.on, it.ts, it.seq);
  }
}

// ---------------------------------------------------------------- ticks

static void tickOnline() {
  if (!sWifiUp) {
    mqttTeardown(false);
    startWifi();
    return;
  }
  if (!mqtt.connected()) {
    mqttTeardown(false);
    gLastFail = FAIL_MQTT;
    enterMqttBackoff();
    return;
  }

  // Publish a queued local toggle BEFORE draining inbound. Its sequence is
  // higher than the retained value's, so protoDecide rejects the stale retained
  // message on merit -- no timing window, no "ignore for 5 seconds" hack. This
  // is the fix for the lamp turning itself back off after an offline press.
  if (sPendValid && !sAwaitEcho) {
    if (sPendTs != 0 && epochOrZero() != 0 &&
        epochOrZero() - sPendTs > STALE_ON_WINDOW_S) {
      Serial.println(F("[net] dropping stale pending publish"));
      sPendValid = false;
    } else {
      publishState(sPendOn, sPendSeq, sPendTs);
    }
  }

  mqtt.loop();

  if (sAwaitEcho && (int32_t)(millis() - sEchoDeadline) >= 0) {
    sAwaitEcho = false;
    sEchoTries++;
    if (sEchoTries >= ECHO_ACK_MAX_TRIES) {
      // publish() returned true into what is probably a half-open socket.
      Serial.println(F("[net] no echo after retries, cycling session"));
      sEchoTries = 0;
      mqttTeardown(false);
      gLastFail = FAIL_MQTT;
      enterMqttBackoff();
      return;
    }
  }

  if (sSubProbeDeadline && (int32_t)(millis() - sSubProbeDeadline) >= 0) {
    sSubProbeDeadline = 0;
    if (!sSawOwnStatus) {
      Serial.println(F("[net] no status echo -- subscribe or ACL denied"));
      gLastFail = FAIL_SUBSCRIBE;
      mqttTeardown(false);
      enterMqttBackoff();
      return;
    }
  }

  refreshTick();
  telemetryTick();
  sLastGoodMs = millis();
}

void netInit() {
  if (!storeLoadBroker(gBroker)) {
    // Fall back to whatever the optional secrets.h seeded. Empty is fine --
    // the portal is the intended provisioning path.
    memset(&gBroker, 0, sizeof(gBroker));
    snprintf(gBroker.host, sizeof(gBroker.host), "%s", SECRET_MQTT_HOST);
    gBroker.port = SECRET_MQTT_PORT;
    snprintf(gBroker.user, sizeof(gBroker.user), "%s", SECRET_MQTT_USER);
    snprintf(gBroker.pass, sizeof(gBroker.pass), "%s", SECRET_MQTT_PASS);
    snprintf(gBroker.group, sizeof(gBroker.group), "%s", SECRET_GROUP_ID);
  }
  buildTopics();

  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  // Modem sleep causes dropouts on cheap APs, adds MQTT latency, and silently
  // eats inbound ESP-NOW frames (which Stage B will need).
  WiFi.setSleep(false);
  WiFi.setHostname(gDeviceId);
  WiFi.mode(WIFI_STA);
  WiFi.onEvent(onWifiEvent);

  espClient.setCACertBundle(rootca_crt_bundle_start,
                            rootca_crt_bundle_end - rootca_crt_bundle_start);

  mqtt.setKeepAlive(MQTT_KEEPALIVE_S);
  mqtt.setSocketTimeout(MQTT_SOCKET_TIMEOUT_S);
  // Not optional: PubSubClient's CHECK_STRING_LENGTH silently returns false if
  // the CONNECT packet does not fit, which presents as a permanent, unexplained
  // connect failure. The default 256 is uncomfortably close for our topics.
  mqtt.setBufferSize(MQTT_BUFFER_BYTES);
  mqtt.setServer(gBroker.host, gBroker.port);
  mqtt.setCallback(onMqttMessage);

  sLastGoodMs = millis();
}

void netTask(void *arg) {
  (void)arg;
  for (;;) {
    gNetHeartbeatMs = millis();

    if (gWantFactoryReset) doFactoryReset(); // does not return

    if (gButtonActivity) {
      // Someone is standing at the lamp. Retry now rather than making them
      // wait out a five-minute backoff.
      gButtonActivity = false;
      sWifiStage = 0;
      sMqttStage = 0;
      sBackoffUntil = millis();
    }

    if (gWantPortal) {
      gWantPortal = false;
      startPortal();
    }

    handleIntents();

    if (sWantRearm && gNetState != NET_PORTAL) {
      sWantRearm = false;
      if (gNetState == NET_ONLINE || gNetState == NET_MQTT_CONNECTING ||
          gNetState == NET_MQTT_BACKOFF) {
        mqttTeardown(false);
        enterWifiBackoff();
      }
    }

    switch (gNetState) {
      case NET_BOOT:
        if (!wm.getWiFiIsSaved()) {
          Serial.println(F("[net] no saved Wi-Fi, opening portal"));
          startPortal();
        } else {
          startWifi();
        }
        break;

      case NET_WIFI_CONNECTING:
        if (sWifiUp) {
          gNetState = NET_MQTT_CONNECTING;
        } else if (millis() - sWifiStartMs > WIFI_JOIN_TIMEOUT_MS) {
          enterWifiBackoff();
        }
        break;

      case NET_WIFI_BACKOFF:
        if (sWifiUp) {
          gNetState = NET_MQTT_CONNECTING;
        } else if ((int32_t)(millis() - sBackoffUntil) >= 0) {
          startWifi();
        }
        break;

      case NET_MQTT_CONNECTING:
        if (!sWifiUp) {
          startWifi();
        } else if (!brokerConfigured()) {
          gLastFail = FAIL_MQTT;
          Serial.println(F("[net] no broker configured -- hold button 5s"));
          enterMqttBackoff();
        } else if (mqttConnectStep()) {
          Serial.println(F("[net] online"));
          gNetState = NET_ONLINE;
          sLastGoodMs = millis();
          sMqttStage = 0;
          storeSetFailCycles(0);
        } else {
          enterMqttBackoff();
        }
        break;

      case NET_MQTT_BACKOFF:
        if (!sWifiUp) {
          startWifi();
        } else if ((int32_t)(millis() - sBackoffUntil) >= 0) {
          gNetState = NET_MQTT_CONNECTING;
        }
        break;

      case NET_ONLINE:
        tickOnline();
        break;

      case NET_PORTAL:
        if (wm.process()) {
          Serial.println(F("[net] portal saved credentials"));
          gNetState = NET_WIFI_CONNECTING;
          sWifiStartMs = millis();
        }
        break;

      default:
        gNetState = NET_BOOT;
        break;
    }

    ladderTick();
    heapTick();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
