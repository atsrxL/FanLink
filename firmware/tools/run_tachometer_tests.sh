#!/usr/bin/env bash
set -euo pipefail
tach_firmware_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tach_test_dir="$(mktemp -d "${TMPDIR:-/tmp}/fanlink-tach-tests.XXXXXX")"
trap 'rm -f "$tach_test_dir/tachometer_tests"; rmdir "$tach_test_dir"' EXIT
tach_flags=(-O0)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  tach_flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic "${tach_flags[@]}" \
  -I"$tach_firmware_root/tests/tachometer_stubs" \
  -I"$tach_firmware_root/include" \
  -I"$tach_firmware_root/lib/fanlink_platform_esp8266/src" \
  "$tach_firmware_root/lib/fanlink_platform_esp8266/src/fanlink/platform/esp8266/tachometer.cpp" \
  "$tach_firmware_root/tests/tachometer_tests.cpp" \
  -o "$tach_test_dir/tachometer_tests"
# Fresh process per scenario: production supports exactly one attached tach ISR.
tach_failed=0
for tach_case in stable1400 stable1500 isolated500 isolated5000 burst missing step stop noise_age restart zero wrap stalled_loop acquisition queued_stale queue_capacity max_speed config recurring_noise; do
  if ! "$tach_test_dir/tachometer_tests" "$tach_case"; then tach_failed=1; fi
done
exit "$tach_failed"
