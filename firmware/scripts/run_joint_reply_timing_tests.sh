#!/usr/bin/env bash
set -euo pipefail
JOINT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JOINT_BUILD="$JOINT_ROOT/build/joint-reply-timing"
mkdir -p "$JOINT_BUILD"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -I"$JOINT_ROOT/include" \
  "$JOINT_ROOT/src/common/crc16.cpp" "$JOINT_ROOT/src/common/protocol.cpp" \
  "$JOINT_ROOT/src/app/tx_controller.cpp" "$JOINT_ROOT/src/app/rx_controller.cpp" \
  "$JOINT_ROOT/tests/joint_reply_timing_tests.cpp" \
  -o "$JOINT_BUILD/joint_reply_timing_tests"
"$JOINT_BUILD/joint_reply_timing_tests"
