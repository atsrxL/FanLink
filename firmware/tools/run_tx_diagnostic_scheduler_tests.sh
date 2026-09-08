#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/build/tx-diagnostic-scheduler"
mkdir -p "$BUILD_DIR"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -pedantic ${SANITIZER_FLAGS:-} \
  -I"$ROOT/include" "$ROOT/src/common/crc16.cpp" "$ROOT/src/common/protocol.cpp" \
  "$ROOT/src/common/serial_protocol.cpp" \
  "$ROOT/tests/tx_diagnostic_scheduler_tests.cpp" -o "$BUILD_DIR/tests"
"$BUILD_DIR/tests"
