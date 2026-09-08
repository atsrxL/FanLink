#!/usr/bin/env bash
set -euo pipefail
FIRMWARE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OTA_BUILD_DIR="$FIRMWARE_ROOT/build/native"
mkdir -p "$OTA_BUILD_DIR"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
  -I"$FIRMWARE_ROOT/include" \
  "$FIRMWARE_ROOT/src/common/crc16.cpp" \
  "$FIRMWARE_ROOT/src/common/ota_protocol.cpp" \
  "$FIRMWARE_ROOT/src/app/ota_receiver.cpp" \
  "$FIRMWARE_ROOT/tests/ota_receiver_tests.cpp" \
  -o "$OTA_BUILD_DIR/ota_receiver_tests"
"$OTA_BUILD_DIR/ota_receiver_tests"
