// config.h -- committed. Pins, timings and topic layout.
//
// Nothing in this file differs between boards. Per-board identity is derived
// from the chip's eFuse MAC at runtime (see makeDeviceId in state.cpp), which
// is what makes the old duplicate-client-id bug impossible rather than merely
// fixed. Broker credentials live in NVS, never here -- see secrets.example.h.

#pragma once
#include <stdint.h>

// ---------------------------------------------------------------- hardware

// GPIO12 is a strapping pin (MTDI, selects flash voltage at reset). It has an
// internal pull-down at reset and the button shorts to GND, so it reads LOW
// both idle and pressed and holding it through power-up is safe.
// NEVER add an external pull-up on GPIO12 -- the board will not boot.
// For a NEW build prefer GPIO27; this stays on 12 to match existing wiring.
static const uint8_t PIN_BUTTON = 12;

// GPIO5 is also a strapping pin with an internal pull-UP, so it glitches
// briefly during boot and the lamp flickers on every reset. Accepted
// deliberately (no rewiring); the recovery ladder in net.cpp is tuned to make
// reboots rare because of it. GPIO32 is the clean choice for a new build.
static const uint8_t PIN_LIGHT = 5;

static const bool BUTTON_ACTIVE_LOW = true;

// ---------------------------------------------------------------- identity

// Cosmetic only. client_id is ALWAYS derived from the MAC, never from here, so
// the firmware is correct with this table empty. Populate it after first flash
// by reading the MAC from the boot banner, purely to get a friendly AP name.
struct DeviceProfile {
  uint64_t mac;
  const char *name;
  const char *portalSsid;
};

static const DeviceProfile DEVICE_PROFILES[] = {
    // { 0x0000AABBCCDDEEFFULL, "lulu", "Lulus heart" },
    // { 0x0000112233445566ULL, "lala", "Lalas heart" },
};
static const size_t DEVICE_PROFILE_COUNT =
    sizeof(DEVICE_PROFILES) / sizeof(DEVICE_PROFILES[0]);

// ---------------------------------------------------------------- protocol

#define TOPIC_ROOT "lovehearts/v1"
#define DEFAULT_GROUP_ID "default"
static const uint8_t PROTO_VERSION = 1;

// Longest line we will ever emit is well under this; see protocol.cpp.
static const size_t PAYLOAD_MAX = 80;

// ---------------------------------------------------------------- timings

static const uint32_t BUTTON_DEBOUNCE_MS = 25;
static const uint32_t BOOT_INPUT_LOCKOUT_MS = 500;

// Gesture thresholds, acted on RELEASE. The lamp blips as each is crossed so
// the user can count and let go, and >= ABORT does nothing at all.
static const uint32_t HOLD_TOGGLE_MAX_MS = 600;
static const uint32_t HOLD_PAIR_MS = 2000;   // Stage B; inert for now
static const uint32_t HOLD_PORTAL_MS = 5000; // preserved: existing muscle memory
static const uint32_t HOLD_FACTORY_MS = 11000;
static const uint32_t HOLD_ABORT_MS = 20000;
static const uint32_t BLIP_MS = 60;

static const uint32_t WIFI_JOIN_TIMEOUT_MS = 20000;
static const uint32_t WIFI_BACKOFF_BASE_MS = 1000;
static const uint32_t WIFI_BACKOFF_CAP_MS = 60000;
static const uint32_t MQTT_BACKOFF_BASE_MS = 2000;
static const uint32_t MQTT_BACKOFF_CAP_MS = 300000; // 5 min worst-case recovery

// Recovery ladder, measured from the last successful MQTT session. Deliberately
// reboot-shy: GPIO5 flickers on reset, so the two radio re-inits do the work and
// ESP.restart() is a genuine last resort.
static const uint32_t LADDER_REARM_STA_MS = 120000;   // 2 min
static const uint32_t LADDER_RADIO_CYCLE_MS = 600000; // 10 min
static const uint32_t LADDER_RADIO_AGAIN_MS = 1500000;// 25 min
static const uint32_t LADDER_RESTART_MS = 2700000;    // 45 min
static const uint32_t LADDER_RESTART_SLOW_MS = 21600000; // 6 h after 3 cycles
static const uint8_t LADDER_MAX_CYCLES = 3;

static const uint32_t NET_HEARTBEAT_TIMEOUT_MS = 120000;
static const uint32_t PORTAL_TIMEOUT_S = 300;

static const uint16_t MQTT_KEEPALIVE_S = 30;
static const uint16_t MQTT_SOCKET_TIMEOUT_S = 8;
static const uint16_t MQTT_BUFFER_BYTES = 512;
static const uint32_t TCP_CONNECT_TIMEOUT_MS = 8000;
static const uint32_t TLS_HANDSHAKE_TIMEOUT_S = 10;

// Proves publish + subscribe + ACL in one message: we subscribe to the status
// wildcard, publish our own status retained, and require the echo back.
static const uint32_t SUB_PROBE_TIMEOUT_MS = 5000;

// A local publish is QoS 0, so we use the broker's echo as an ACK.
static const uint32_t ECHO_ACK_TIMEOUT_MS = 3000;
static const uint8_t ECHO_ACK_MAX_TRIES = 3;

// An ON older than this is not applied -- this is the "lamp lights itself at
// 3am after a week offline" fix. OFF is never gated.
static const uint32_t STALE_ON_WINDOW_S = 6 * 3600;

// The heart that owns the current ON state refreshes it so a held ON never
// decays, while an abandoned one does.
static const uint32_t STATE_REFRESH_MS = 300000; // 5 min
static const uint32_t TELEMETRY_MS = 300000;

static const uint32_t NVS_WRITE_DEBOUNCE_MS = 5000;

// Fragmentation, not exhaustion, is the real TLS failure mode: two static 16 KB
// buffers must come from contiguous heap. Watch largest-free-block.
static const uint32_t MIN_MAX_ALLOC_BYTES = 45000;
static const uint32_t MIN_FREE_HEAP_BYTES = 40000;
static const uint8_t HEAP_STRIKES_BEFORE_RESTART = 3;

// Below this, time() has not been set by SNTP yet and timestamps are unusable.
static const uint32_t TIME_SYNCED_EPOCH = 1700000000UL;
