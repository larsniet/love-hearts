#!/usr/bin/env bash
# Build and run the protocol self-test on the host.
#
# Compiles the real protocol.cpp and selftest.cpp against a tiny Arduino.h shim,
# so these are the same assertions the device prints at boot -- they cannot drift.
#
#   ./run.sh
#
# Exit status is the number of failing assertions.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
fw="$here/../love-lamp"
out="$here/build"

if ! command -v g++ >/dev/null 2>&1; then
  echo "g++ not found. On Ubuntu/WSL:" >&2
  echo "  sudo apt-get install -y g++" >&2
  exit 127
fi

mkdir -p "$out"

g++ -std=c++17 -Wall -Wextra -Wno-unused-parameter -O1 \
  -I "$here/shim" -I "$fw" \
  "$here/main.cpp" "$fw/protocol.cpp" "$fw/selftest.cpp" \
  -o "$out/protocol_test"

"$out/protocol_test"
