// selftest.h -- boot-time assertions for the reconciliation rules.
//
// protoDecide's rules are the subtlest thing in this firmware and the part that
// actually fixes the two headline bugs, but there is no host compiler available
// here to unit-test them off-target. So they run on the device instead: one
// pass at boot, one summary line over serial, and a distinctive blink if
// anything fails.
//
// Set PROTOCOL_SELFTEST to 0 in config.h once you are tired of seeing it.

#pragma once
#include <Arduino.h>

// Returns the number of failures (0 = all good).
int selfTestRun();
