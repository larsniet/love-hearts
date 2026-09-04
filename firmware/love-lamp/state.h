// state.h -- cross-task shared state, device identity, broker config.
//
// loop() (core 1) owns the button and the light. netTask (core 0) owns Wi-Fi,
// TLS and MQTT. They meet only here: one queue for local toggle intents and one
// mutex-guarded logical state. PubSubClient is not thread-safe, so every mqtt.*
// call lives in netTask -- loop() publishes by pushing an intent.

#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "config.h"

enum NetState : uint8_t {
  NET_BOOT,
  NET_WIFI_CONNECTING,
  NET_WIFI_BACKOFF,
  NET_MQTT_CONNECTING,
  NET_MQTT_BACKOFF,
  NET_ONLINE,
  NET_PORTAL,
  NET_RECOVERING,
};

// Kept separate from NetState so the blink vocabulary can tell a wrong Wi-Fi
// password apart from a captive portal apart from a dead broker without a
// serial monitor.
enum FailKind : uint8_t {
  FAIL_NONE,
  FAIL_WIFI_NO_AP,
  FAIL_WIFI_AUTH,
  FAIL_DNS,
  FAIL_CAPTIVE_PORTAL,
  FAIL_HEAP,
  FAIL_TLS,
  FAIL_MQTT,
  FAIL_SUBSCRIBE,
};

struct BrokerConfig {
  char host[64];
  uint16_t port;
  char user[32];
  char pass[48];
  char group[32];
};

// A local button press waiting to become a publish.
struct ToggleIntent {
  bool on;
  uint32_t seq;
  uint32_t ts; // epoch seconds, 0 when the clock is not yet synced
};

// ------------------------------------------------------------ identity

extern uint64_t gEfuseMac;
extern char gDeviceId[24];    // "heart-XXXXXXXXXXXX", the MQTT client id
extern char gDeviceName[24];  // friendly, cosmetic
extern char gPortalSsid[33];

void identityInit();

// ------------------------------------------------------------ config

extern BrokerConfig gBroker;
bool brokerConfigured();

// ------------------------------------------------------------ shared

extern QueueHandle_t gIntentQ;
extern volatile NetState gNetState;
extern volatile FailKind gLastFail;
extern volatile uint32_t gNetHeartbeatMs;
extern volatile bool gWantPortal;
extern volatile bool gWantFactoryReset;
extern volatile uint8_t gWifiAuthFailStreak;

bool stateInit();

// Logical light state. Both tasks read it; writes are serialised.
bool logicalOn();
void logicalSet(bool on);

// Lamport counter: max(seen, ours) + 1, so an offline toggle always outranks
// the retained value it is racing.
uint32_t seqNext();
uint32_t seqCurrent();
void seqObserve(uint32_t seen);

// 0 when SNTP has not set the clock yet.
uint32_t epochOrZero();
