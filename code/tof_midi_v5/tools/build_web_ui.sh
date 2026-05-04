#!/usr/bin/env bash
# Generates web_ui_html.h from web_ui.html.
#
# Strips everything between <!-- BUILD:STRIP_BEGIN --> and
# <!-- BUILD:STRIP_END --> markers (top doc comment, file://-only mock
# script). Wraps the rest in a PROGMEM raw-string for the ESP32 sketch
# to send_P() at /.
#
# Run from anywhere; resolves paths relative to this script.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../web_ui.html"
OUT="$HERE/../web_ui_html.h"

if [[ ! -f "$SRC" ]]; then
    echo "error: $SRC not found" >&2
    exit 1
fi

{
    printf '// Auto-generated from web_ui.html by tools/build_web_ui.sh.\n'
    printf '// PROGMEM keeps the page in flash so heap stays free for WiFi+BLE.\n'
    printf '// Do not edit by hand — re-run the script after changing web_ui.html.\n\n'
    printf '#pragma once\n\n'
    printf 'static const char WEB_UI_HTML[] PROGMEM = R"WEBUI('
    awk '
        /<!-- BUILD:STRIP_BEGIN -->/ { skip=1; next }
        /<!-- BUILD:STRIP_END -->/   { skip=0; next }
        !skip { print }
    ' "$SRC"
    printf ')WEBUI";\n'
} > "$OUT"

bytes=$(wc -c < "$OUT" | tr -d ' ')
echo "Wrote $OUT ($bytes bytes)"
