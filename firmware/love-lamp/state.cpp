#include "state.h"

#include <time.h>

#include "store.h"

uint64_t gEfuseMac = 0;
char gDeviceId[24] = {0};
char gDeviceName[24] = {0};
char gPortalSsid[33] = {0};

BrokerConfig gBroker = {0};

QueueHandle_t gIntentQ = nullptr;
volatile NetState gNetState = NET_BOOT;
volatile FailKind gLastFail = FAIL_NONE;
volatile uint32_t gNetHeartbeatMs = 0;
volatile bool gWantPortal = false;
volatile bool gWantFactoryReset = false;
volatile uint8_t gWifiAuthFailStreak = 0;

static SemaphoreHandle_t sMutex = nullptr;
static bool sLightOn = false;
static uint32_t sSeq = 0;

void identityInit() {
  // Read the eFuse directly rather than WiFi.macAddress(): this works before
  // the Wi-Fi driver has started, and the id is needed for the hostname, the
  // portal SSID and the log banner -- all of which precede WiFi.begin().
  // Note the 48 bits come back byte-reversed relative to the sticker on the
  // board. That is irrelevant for uniqueness; do not "fix" it.
  gEfuseMac = ESP.getEfuseMac();

  snprintf(gDeviceId, sizeof(gDeviceId), "heart-%04X%08lX",
           (unsigned int)(uint16_t)(gEfuseMac >> 32),
           (unsigned long)(uint32_t)gEfuseMac);

  const char *name = nullptr;
  const char *ssid = nullptr;
  for (size_t i = 0; i < DEVICE_PROFILE_COUNT; i++) {
    if (DEVICE_PROFILES[i].mac == gEfuseMac) {
      name = DEVICE_PROFILES[i].name;
      ssid = DEVICE_PROFILES[i].portalSsid;
      break;
    }
  }

  if (name) {
    snprintf(gDeviceName, sizeof(gDeviceName), "%s", name);
  } else {
    snprintf(gDeviceName, sizeof(gDeviceName), "lamp-%08lX",
             (unsigned long)(uint32_t)gEfuseMac);
  }

  if (ssid) {
    snprintf(gPortalSsid, sizeof(gPortalSsid), "%s", ssid);
  } else {
    snprintf(gPortalSsid, sizeof(gPortalSsid), "Love lamp %06lX",
             (unsigned long)(uint32_t)(gEfuseMac & 0xFFFFFF));
  }
}

bool brokerConfigured() { return gBroker.host[0] != '\0' && gBroker.port != 0; }

bool stateInit() {
  sMutex = xSemaphoreCreateMutex();
  if (!sMutex) return false;

  gIntentQ = xQueueCreate(4, sizeof(ToggleIntent));
  if (!gIntentQ) return false;

  sSeq = storeSeq();
  sLightOn = false;
  return true;
}

bool logicalOn() {
  if (!sMutex) return sLightOn;
  bool v;
  xSemaphoreTake(sMutex, portMAX_DELAY);
  v = sLightOn;
  xSemaphoreGive(sMutex);
  return v;
}

void logicalSet(bool on) {
  if (!sMutex) {
    sLightOn = on;
    return;
  }
  xSemaphoreTake(sMutex, portMAX_DELAY);
  sLightOn = on;
  xSemaphoreGive(sMutex);
}

uint32_t seqCurrent() {
  if (!sMutex) return sSeq;
  uint32_t v;
  xSemaphoreTake(sMutex, portMAX_DELAY);
  v = sSeq;
  xSemaphoreGive(sMutex);
  return v;
}

uint32_t seqNext() {
  if (!sMutex) return ++sSeq;
  uint32_t v;
  xSemaphoreTake(sMutex, portMAX_DELAY);
  v = ++sSeq;
  xSemaphoreGive(sMutex);
  return v;
}

void seqObserve(uint32_t seen) {
  if (!sMutex) {
    if (seen > sSeq) sSeq = seen;
    return;
  }
  xSemaphoreTake(sMutex, portMAX_DELAY);
  if (seen > sSeq) sSeq = seen;
  xSemaphoreGive(sMutex);
}

uint32_t epochOrZero() {
  time_t now = time(nullptr);
  if ((uint32_t)now < TIME_SYNCED_EPOCH) return 0;
  return (uint32_t)now;
}
