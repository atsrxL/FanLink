#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/build/rx-uart-diagnostic"
mkdir -p "$BUILD_DIR"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -pedantic ${SANITIZER_FLAGS:-} \
  -I"$ROOT/include" "$ROOT/src/common/crc16.cpp" "$ROOT/src/common/protocol.cpp" \
  "$ROOT/src/app/rx_controller.cpp" "$ROOT/tests/rx_uart_diagnostic_tests.cpp" \
  -o "$BUILD_DIR/tests"
"$BUILD_DIR/tests"
