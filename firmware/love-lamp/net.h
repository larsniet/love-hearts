// net.h -- connection state machine, owned entirely by netTask on core 0.
//
// Nothing here may be called from loop(). PubSubClient is not thread-safe and
// mqtt.connect() can block for tens of seconds; keeping all of it on core 0 is
// what makes the button stay responsive while the network is broken.

#pragma once
#include <Arduino.h>

// Called from setup() on core 1, before the task exists.
void netInit();

// Task entry point. Never returns.
void netTask(void *arg);

const char *netStateName(uint8_t s);
const char *netFailName(uint8_t f);
