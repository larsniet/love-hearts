// protocol.h -- state payload codec and reconciliation rules.
//
// Wire format, pipe-delimited so it parses with strtok_r and no library:
//
//     1|ON|heart-34AB12CD5678|97|1757001234
//     ^  ^  ^                  ^  ^
//     |  |  origin device id   |  epoch seconds (0 = clock not synced)
//     |  state                 Lamport sequence
//     schema version
//
// ArduinoJson is deliberately not used: writing JSON is trivial with snprintf,
// but *parsing* it in a 250-byte budget is where the bugs live.
//
// A bare "ON"/"OFF" is accepted as legacy v0 (that is what the old firmware and
// the now-frozen web app emit). It carries no origin and no timestamp, so it is
// applied idempotently and cannot participate in tie-breaking.

#pragma once
#include <Arduino.h>

#include "config.h"

struct StateMsg {
  bool valid;
  uint8_t ver; // 0 = legacy bare ON/OFF
  bool on;
  char origin[24];
  uint32_t seq;
  uint32_t ts;
};

// PubSubClient hands over a payload that is NOT NUL-terminated -- the most
// common crash in that library. This copies into a bounded buffer first.
bool protoDecode(const uint8_t *payload, unsigned int len, StateMsg &out);

size_t protoEncode(char *buf, size_t cap, bool on, const char *origin,
                   uint32_t seq, uint32_t ts);

enum ApplyAction : uint8_t {
  APPLY_IGNORE,         // own echo, replay, or we win the tie
  APPLY_SET,            // adopt msg.on
  APPLY_SUPPRESS_STALE, // an ON too old to honour: adopt OFF and republish it
};

// Pure decision function, so the awkward cases are testable by eye.
//   selfId     our device id
//   lastSeq    highest sequence we have observed
//   currentOn  our current logical state
//   nowEpoch   epoch seconds, or 0 if the clock is not synced
ApplyAction protoDecide(const StateMsg &m, const char *selfId, uint32_t lastSeq,
                        bool currentOn, uint32_t nowEpoch);

const char *protoActionName(ApplyAction a);
