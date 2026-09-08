#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT/build/tx-local"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -pedantic ${FANLINK_TEST_SANITIZERS:-} -I"$ROOT/include" \
  "$ROOT/src/common/crc16.cpp" "$ROOT/src/common/protocol.cpp" \
  "$ROOT/src/common/ota_protocol.cpp" "$ROOT/src/app/ota_receiver.cpp" \
  "$ROOT/src/app/tx_controller.cpp" "$ROOT/src/app/ota_tx_local.cpp" \
  "$ROOT/tests/tx_local_tests.cpp" -o "$ROOT/build/tx-local/tests"
"$ROOT/build/tx-local/tests"
