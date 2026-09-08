#!/usr/bin/env bash
set -euo pipefail

FIRMWARE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RECOVERY_BUILD_DIR="$FIRMWARE_ROOT/build/native"
mkdir -p "$RECOVERY_BUILD_DIR"

"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
  -I"$FIRMWARE_ROOT/include" \
  "$FIRMWARE_ROOT/src/common/crc16.cpp" \
  "$FIRMWARE_ROOT/src/common/protocol.cpp" \
  "$FIRMWARE_ROOT/src/app/tx_controller.cpp" \
  "$FIRMWARE_ROOT/src/app/rx_controller.cpp" \
  "$FIRMWARE_ROOT/tests/runtime_recovery_tests.cpp" \
  -o "$RECOVERY_BUILD_DIR/runtime_recovery_tests"

"$RECOVERY_BUILD_DIR/runtime_recovery_tests"
