#include "button.h"

#include "config.h"
#include "lamp.h"

volatile bool gButtonActivity = false;

static bool sStable = false;    // debounced: true == pressed
static bool sLastRaw = false;
static uint32_t sLastChangeMs = 0;
static uint32_t sPressStartMs = 0;
static uint8_t sBlipsGiven = 0;

void buttonInit() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  sLastRaw = false;
  sStable = false;
  sLastChangeMs = millis();
}

static bool readRaw() {
  int v = digitalRead(PIN_BUTTON);
  return BUTTON_ACTIVE_LOW ? (v == LOW) : (v == HIGH);
}

// How many thresholds this hold has crossed so far.
static uint8_t thresholdsFor(uint32_t held) {
  if (held >= HOLD_ABORT_MS) return 4;
  if (held >= HOLD_FACTORY_MS) return 3;
  if (held >= HOLD_PORTAL_MS) return 2;
  if (held >= HOLD_PAIR_MS) return 1;
  return 0;
}

Gesture buttonTick() {
  uint32_t now = millis();

  // Ignore the button briefly at boot: a button held through power-up, or a
  // pin still floating between pinMode() and the first sample, must not
  // register as a press.
  if (now < BOOT_INPUT_LOCKOUT_MS) return GESTURE_NONE;

  bool raw = readRaw();
  if (raw != sLastRaw) {
    sLastRaw = raw;
    sLastChangeMs = now;
  }

  if (now - sLastChangeMs < BUTTON_DEBOUNCE_MS) {
    // Still settling, but keep marking thresholds on a hold in progress.
    if (sStable) {
      uint8_t want = thresholdsFor(now - sPressStartMs);
      if (want > sBlipsGiven) {
        sBlipsGiven = want;
        lampBlip();
      }
    }
    return GESTURE_NONE;
  }

  if (sLastRaw == sStable) {
    if (sStable) {
      uint8_t want = thresholdsFor(now - sPressStartMs);
      if (want > sBlipsGiven) {
        sBlipsGiven = want;
        lampBlip();
      }
    }
    return GESTURE_NONE;
  }

  // Debounced edge.
  sStable = sLastRaw;

  if (sStable) { // pressed
    sPressStartMs = now;
    sBlipsGiven = 0;
    gButtonActivity = true;
    return GESTURE_NONE;
  }

  // Released -- classify the hold.
  uint32_t held = now - sPressStartMs;
  if (held <= HOLD_TOGGLE_MAX_MS) return GESTURE_TOGGLE;
  if (held < HOLD_PAIR_MS) return GESTURE_NONE; // dead zone
  if (held < HOLD_PORTAL_MS) return GESTURE_PAIR;
  if (held < HOLD_FACTORY_MS) return GESTURE_PORTAL;
  if (held < HOLD_ABORT_MS) return GESTURE_FACTORY;
  return GESTURE_ABORT;
}

const char *gestureName(Gesture g) {
  switch (g) {
    case GESTURE_TOGGLE: return "toggle";
    case GESTURE_PAIR: return "pair";
    case GESTURE_PORTAL: return "portal";
    case GESTURE_FACTORY: return "factory-reset";
    case GESTURE_ABORT: return "abort";
    default: return "none";
  }
}
