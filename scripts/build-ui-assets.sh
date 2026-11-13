#!/bin/bash
# Regenerates ps3_platform/source/ps3_ui_font.h (UI font atlas) and ps3_platform/assets/ICON0.PNG.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
gcc -O2 -o "$TMP/make_ui_assets" "$ROOT/tools/ui-font/make_ui_assets.c" -lz -lm
"$TMP/make_ui_assets" "$ROOT/third_party/dejavu/DejaVuSans.ttf" "$ROOT/third_party/dejavu/DejaVuSans-Bold.ttf" \
    "$ROOT/ps3_platform/source/ps3_ui_font.h" "$ROOT/ps3_platform/assets/ICON0.PNG"
rm -rf "$TMP"
