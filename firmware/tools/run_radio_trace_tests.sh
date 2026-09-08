#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d -t fanlink-radio-trace.XXXXXX)"
trap 'rm -f "$work/test"; rmdir "$work"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$root/include" "$root/tests/radio_trace_tests.cpp" -o "$work/test"
"$work/test"
