// secrets.example.h -- committed template. Copy to secrets.h and fill in.
//
//   cp secrets.example.h secrets.h
//
// secrets.h is gitignored. This repo is PUBLIC -- never commit real values.
//
// This file is entirely OPTIONAL. love-lamp.ino wraps it in
// #if __has_include("secrets.h"), so a clean clone compiles without it and CI
// can verify the sketch. Anything left undefined simply has no build-time seed
// and must be provisioned through the Wi-Fi config portal instead (hold the
// button 5 s), which is the preferred path -- values entered there land in NVS
// and survive every subsequent sketch upload.
//
// Certificates are NOT secrets and do not belong here. The firmware validates
// the broker against the Mozilla root bundle already compiled into the ESP32
// core, so there is no PEM to paste and nothing to renew. That permanently
// retires the inline-certificate-with-a-hidden-expiry pattern that this
// project was previously bitten by.

#pragma once

// Broker. Host must be a DNS name, not a bare IP -- a hostname is what stops a
// changed IP from bricking every lamp until it is reflashed.
#define SECRET_MQTT_HOST "your-cluster.s1.eu.hivemq.cloud"
#define SECRET_MQTT_PORT 8883
#define SECRET_MQTT_USER "hearts"
#define SECRET_MQTT_PASS "generate-a-random-one"

// Shared by every heart in a group; forms the topic namespace.
#define SECRET_GROUP_ID "default"

// WPA2 password for the config portal AP, 8+ chars. An open portal lets a
// neighbour reconfigure the lamp and hands out whatever the portal collects.
#define SECRET_PORTAL_PASS "loveheart"
