#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT/build/settings-tx"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -pedantic ${FANLINK_TEST_SANITIZERS:-} -I"$ROOT/include" \
  "$ROOT/src/common/crc16.cpp" "$ROOT/src/common/protocol.cpp" \
  "$ROOT/src/app/tx_controller.cpp" "$ROOT/tests/settings_tx_tests.cpp" \
  -o "$ROOT/build/settings-tx/tests"
"$ROOT/build/settings-tx/tests"
