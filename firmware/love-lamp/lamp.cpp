#include "lamp.h"

#include "config.h"
#include "state.h"

static bool sPhysical = false;

static uint32_t sBlipUntil = 0;

static uint8_t sFlashCount = 0;
static uint32_t sFlashStart = 0;
static uint16_t sFlashOn = 0;
static uint16_t sFlashOff = 0;

// Patterns alternate on/off durations, starting with on, and cycle.
static const uint16_t PAT_CONNECTING[] = {100, 900};
static const uint16_t PAT_WIFI_BACKOFF[] = {100, 2900};
static const uint16_t PAT_WIFI_AUTH[] = {100, 150, 100, 150, 100, 2000};
static const uint16_t PAT_MQTT_BACKOFF[] = {100, 150, 100, 2550};
static const uint16_t PAT_CAPTIVE[] = {500, 200, 100, 200, 500, 2000};
static const uint16_t PAT_PORTAL[] = {500, 500};
static const uint16_t PAT_FACTORY[] = {100, 100};

static void write(bool on) {
  if (on == sPhysical) return; // only touch the pin on a real change
  digitalWrite(PIN_LIGHT, on ? HIGH : LOW);
  sPhysical = on;
}

void lampInit() {
  // Drive the level before switching to output so the pin never sits as an
  // output with an undefined level.
  digitalWrite(PIN_LIGHT, LOW);
  pinMode(PIN_LIGHT, OUTPUT);
  digitalWrite(PIN_LIGHT, LOW);
  sPhysical = false;
}

void lampBlip() { sBlipUntil = millis() + BLIP_MS; }

void lampFlashes(uint8_t count, uint16_t onMs, uint16_t offMs) {
  sFlashCount = count;
  sFlashOn = onMs;
  sFlashOff = offMs;
  sFlashStart = millis();
}

bool lampTransientActive() {
  return sFlashCount > 0 || (int32_t)(sBlipUntil - millis()) > 0;
}

static bool patternPhase(const uint16_t *pat, size_t len, uint32_t now) {
  uint32_t total = 0;
  for (size_t i = 0; i < len; i++) total += pat[i];
  if (total == 0) return false;

  uint32_t t = now % total;
  for (size_t i = 0; i < len; i++) {
    if (t < pat[i]) return (i % 2) == 0; // even index = on
    t -= pat[i];
  }
  return false;
}

#define PAT(p) p, (sizeof(p) / sizeof((p)[0]))

void lampTick() {
  uint32_t now = millis();

  // Bounded flash sequence wins over everything.
  if (sFlashCount > 0) {
    uint32_t period = (uint32_t)sFlashOn + sFlashOff;
    uint32_t elapsed = now - sFlashStart;
    if (period == 0 || elapsed >= period * sFlashCount) {
      sFlashCount = 0;
    } else {
      write((elapsed % period) < sFlashOn);
      return;
    }
  }

  // Gesture blip: invert whatever the lamp would otherwise show.
  if ((int32_t)(sBlipUntil - now) > 0) {
    write(!logicalOn());
    return;
  }

  // The actual mirror state takes precedence over diagnostics.
  if (logicalOn()) {
    write(true);
    return;
  }

  if (gWantFactoryReset) {
    write(patternPhase(PAT(PAT_FACTORY), now));
    return;
  }

  switch (gNetState) {
    case NET_ONLINE:
      write(false);
      break;
    case NET_PORTAL:
      write(patternPhase(PAT(PAT_PORTAL), now));
      break;
    case NET_WIFI_BACKOFF:
    case NET_RECOVERING:
      if (gWifiAuthFailStreak >= 5)
        write(patternPhase(PAT(PAT_WIFI_AUTH), now));
      else
        write(patternPhase(PAT(PAT_WIFI_BACKOFF), now));
      break;
    case NET_MQTT_BACKOFF:
      if (gLastFail == FAIL_CAPTIVE_PORTAL)
        write(patternPhase(PAT(PAT_CAPTIVE), now));
      else
        write(patternPhase(PAT(PAT_MQTT_BACKOFF), now));
      break;
    default:
      write(patternPhase(PAT(PAT_CONNECTING), now));
      break;
  }
}
