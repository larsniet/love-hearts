// defaults.h -- optional build-time seed values.
//
// secrets.h is gitignored and entirely optional: __has_include means a clean
// clone (and CI) compiles without it. Anything not seeded here must be entered
// once through the Wi-Fi config portal, which is the preferred path anyway --
// portal values land in NVS and survive every later sketch upload.

#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef SECRET_MQTT_HOST
#define SECRET_MQTT_HOST ""
#endif
#ifndef SECRET_MQTT_PORT
#define SECRET_MQTT_PORT 8883
#endif
#ifndef SECRET_MQTT_USER
#define SECRET_MQTT_USER ""
#endif
#ifndef SECRET_MQTT_PASS
#define SECRET_MQTT_PASS ""
#endif
#ifndef SECRET_GROUP_ID
#define SECRET_GROUP_ID "default"
#endif
#ifndef SECRET_PORTAL_PASS
// 8+ chars: an open portal lets a passer-by reconfigure the lamp, and it hands
// out whatever the portal collects -- including the broker credentials.
#define SECRET_PORTAL_PASS "loveheart"
#endif
