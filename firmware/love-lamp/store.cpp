#include "store.h"

#include <Preferences.h>

static Preferences prefs;
static bool sOpen = false;

// Debounced pending write.
static bool sPendValid = false;
static bool sPendOn = false;
static uint32_t sPendTs = 0;
static uint32_t sPendSeq = 0;
static uint32_t sPendAtMs = 0;

bool storeInit() {
  // A corrupt or full NVS must not be fatal -- run on defaults and let the
  // failure show up in telemetry instead of bricking the lamp.
  sOpen = prefs.begin("lh", false);
  if (!sOpen) {
    Serial.println(F("[store] NVS unavailable, running on defaults"));
  }
  return sOpen;
}

bool storeLoadBroker(BrokerConfig &out) {
  memset(&out, 0, sizeof(out));
  if (!sOpen) return false;

  String host = prefs.getString("bhost", "");
  if (host.length() == 0) return false;

  snprintf(out.host, sizeof(out.host), "%s", host.c_str());
  out.port = prefs.getUShort("bport", 8883);
  snprintf(out.user, sizeof(out.user), "%s",
           prefs.getString("buser", "").c_str());
  snprintf(out.pass, sizeof(out.pass), "%s",
           prefs.getString("bpass", "").c_str());
  snprintf(out.group, sizeof(out.group), "%s",
           prefs.getString("group", DEFAULT_GROUP_ID).c_str());
  return true;
}

void storeSaveBroker(const BrokerConfig &in) {
  if (!sOpen) return;
  prefs.putString("bhost", in.host);
  prefs.putUShort("bport", in.port);
  prefs.putString("buser", in.user);
  prefs.putString("bpass", in.pass);
  prefs.putString("group", in.group[0] ? in.group : DEFAULT_GROUP_ID);
}

bool storeLightOn() {
  if (!sOpen) return false;
  return prefs.getUChar("st", 0) != 0;
}

uint32_t storeLightTs() {
  if (!sOpen) return 0;
  return prefs.getULong("stTs", 0);
}

uint32_t storeSeq() {
  if (!sOpen) return 0;
  return prefs.getULong("seq", 0);
}

void storeQueueState(bool on, uint32_t ts, uint32_t seq) {
  sPendOn = on;
  sPendTs = ts;
  sPendSeq = seq;
  sPendAtMs = millis();
  sPendValid = true;
}

void storeFlush() {
  if (!sPendValid || !sOpen) {
    sPendValid = false;
    return;
  }
  prefs.putUChar("st", sPendOn ? 1 : 0);
  prefs.putULong("stTs", sPendTs);
  prefs.putULong("seq", sPendSeq);
  sPendValid = false;
}

void storeTick() {
  if (!sPendValid) return;
  if (millis() - sPendAtMs >= NVS_WRITE_DEBOUNCE_MS) storeFlush();
}

uint32_t storeBumpBootCount() {
  if (!sOpen) return 0;
  uint32_t n = prefs.getULong("boot", 0) + 1;
  prefs.putULong("boot", n);
  return n;
}

uint8_t storeFailCycles() {
  if (!sOpen) return 0;
  return prefs.getUChar("failCyc", 0);
}

void storeSetFailCycles(uint8_t n) {
  if (!sOpen) return;
  prefs.putUChar("failCyc", n);
}

void storeClear() {
  if (!sOpen) return;
  prefs.clear();
}
