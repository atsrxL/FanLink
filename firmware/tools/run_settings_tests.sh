#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/build/settings"
mkdir -p "$BUILD_DIR"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -pedantic ${SANITIZER_FLAGS:-} \
  -I"$ROOT/include" -I"$ROOT/tests/stubs" -I"$ROOT/lib/fanlink_platform_esp8266/src" \
  "$ROOT/src/common/crc16.cpp" "$ROOT/src/common/ota_protocol.cpp" \
  "$ROOT/src/app/ota_receiver.cpp" "$ROOT/tests/settings_tests.cpp" \
  -o "$BUILD_DIR/tests"
"$BUILD_DIR/tests"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -pedantic ${SANITIZER_FLAGS:-} \
  -I"$ROOT/tests/runtime_pins_stubs" -I"$ROOT/include" -I"$ROOT/lib/fanlink_platform_esp8266/src" \
  "$ROOT/lib/fanlink_platform_esp8266/src/fanlink/platform/esp8266/fan_output.cpp" \
  "$ROOT/lib/fanlink_platform_esp8266/src/fanlink/platform/esp8266/tachometer.cpp" \
  "$ROOT/tests/runtime_pins_tests.cpp" -o "$BUILD_DIR/runtime-pins-tests"
for settings_mode in 0 1 2 254; do
  for settings_polarity in 0 1; do
    "$BUILD_DIR/runtime-pins-tests" "$settings_mode" "$settings_polarity"
  done
done
