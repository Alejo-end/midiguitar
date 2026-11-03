#!/usr/bin/env python3
"""Quick analysis of finalMidiGuitar.kicad_pcb — board outline, footprint placements,
unrouted ratlines, layer usage, and rough density check."""
import re
import sys
from pathlib import Path

# Default: the board in this repo. Pass another path as the first argument.
REPO = Path(__file__).resolve().parents[2]
PCB = Path(sys.argv[1]) if len(sys.argv) > 1 else REPO / 'finalMidiGuitar' / 'finalMidiGuitar.kicad_pcb'
content = PCB.read_text()

# Board edge cuts (Edge.Cuts layer) — find min/max XY of all "Edge.Cuts" graphic items
# Each gr_* item carries nested (stroke ...) in KiCad 7+, so match up to its
# own (layer ...) and pull the coordinates out of that span.
xs, ys = [], []
for item in re.finditer(r'\(gr_(?:line|rect|arc|circle)\b(.*?)\(layer "([^"]+)"\)', content, re.DOTALL):
    if item.group(2) != 'Edge.Cuts':
        continue
    for x, y in re.findall(r'\((?:start|mid|end|center) ([\-\d.]+) ([\-\d.]+)\)', item.group(1)):
        xs.append(float(x)); ys.append(float(y))
if xs:
    print(f"Board outline: X {min(xs):.2f}–{max(xs):.2f}  Y {min(ys):.2f}–{max(ys):.2f}")
    print(f"Board size:    {max(xs)-min(xs):.2f} mm × {max(ys)-min(ys):.2f} mm")
else:
    print("No Edge.Cuts geometry found")

# Footprints — name, position, layer
print("\n=== FOOTPRINTS ===")
fp_re = re.compile(r'\(footprint "([^"]+)"[^()]*?\(layer "([^"]+)"\)[^()]*?\(at ([\-\d.]+) ([\-\d.]+)(?: ([\-\d.]+))?\)', re.DOTALL)
fps = fp_re.findall(content[:60000])
ref_re = re.compile(r'\(property "Reference" "([^"]+)"', re.DOTALL)
# Fallback: find footprint blocks with their references
fp_blocks = re.split(r'\n\t\(footprint ', content)
print(f"Footprint blocks found: {len(fp_blocks) - 1}")
for i, block in enumerate(fp_blocks[1:], 1):
    name_m = re.match(r'"([^"]+)"', block)
    layer_m = re.search(r'\(layer "([^"]+)"\)', block)
    at_m = re.search(r'\(at ([\-\d.]+) ([\-\d.]+)(?: ([\-\d.]+))?\)', block)
    ref_m = ref_re.search(block[:2000])
    if name_m and at_m:
        ref = ref_m.group(1) if ref_m else "?"
        layer = layer_m.group(1) if layer_m else "?"
        rot = at_m.group(3) or "0"
        print(f"  {ref:6} {name_m.group(1):60} layer={layer:8} at=({at_m.group(1)}, {at_m.group(2)}) rot={rot}")

# Tracks (segments on copper layers)
seg_re = re.compile(r'\(segment[^()]*?\(start ([\-\d.]+) ([\-\d.]+)\)[^()]*?\(end ([\-\d.]+) ([\-\d.]+)\)[^()]*?\(layer "([^"]+)"\)[^()]*?\(net (\d+)\)', re.DOTALL)
segments = seg_re.findall(content)
print(f"\n=== TRACKS ===")
print(f"Total routed segments: {len(segments)}")
by_layer = {}
by_net = {}
for x1, y1, x2, y2, layer, net in segments:
    by_layer[layer] = by_layer.get(layer, 0) + 1
    by_net[int(net)] = by_net.get(int(net), 0) + 1
for layer, n in by_layer.items():
    print(f"  {layer}: {n} segments")

# Vias
via_re = re.compile(r'\(via[^()]*?\(at ([\-\d.]+) ([\-\d.]+)\)', re.DOTALL)
vias = via_re.findall(content)
print(f"\n=== VIAS ===\nTotal: {len(vias)}")

# Zones / fills
zone_re = re.compile(r'\(zone[^()]*?\(net (\d+)\)[^()]*?\(net_name "([^"]*)"\)[^()]*?\(layer "([^"]+)"\)', re.DOTALL)
zones = zone_re.findall(content)
print(f"\n=== ZONES / COPPER POURS ===")
print(f"Total: {len(zones)}")
for net, name, layer in zones:
    print(f"  net={name or '<unnamed>'} layer={layer}")

# Net list (count nets)
net_re = re.compile(r'\(net (\d+) "([^"]*)"\)')
nets = net_re.findall(content)
print(f"\n=== NETS ===")
print(f"Total nets defined: {len(nets)}")
unrouted_nets = []
for n_id, n_name in nets:
    if not n_name:
        continue
    if int(n_id) not in by_net:
        unrouted_nets.append(n_name)
print(f"Nets with NO routed segments: {len(unrouted_nets)}")
for n in unrouted_nets[:30]:
    print(f"  {n}")
