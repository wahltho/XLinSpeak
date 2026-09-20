#!/bin/bash
set -euo pipefail

# Package the existing binary and create its MTK release manifest. No binary build.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST_DIR="$ROOT_DIR/dist"
VERSION="$(python3 - "$ROOT_DIR/src/version.h" <<'PY'
import re, sys
from pathlib import Path
source = Path(sys.argv[1]).read_text()
print('.'.join(re.search(r'#define XLINSPEAK_VERSION_' + part + r'\s+(\d+)', source).group(1)
               for part in ['MAJOR', 'MINOR', 'PATCH']))
PY
)"
mkdir -p "$DIST_DIR"
STAGE_DIR="$(mktemp -d "$DIST_DIR/package.XXXXXX")"
trap 'rm -rf "$STAGE_DIR"' EXIT
ZIP_NAME="XLinSpeak-linux.$VERSION.zip"
mkdir -p "$STAGE_DIR/XLinSpeak/lin_x64"
cp "$ROOT_DIR/XLinSpeak/lin_x64/XLinSpeak.xpl" "$STAGE_DIR/XLinSpeak/lin_x64/XLinSpeak.xpl"
cp "$ROOT_DIR/README.md" "$STAGE_DIR/XLinSpeak/README.md"
(cd "$STAGE_DIR" && zip -qr "$ZIP_NAME" XLinSpeak)
python3 "$ROOT_DIR/tools/create_mtk_manifest.py" "$STAGE_DIR/$ZIP_NAME" --version "$VERSION"
mv "$STAGE_DIR/$ZIP_NAME" "$DIST_DIR/$ZIP_NAME"
mv "$STAGE_DIR/XLinSpeak-$VERSION-manifest.json" "$DIST_DIR/XLinSpeak-$VERSION-manifest.json"
echo "Created archive and MTK manifest in $DIST_DIR"
