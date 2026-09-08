#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT/build/ota-integration"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -pedantic -I"$ROOT/include" \
  "$ROOT/src/common/crc16.cpp" "$ROOT/src/common/protocol.cpp" \
  "$ROOT/src/common/ota_protocol.cpp" "$ROOT/src/app/ota_receiver.cpp" \
  "$ROOT/src/app/tx_controller.cpp" "$ROOT/src/app/rx_controller.cpp" \
  "$ROOT/src/app/ota_tx_bridge.cpp" "$ROOT/tests/ota_integration_tests.cpp" \
  -o "$ROOT/build/ota-integration/tests"
"$ROOT/build/ota-integration/tests"
