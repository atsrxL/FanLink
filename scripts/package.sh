#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PARENT="$(dirname "$ROOT")"
NAME="$(basename "$ROOT")"
OUTPUT="${1:-$PARENT/${NAME}-reviewed.zip}"
OUTPUT_DIR="$(dirname "$OUTPUT")"
OUTPUT_NAME="$(basename "$OUTPUT")"
TEMP_DIR="$(mktemp -d)"
TEMP_OUTPUT="$TEMP_DIR/$OUTPUT_NAME"
trap 'rm -rf "$TEMP_DIR"' EXIT

"$ROOT/scripts/source_manifest.sh" --check
mkdir -p "$OUTPUT_DIR"
(
  cd "$PARENT"
  zip -qr "$TEMP_OUTPUT" "$NAME" \
    -x '*/.git' '*/.git/*' '*/.pio/*' '*/build/*' '*/dist/*' \
       '*/.venv/*' '*/.vscode/*' '*/.idea/*' \
       '*__pycache__*' '*.pyc' '*/.DS_Store'
)
mv -f "$TEMP_OUTPUT" "$OUTPUT"
(
  cd "$OUTPUT_DIR"
  shasum -a 256 "$OUTPUT_NAME" > "$OUTPUT_NAME.sha256"
  shasum -a 256 -c "$OUTPUT_NAME.sha256"
)
printf 'Created %s\n' "$OUTPUT"
cat "$OUTPUT.sha256"
