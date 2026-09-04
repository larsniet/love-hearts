// lamp.h -- light output and the blink vocabulary.
//
// Status patterns play only while the lamp is logically OFF, so diagnostics
// never mask the actual mirror state. When it is ON, it is just on.
//
//   online, off        dark          (it lives in a bedroom; no heartbeat blink)
//   connecting         100 / 900
//   wifi backoff       100 / 2900
//   wifi auth failing  3 short / 2s  -- "re-pair me"
//   mqtt backoff       double / 2.5s
//   captive portal     long-short-long / 2s
//   config portal      500 / 500
//   factory armed      100 / 100 continuous
//
// Transients (gesture blips, confirmations, pre-reboot) override everything.

#pragma once
#include <Arduino.h>

void lampInit();
void lampTick();

// Invert the lamp briefly, whatever it is currently doing. Used to mark each
// gesture threshold as the user crosses it, so they can count and let go.
void lampBlip();

// A bounded flash sequence, e.g. gesture confirmation or pre-reboot warning.
void lampFlashes(uint8_t count, uint16_t onMs, uint16_t offMs);

bool lampTransientActive();
