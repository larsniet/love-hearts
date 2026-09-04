// store.h -- NVS persistence via Preferences.
//
// Survives a normal sketch upload, which is the whole point: broker credentials
// are provisioned once per board and then left alone. Two things destroy it:
//   * Tools > Erase All Flash Before Sketch Upload  (keep Disabled)
//   * changing Tools > Partition Scheme             (relocates the nvs region)

#pragma once
#include <Arduino.h>

#include "state.h"

bool storeInit();

// Broker config. Returns false when nothing has been provisioned yet.
bool storeLoadBroker(BrokerConfig &out);
void storeSaveBroker(const BrokerConfig &in);

// Light state. Writes are debounced so a mashed button coalesces into one
// record; storeTick() must be called from loop(). ESP-IDF skips a write whose
// value is unchanged, so idempotent saves cost nothing.
bool storeLightOn();
uint32_t storeLightTs();
uint32_t storeSeq();
void storeQueueState(bool on, uint32_t ts, uint32_t seq);
void storeTick();
void storeFlush();

// Boot bookkeeping, for telemetry and the recovery ladder.
uint32_t storeBumpBootCount();
uint8_t storeFailCycles();
void storeSetFailCycles(uint8_t n);

// Factory reset. Wi-Fi credentials are WiFiManager's, cleared separately.
void storeClear();
