// button.h -- debounce and gesture recognition.
//
// Gestures are decided on RELEASE, not while held. The lamp blips as each
// threshold is crossed, so the user counts blips and lets go at the one they
// want -- which also means over-holding is recoverable instead of committing
// to whatever fired silently underneath their finger.
//
//   < 600 ms      toggle                        0 blips
//   0.6 - 1.9 s   nothing (kills accidents)     0
//   2 - 4.9 s     pairing window (Stage B)      1
//   5 - 10.9 s    Wi-Fi config portal           2
//   11 - 19.9 s   factory reset                 3
//   >= 20 s       abort, do nothing             4
//
// The old firmware fired the portal *while still held* at 5 s and, worse,
// wiped the Wi-Fi credentials first -- which left the lamp in a permanent
// blinking AP mode with a dead button. Neither happens here.

#pragma once
#include <Arduino.h>

enum Gesture : uint8_t {
  GESTURE_NONE,
  GESTURE_TOGGLE,
  GESTURE_PAIR,
  GESTURE_PORTAL,
  GESTURE_FACTORY,
  GESTURE_ABORT,
};

// Set on every press edge; netTask consumes it to reset connection backoff.
// If the user is standing at the lamp, retrying now is the right call.
extern volatile bool gButtonActivity;

void buttonInit();
Gesture buttonTick();
const char *gestureName(Gesture g);
