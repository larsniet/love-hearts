#include "selftest.h"

#include "config.h"
#include "protocol.h"

#if PROTOCOL_SELFTEST

static int sFails = 0;

static void check(bool ok, const char *what) {
  if (!ok) {
    sFails++;
    Serial.printf("[selftest] FAIL: %s\n", what);
  }
}

static const char *SELF = "heart-AAAA00000001";
static const char *PEER = "heart-0000FFFFFFFF"; // sorts BEFORE self
static const uint32_t NOW = 1757001234UL;

static StateMsg mk(bool on, const char *origin, uint32_t seq, uint32_t ts) {
  StateMsg m;
  memset(&m, 0, sizeof(m));
  m.valid = true;
  m.ver = PROTO_VERSION;
  m.on = on;
  snprintf(m.origin, sizeof(m.origin), "%s", origin);
  m.seq = seq;
  m.ts = ts;
  return m;
}

static void testDecode() {
  StateMsg m;

  const char *good = "1|ON|heart-34AB12CD5678|97|1757001234";
  check(protoDecode((const uint8_t *)good, strlen(good), m), "decode v1");
  check(m.valid && m.on && m.seq == 97 && m.ts == 1757001234UL &&
            strcmp(m.origin, "heart-34AB12CD5678") == 0,
        "decode v1 fields");

  // The real hazard: PubSubClient hands over a payload with no terminator, so
  // decoding must rely on the length and nothing else. Deliberately place
  // trailing garbage right after the payload.
  char raw[64];
  memset(raw, 'X', sizeof(raw));
  memcpy(raw, "1|OFF|heart-1|5|0", 17);
  check(protoDecode((const uint8_t *)raw, 17, m), "decode unterminated");
  check(m.valid && !m.on && m.seq == 5 && strcmp(m.origin, "heart-1") == 0,
        "decode unterminated fields");

  const char *legacyOn = "ON";
  check(protoDecode((const uint8_t *)legacyOn, 2, m), "decode legacy ON");
  check(m.ver == 0 && m.on && m.origin[0] == '\0', "legacy has no origin");

  const char *future = "2|ON|heart-1|5|0";
  check(!protoDecode((const uint8_t *)future, strlen(future), m),
        "reject future version");

  const char *junk = "1|MAYBE|heart-1|5|0";
  check(!protoDecode((const uint8_t *)junk, strlen(junk), m),
        "reject bad state word");

  const char *truncated = "1|ON|heart-1";
  check(!protoDecode((const uint8_t *)truncated, strlen(truncated), m),
        "reject missing fields");

  check(!protoDecode((const uint8_t *)"", 0, m), "reject empty");

  // Round trip.
  char buf[PAYLOAD_MAX];
  size_t n = protoEncode(buf, sizeof(buf), true, SELF, 42, NOW);
  check(n > 0 && n < PAYLOAD_MAX, "encode length sane");
  check(protoDecode((const uint8_t *)buf, n, m) && m.on && m.seq == 42 &&
            m.ts == NOW && strcmp(m.origin, SELF) == 0,
        "encode/decode round trip");
}

static void testDecide() {
  // Our own echo must never be applied or republished.
  check(protoDecide(mk(true, SELF, 10, NOW), SELF, 5, false, NOW) ==
            APPLY_IGNORE,
        "own echo ignored");

  // A replayed / stale sequence loses.
  check(protoDecide(mk(true, PEER, 3, NOW), SELF, 9, false, NOW) ==
            APPLY_IGNORE,
        "lower seq ignored");

  // Fresh ON from a peer applies.
  check(protoDecide(mk(true, PEER, 11, NOW), SELF, 9, false, NOW) == APPLY_SET,
        "fresh peer ON applies");

  // OFF applies regardless of age -- turning off is never harmful.
  check(protoDecide(mk(false, PEER, 11, 1), SELF, 9, true, NOW) == APPLY_SET,
        "ancient OFF still applies");

  // The 3am bug: a week-old retained ON must not light the lamp.
  uint32_t weekAgo = NOW - (7UL * 24 * 3600);
  check(protoDecide(mk(true, PEER, 11, weekAgo), SELF, 9, false, NOW) ==
            APPLY_SUPPRESS_STALE,
        "stale ON suppressed");

  // Just inside the window still applies.
  check(protoDecide(mk(true, PEER, 11, NOW - (STALE_ON_WINDOW_S - 60)), SELF, 9,
                    false, NOW) == APPLY_SET,
        "ON just inside window applies");

  // No usable clock on either side: trust the sender rather than sit dark.
  check(protoDecide(mk(true, PEER, 11, weekAgo), SELF, 9, false, 0) == APPLY_SET,
        "unsynced receiver trusts sender");
  check(protoDecide(mk(true, PEER, 11, 0), SELF, 9, false, NOW) == APPLY_SET,
        "unsynced sender trusted");

  // Equal sequence, conflicting state: lower device id wins, deterministically,
  // so both hearts converge with no coordination. PEER sorts below SELF.
  check(protoDecide(mk(true, PEER, 9, NOW), SELF, 9, false, NOW) == APPLY_SET,
        "tie: lower id wins");
  const char *higher = "heart-FFFFFFFFFFFF";
  check(protoDecide(mk(true, higher, 9, NOW), SELF, 9, false, NOW) ==
            APPLY_IGNORE,
        "tie: higher id loses");

  // Equal sequence but no conflict -- nothing to arbitrate.
  check(protoDecide(mk(true, higher, 9, NOW), SELF, 9, true, NOW) == APPLY_SET,
        "tie with agreement applies");

  // Legacy payloads carry no timestamp and cannot be age-gated.
  StateMsg legacy;
  memset(&legacy, 0, sizeof(legacy));
  legacy.valid = true;
  legacy.ver = 0;
  legacy.on = true;
  check(protoDecide(legacy, SELF, 100, false, NOW) == APPLY_SET,
        "legacy ON applies regardless of seq");

  // Garbage in, nothing out.
  StateMsg bad;
  memset(&bad, 0, sizeof(bad));
  bad.valid = false;
  check(protoDecide(bad, SELF, 0, false, NOW) == APPLY_IGNORE,
        "invalid message ignored");
}

int selfTestRun() {
  sFails = 0;
  testDecode();
  testDecide();
  if (sFails == 0)
    Serial.println(F("[selftest] protocol OK"));
  else
    Serial.printf("[selftest] %d FAILURE(S)\n", sFails);
  return sFails;
}

#else
int selfTestRun() { return 0; }
#endif
