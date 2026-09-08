#!/usr/bin/env bash
set -euo pipefail

fan_output_firmware_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fan_output_test_dir="$(mktemp -d "${TMPDIR:-/tmp}/fanlink-fan-output.XXXXXX")"
trap 'rm -f "$fan_output_test_dir/fan_output_tests"; rmdir "$fan_output_test_dir"' EXIT

"${CXX:-c++}" \
  -std=c++17 -Wall -Wextra -Werror -pedantic \
  -I"$fan_output_firmware_root/tests/fan_output_stubs" \
  -I"$fan_output_firmware_root/include" \
  -I"$fan_output_firmware_root/lib/fanlink_platform_esp8266/src" \
  "$fan_output_firmware_root/lib/fanlink_platform_esp8266/src/fanlink/platform/esp8266/fan_output.cpp" \
  "$fan_output_firmware_root/tests/fan_output_tests.cpp" \
  -o "$fan_output_test_dir/fan_output_tests"

"$fan_output_test_dir/fan_output_tests"
