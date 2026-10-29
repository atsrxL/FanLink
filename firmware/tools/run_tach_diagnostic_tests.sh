#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT/build/tach-diagnostic"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -pedantic ${FANLINK_TEST_SANITIZERS:-} -I"$ROOT/include" \
  "$ROOT/src/common/crc16.cpp" "$ROOT/src/common/protocol.cpp" \
  "$ROOT/src/app/tx_controller.cpp" "$ROOT/src/app/rx_controller.cpp" \
  "$ROOT/tests/tach_diagnostic_tests.cpp" -o "$ROOT/build/tach-diagnostic/tests"
"$ROOT/build/tach-diagnostic/tests"
