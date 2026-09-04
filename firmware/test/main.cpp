// Host runner for the protocol self-test.
//
//   firmware/test/run.sh
//
// Exit status is the number of failures, so this drops straight into CI.

#include <Arduino.h>

#include "selftest.h"

HostSerial Serial;

int main() {
  std::printf("love-lamp protocol self-test (host)\n");
  int fails = selfTestRun();
  if (fails == 0) {
    std::printf("PASS\n");
    return 0;
  }
  std::printf("FAIL: %d\n", fails);
  return fails;
}
