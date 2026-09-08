#!/usr/bin/env bash
set -euo pipefail
ota_storage_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ota_storage_test_dir="$(mktemp -d "${TMPDIR:-/tmp}/fanlink-ota-storage-tests.XXXXXX")"
trap 'rm -f "$ota_storage_test_dir/ota_storage_tests"; rmdir "$ota_storage_test_dir"' EXIT
ota_storage_flags=(-O0)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  ota_storage_flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic "${ota_storage_flags[@]}" \
  -I"$ota_storage_root/tests/ota_storage_stubs" \
  -I"$ota_storage_root/include" \
  -I"$ota_storage_root/lib/fanlink_platform_esp8266/src" \
  "$ota_storage_root/lib/fanlink_platform_esp8266/src/fanlink/platform/esp8266/ota_storage.cpp" \
  "$ota_storage_root/tests/ota_storage_tests.cpp" \
  -o "$ota_storage_test_dir/ota_storage_tests"
"$ota_storage_test_dir/ota_storage_tests"
