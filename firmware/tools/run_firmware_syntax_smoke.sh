#!/usr/bin/env bash
# Host-side syntax smoke using small API stubs. This does not replace a real
# PlatformIO build, link, flash, RF test, or oscilloscope validation.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-g++}"
COMMON=(
  -std=c++17 -fsyntax-only -Wall -Wextra -Wpedantic -Werror
  -DESP8266=1 -DF_CPU=80000000UL
  -I"$ROOT/tests/ota_storage_stubs"
  -I"$ROOT/tests/stubs"
  -I"$ROOT/include"
  -I"$ROOT/lib/fanlink_platform_esp8266/src"
)

PLATFORM_SOURCES=(
  "$ROOT/lib/fanlink_platform_esp8266/src/fanlink/platform/esp8266/ota_storage.cpp"
  "$ROOT/lib/fanlink_platform_esp8266/src/fanlink/platform/esp8266/fan_output.cpp"
  "$ROOT/lib/fanlink_platform_esp8266/src/fanlink/platform/esp8266/tachometer.cpp"
  "$ROOT/lib/fanlink_platform_esp8266/src/fanlink/platform/esp8266/radiolib_sx1280_port.cpp"
  "$ROOT/lib/fanlink_platform_esp8266/src/fanlink/platform/esp8266/status_led.cpp"
)

"$CXX" "${COMMON[@]}" "${PLATFORM_SOURCES[@]}"
"$CXX" "${COMMON[@]}" -DFANLINK_BUILD_TX=1 "$ROOT/src/main_tx.cpp"
"$CXX" "${COMMON[@]}" -DFANLINK_BUILD_RX=1 -DFANLINK_NODE_ID=1 "$ROOT/src/main_rx.cpp"
"$CXX" "${COMMON[@]}" -DFANLINK_BUILD_RX=1 -DFANLINK_NODE_ID=2 -DFANLINK_RX_UART_DIAGNOSTIC=1 "$ROOT/src/main_rx.cpp" "$ROOT/src/main_rx_diag.cpp"

echo "PASS: firmware syntax smoke (stub APIs only)"
