#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MANIFEST="$ROOT/SOURCE_MANIFEST.sha256"
TEMP_MANIFEST="$(mktemp)"
trap 'rm -f "$TEMP_MANIFEST"' EXIT

(
  cd "$ROOT"
  find . -type d \( \
      -name .git -o -name .pio -o -name build -o -name dist \
      -o -name __pycache__ -o -name .venv -o -name .vscode -o -name .idea \
    \) -prune -o -type f \
    ! -name '.git' \
    ! -name '*.pyc' \
    ! -name '.DS_Store' \
    ! -name 'SOURCE_MANIFEST.sha256' \
    -print | LC_ALL=C sort | while IFS= read -r file; do
      shasum -a 256 "$file"
    done
) > "$TEMP_MANIFEST"

case "${1:---check}" in
  --check)
    diff -u "$MANIFEST" "$TEMP_MANIFEST"
    ;;
  --write)
    mv "$TEMP_MANIFEST" "$MANIFEST"
    ;;
  *)
    printf 'usage: %s [--check|--write]\n' "$0" >&2
    exit 2
    ;;
esac
