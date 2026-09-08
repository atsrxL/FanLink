#!/usr/bin/env bash
set -euo pipefail
RX_TIMING_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RX_TIMING_BUILD="$RX_TIMING_ROOT/build/rx-reply-timing"
mkdir -p "$RX_TIMING_BUILD"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -I"$RX_TIMING_ROOT/include" \
  "$RX_TIMING_ROOT/src/common/crc16.cpp" \
  "$RX_TIMING_ROOT/src/common/protocol.cpp" \
  "$RX_TIMING_ROOT/src/app/rx_controller.cpp" \
  "$RX_TIMING_ROOT/tests/rx_reply_timing_tests.cpp" \
  -o "$RX_TIMING_BUILD/rx_reply_timing_tests"
"$RX_TIMING_BUILD/rx_reply_timing_tests"
