#include "protocol.h"

#include <string.h>

static bool parseState(const char *s, bool &on) {
  if (strcmp(s, "ON") == 0) {
    on = true;
    return true;
  }
  if (strcmp(s, "OFF") == 0) {
    on = false;
    return true;
  }
  return false;
}

bool protoDecode(const uint8_t *payload, unsigned int len, StateMsg &out) {
  memset(&out, 0, sizeof(out));
  if (len == 0 || len >= PAYLOAD_MAX) return false;

  char buf[PAYLOAD_MAX];
  memcpy(buf, payload, len);
  buf[len] = '\0';

  // Legacy v0: the whole payload is the state.
  if (parseState(buf, out.on)) {
    out.valid = true;
    out.ver = 0;
    out.origin[0] = '\0';
    out.seq = 0;
    out.ts = 0;
    return true;
  }

  char *save = nullptr;
  char *fVer = strtok_r(buf, "|", &save);
  char *fState = strtok_r(nullptr, "|", &save);
  char *fOrigin = strtok_r(nullptr, "|", &save);
  char *fSeq = strtok_r(nullptr, "|", &save);
  char *fTs = strtok_r(nullptr, "|", &save);
  if (!fVer || !fState || !fOrigin || !fSeq || !fTs) return false;

  long ver = strtol(fVer, nullptr, 10);
  if (ver != PROTO_VERSION) return false; // never guess at a future schema
  if (!parseState(fState, out.on)) return false;

  out.valid = true;
  out.ver = (uint8_t)ver;
  snprintf(out.origin, sizeof(out.origin), "%s", fOrigin);
  out.seq = (uint32_t)strtoul(fSeq, nullptr, 10);
  out.ts = (uint32_t)strtoul(fTs, nullptr, 10);
  return true;
}

size_t protoEncode(char *buf, size_t cap, bool on, const char *origin,
                   uint32_t seq, uint32_t ts) {
  int n = snprintf(buf, cap, "%u|%s|%s|%lu|%lu", (unsigned)PROTO_VERSION,
                   on ? "ON" : "OFF", origin, (unsigned long)seq,
                   (unsigned long)ts);
  if (n < 0) return 0;
  return (size_t)n < cap ? (size_t)n : cap - 1;
}

ApplyAction protoDecide(const StateMsg &m, const char *selfId, uint32_t lastSeq,
                        bool currentOn, uint32_t nowEpoch) {
  if (!m.valid) return APPLY_IGNORE;

  // 1. Our own echo. Never apply, never republish -- the caller uses it only to
  //    clear the pending publish and to confirm the round trip.
  if (m.ver > 0 && m.origin[0] && strcmp(m.origin, selfId) == 0)
    return APPLY_IGNORE;

  if (m.ver > 0) {
    // 2. Replay or stale retained value.
    if (m.seq < lastSeq) return APPLY_IGNORE;

    // 3. Two hearts toggled during a shared outage and landed on the same
    //    sequence. Lower device id wins, deterministically, so both converge
    //    without any coordination.
    if (m.seq == lastSeq && m.on != currentOn) {
      if (strcmp(m.origin, selfId) >= 0) return APPLY_IGNORE;
    }
  }

  // 4. OFF is never harmful and is never age-gated.
  if (!m.on) return APPLY_SET;

  // 5. An ON has to be recent enough to honour. This is what stops a week-old
  //    retained ON from lighting the lamp at 3am. Every heart reaches the same
  //    conclusion independently, so they stay consistent.
  if (m.ver == 0) return APPLY_SET; // legacy, no timestamp to judge
  if (nowEpoch == 0) return APPLY_SET; // our clock is unset; trust the sender
  if (m.ts == 0) return APPLY_SET;     // sender's clock was unset
  if (nowEpoch >= m.ts && (nowEpoch - m.ts) > STALE_ON_WINDOW_S)
    return APPLY_SUPPRESS_STALE;

  return APPLY_SET;
}

const char *protoActionName(ApplyAction a) {
  switch (a) {
    case APPLY_SET: return "set";
    case APPLY_SUPPRESS_STALE: return "stale-on-suppressed";
    default: return "ignore";
  }
}
