// love-lamp -- two (or more) ESP32 lamps that mirror one light state.
//
// Press the button on any heart and every heart follows, over MQTT. The same
// binary runs on every board: identity is derived from the chip's own eFuse MAC
// at runtime, and broker credentials live in NVS, provisioned once through the
// Wi-Fi config portal. There is nothing to edit between the two uploads.
//
// Task split:
//   loop()  (core 1)  button + lamp. Never touches the network, so the button
//                     stays instant even while TLS is stalled.
//   netTask (core 0)  Wi-Fi, TLS, MQTT, the connection state machine.
// They meet only through gIntentQ and the mutex in state.cpp.
//
// Gestures (acted on release; the lamp blips as each threshold is crossed):
//   tap            toggle
//   2 s            pairing window   (Stage B -- inert for now)
//   5 s            Wi-Fi portal
//   11 s           factory reset
//   20 s+          abort
//
// Flashing: ESP32 Dev Module, 115200 monitor. Keep "Erase All Flash Before
// Sketch Upload" DISABLED and never change the Partition Scheme -- either one
// wipes NVS and de-provisions the lamp.

#include "button.h"
#include "config.h"
#include "defaults.h"
#include "lamp.h"
#include "net.h"
#include "protocol.h"
#include "selftest.h"
#include "state.h"
#include "store.h"

#include <esp_system.h>

// A stored ON restored after a power-on reset might be days old. We light it
// immediately so the lamp looks right, then re-check once SNTP gives us a
// clock. Our own recovery reboots skip this entirely -- a self-heal must be
// invisible to the user.
static bool sBootGatePending = false;
static uint32_t sBootStoredTs = 0;
static uint32_t sBootGateStartMs = 0;

static const char *resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "int-wdt";
    case ESP_RST_TASK_WDT: return "task-wdt";
    case ESP_RST_WDT: return "other-wdt";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    default: return "unknown";
  }
}

static void restoreBootState() {
  esp_reset_reason_t rr = esp_reset_reason();
  bool storedOn = storeLightOn();
  sBootStoredTs = storeLightTs();

  if (!storedOn) {
    logicalSet(false);
    return;
  }

  bool ourOwnReboot = (rr == ESP_RST_SW || rr == ESP_RST_PANIC ||
                       rr == ESP_RST_TASK_WDT || rr == ESP_RST_INT_WDT ||
                       rr == ESP_RST_WDT);

  logicalSet(true);
  if (!ourOwnReboot) {
    sBootGatePending = true;
    sBootGateStartMs = millis();
  }
}

static void bootGateTick() {
  if (!sBootGatePending) return;

  uint32_t now = epochOrZero();
  if (now == 0) {
    // No clock after a minute of trying: leave the lamp as it was rather than
    // switching it off on a guess.
    if (millis() - sBootGateStartMs > 60000) sBootGatePending = false;
    return;
  }

  sBootGatePending = false;
  if (sBootStoredTs != 0 && now > sBootStoredTs &&
      (now - sBootStoredTs) > STALE_ON_WINDOW_S) {
    Serial.println(F("[boot] stored ON is stale, switching off"));
    logicalSet(false);
    storeQueueState(false, now, seqNext());
  }
}

void setup() {
  Serial.begin(115200);
  delay(50);

  lampInit();
  buttonInit();
  identityInit();
  storeInit();

  if (!stateInit()) {
    // Without the queue and mutex the two-task split is unsafe. Better to
    // reboot than to run with them missing.
    Serial.println(F("[boot] state init failed, restarting"));
    delay(2000);
    ESP.restart();
  }

  uint32_t boots = storeBumpBootCount();
  restoreBootState();
  netInit();

  Serial.println();
  Serial.println(F("=== love-lamp ==="));
  Serial.printf("device   : %s (%s)\n", gDeviceId, gDeviceName);
  Serial.printf("portal   : %s\n", gPortalSsid);
  Serial.printf("group    : %s\n", gBroker.group);
  Serial.printf("broker   : %s:%u%s\n", gBroker.host[0] ? gBroker.host : "(none)",
                gBroker.port, brokerConfigured() ? "" : "  <- hold button 5s");
  Serial.printf("light    : %s\n", logicalOn() ? "ON" : "OFF");
  Serial.printf("reset    : %s   boot #%lu\n",
                resetReasonName(esp_reset_reason()), (unsigned long)boots);
  Serial.printf("heap     : free=%lu maxalloc=%lu\n",
                (unsigned long)ESP.getFreeHeap(),
                (unsigned long)ESP.getMaxAllocHeap());
  Serial.println(F("================="));

  if (selfTestRun() != 0) {
    // The reconciliation rules are wrong. The lamp will still work as a local
    // switch, but flag it unmistakably rather than trusting the mirror logic.
    lampFlashes(10, 80, 80);
  }

  xTaskCreatePinnedToCore(netTask, "net", 8192, nullptr, 1, nullptr, 0);

  // loop() now only debounces a button and writes a GPIO, so a 5 s stall
  // genuinely means something is wrong. This is a real detector, not decoration.
  enableLoopWDT();
}

void loop() {
  switch (buttonTick()) {
    case GESTURE_TOGGLE: {
      bool on = !logicalOn();
      logicalSet(on); // instant, before the network hears about it
      ToggleIntent it = {on, seqNext(), epochOrZero()};
      xQueueSend(gIntentQ, &it, 0);
      Serial.printf("[btn] toggle -> %s\n", on ? "ON" : "OFF");
      break;
    }

    case GESTURE_PAIR:
      // Stage B. The threshold and its blip ship now so the gesture timings
      // never shift under the user's fingers when pairing arrives.
      Serial.println(F("[btn] pairing not available yet"));
      lampFlashes(2, 120, 120);
      break;

    case GESTURE_PORTAL:
      Serial.println(F("[btn] opening config portal"));
      lampFlashes(3, 150, 150);
      gWantPortal = true;
      break;

    case GESTURE_FACTORY:
      Serial.println(F("[btn] factory reset requested"));
      gWantFactoryReset = true;
      break;

    case GESTURE_ABORT:
      Serial.println(F("[btn] gesture aborted"));
      lampFlashes(1, 400, 200);
      break;

    default:
      break;
  }

  bootGateTick();
  lampTick();
  storeTick();

  // netTask legitimately blocks for up to ~30 s inside a TLS connect, so the
  // hardware watchdog would be wrong here; supervise it in software with 4x
  // margin instead.
  if (gNetHeartbeatMs != 0 &&
      (millis() - gNetHeartbeatMs) > NET_HEARTBEAT_TIMEOUT_MS) {
    Serial.println(F("[wdt] netTask wedged, restarting"));
    storeFlush();
    delay(100);
    ESP.restart();
  }

  delay(1);
}
