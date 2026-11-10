#!/bin/sh
# DRC on a temp copy with refilled zones. Usage: ./drc.sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
T="$(mktemp -d)"
cp "$HERE/reins-ring.kicad_pcb" "$HERE/reins-ring.kicad_pro" "$T/"
/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli pcb drc --refill-zones --save-board --severity-all --format report --units mm -o "$T/drc.rpt" "$T/reins-ring.kicad_pcb" >/dev/null
cp "$T/reins-ring.kicad_pcb" "$HERE/.filled.kicad_pcb"
cp "$T/drc.rpt" "$HERE/drc.rpt"
sed -n 1,400p "$HERE/drc.rpt" | grep -E "^\*\* |^\[" | sort | uniq -c | sort -rn | head -30
