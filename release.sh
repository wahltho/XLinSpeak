#!/bin/bash
set -euo pipefail

# Create release zip with X-Plane plugin folder structure
# Output: dist/XLinSpeak-linux.zip

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST_DIR="$ROOT_DIR/dist"
STAGE_DIR="$DIST_DIR/XLinSpeak"
ZIP_PATH="$DIST_DIR/XLinSpeak-linux.zip"

rm -rf "$DIST_DIR"
mkdir -p "$STAGE_DIR/lin_x64"

cp -f "$ROOT_DIR/XLinSpeak/lin_x64/XLinSpeak.xpl" "$STAGE_DIR/lin_x64/XLinSpeak.xpl"
cp -f "$ROOT_DIR/README.md" "$STAGE_DIR/README.md"

(cd "$DIST_DIR" && zip -r "$(basename "$ZIP_PATH")" "$(basename "$STAGE_DIR")")

echo "Created: $ZIP_PATH"
