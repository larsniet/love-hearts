// Minimal Arduino.h so the pure-logic firmware sources compile on the host.
//
// This exists so protocol.cpp and selftest.cpp -- the reconciliation rules that
// fix the two headline bugs -- can be exercised in a second on a PC instead of
// only on a flashed board. The SAME selftest.cpp runs in both places, so the
// host run and the device banner cannot drift apart.
//
// Only what those two files actually touch is stubbed. Anything that needs real
// hardware (net.cpp, lamp.cpp, store.cpp) is deliberately not host-buildable.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define F(x) (x)

struct HostSerial {
  template <typename... Args>
  void printf(const char *fmt, Args... args) {
    std::printf(fmt, args...);
  }
  void printf(const char *fmt) { std::fputs(fmt, stdout); }
  void println(const char *s) { std::printf("%s\n", s); }
  void println() { std::printf("\n"); }
  void print(const char *s) { std::fputs(s, stdout); }
};

extern HostSerial Serial;
