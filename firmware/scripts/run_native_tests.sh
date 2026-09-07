#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/build/native"
mkdir -p "$BUILD_DIR"

CXX="${CXX:-g++}"
"$CXX" \
  -std=c++17 \
  -Wall -Wextra -Werror -pedantic \
  -I"$ROOT/include" \
  "$ROOT/src/common/crc16.cpp" \
  "$ROOT/src/common/protocol.cpp" \
  "$ROOT/src/common/serial_protocol.cpp" \
  "$ROOT/src/app/tx_controller.cpp" \
  "$ROOT/src/app/rx_controller.cpp" \
  "$ROOT/tests/native_tests.cpp" \
  -o "$BUILD_DIR/native_tests"

"$BUILD_DIR/native_tests"
