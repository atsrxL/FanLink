#!/usr/bin/env bash
set -euo pipefail

firmware_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ $# != 1 || ! -f "$1/fanlink_protocol.h" || ! -f "$1/fanlink_transport.h" ]]; then
  echo "Usage: $0 /path/to/production/components/fanlink" >&2
  echo "Requires real FanLinkC6 protocol/transport headers; no vendored substitute is used." >&2
  exit 64
fi
c6_component="$(cd "$1" && pwd)"
build_dir="$firmware_root/build/c6-compat"
mkdir -p "$build_dir"
cxx="${CXX:-c++}"
"$cxx" -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
  -I"$firmware_root/include" -I"$c6_component" \
  "$firmware_root/src/common/crc16.cpp" \
  "$firmware_root/src/common/protocol.cpp" \
  "$firmware_root/src/common/serial_protocol.cpp" \
  "$firmware_root/tests/c6_wire_compat_tests.cpp" \
  -o "$build_dir/c6_wire_compat_tests"
"$build_dir/c6_wire_compat_tests"
shasum -a 256 "$c6_component/fanlink_protocol.h" "$c6_component/fanlink_transport.h"
