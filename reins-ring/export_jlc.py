# Export the JLCPCB order files for the REINS LED ring.
#
#   <KiCad python> export_jlc.py            (writes fab/jlc/reins-ring/)
#   (KiCad python: /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3)
#
#   reins-ring-gerbers.zip   Gerbers and drill files: upload this as the PCB
#   reins-ring-bom.csv       Comment, Designator, Footprint, LCSC: the parts JLCPCB assembles
#   reins-ring-cpl.csv       Designator, Mid X, Mid Y, Layer, Rotation: where they go
#   reins-ring-top-bom.csv, reins-ring-top-cpl.csv   the same for the top side only (Economic assembly)
#
# LED and cap rotation: JLCPCB's part rotation can differ from KiCad's. ROT holds a correction per LCSC
# part, in degrees. The values are 0 because nobody has checked them yet. After the upload, compare the
# LED pin 1 in JLCPCB's placement preview with the drawing in README.md. If every LED is turned the
# same wrong way, put the correction here (for example 180 or 90) and export again.
# A bottom side angle is mirrored first, like the kicad-jlcpcb-tools plugin: (180 - R + c) mod 360.
import csv, os, shutil, subprocess, zipfile
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
CLI = '/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli'
LAYERS = 'F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts'
ROT = {'C114586': 0, 'C52917433': 0, 'C14663': 0}
# LCSC parts that JLCPCB no longer offers, and the part that replaces them in the order files.
# C114586 (WS2812B-B/W) is not in the JLCPCB assembly library on 2026-11-21. C52917433 is the Worldsemi
# WS2812B-V6 in the same SMD5050-4P package. The WS2812B pin order is 1 VDD, 2 DOUT, 3 VSS, 4 DIN: confirm it in the V6 data sheet
# on the parts page before you approve.
SUBST = {'C114586': 'C52917433'}
# JLCPCB Economic assembly places one side only. The -top files hold the 16 LEDs; you solder the
# 16 bottom caps by hand. The full files (both sides) need Standard assembly.
board = 'reins-ring'


def run(*args):
    subprocess.run([CLI] + list(args), check=True, stdout=subprocess.DEVNULL)


pcb = os.path.join(HERE, board + '.kicad_pcb')
out = os.path.join(HERE, 'fab', 'jlc', board)
tmp = os.path.join(out, 'gerbers')
shutil.rmtree(out, ignore_errors=True)
os.makedirs(tmp)
run('pcb', 'export', 'gerbers', '--layers', LAYERS, '--subtract-soldermask', '-o', tmp + '/', pcb)
run('pcb', 'export', 'drill', '--format', 'excellon', '--drill-origin', 'absolute', '--excellon-units', 'mm',
    '--generate-map', '--map-format', 'gerberx2', '-o', tmp + '/', pcb)
with zipfile.ZipFile(os.path.join(out, board + '-gerbers.zip'), 'w', zipfile.ZIP_DEFLATED) as z:
    for f in sorted(os.listdir(tmp)):
        z.write(os.path.join(tmp, f), f)

b = pcbnew.LoadBoard(pcb)
groups, rows = {}, []
for fp in b.GetFootprints():
    if not fp.HasField('LCSC') or not fp.GetFieldText('LCSC'):
        continue
    lcsc = SUBST.get(fp.GetFieldText('LCSC'), fp.GetFieldText('LCSC'))
    key = (fp.GetValue(), fp.GetFPID().GetLibItemName().wx_str(), lcsc)
    groups.setdefault(key, []).append((fp.GetReference(), fp.IsFlipped()))
    pos = fp.GetPosition()
    bottom = fp.IsFlipped()
    rot = fp.GetOrientationDegrees()
    c = ROT.get(lcsc, 0)
    jrot = (180 - rot + c) % 360 if bottom else (rot + c) % 360
    rows.append([fp.GetReference(), '%.3fmm' % pcbnew.ToMM(pos.x), '%.3fmm' % -pcbnew.ToMM(pos.y),
                 'Bottom' if bottom else 'Top', '%.1f' % jrot])
def num(r):
    return (r.rstrip('0123456789'), int(r.lstrip('ABCDEFGHIJKLMNOPQRSTUVWXYZ')))


for suffix, keep in (('', lambda bottom: True), ('-top', lambda bottom: not bottom)):
    with open(os.path.join(out, board + suffix + '-bom.csv'), 'w', newline='') as fh:
        w = csv.writer(fh)
        w.writerow(['Comment', 'Designator', 'Footprint', 'LCSC'])
        for (val, foot, lcsc), refs in sorted(groups.items()):
            refs = [r for r, bottom in refs if keep(bottom)]
            if refs:
                w.writerow([val, ','.join(sorted(refs, key=num)), foot, lcsc])
    with open(os.path.join(out, board + suffix + '-cpl.csv'), 'w', newline='') as fh:
        w = csv.writer(fh)
        w.writerow(['Designator', 'Mid X', 'Mid Y', 'Layer', 'Rotation'])
        w.writerows(sorted([r for r in rows if keep(r[3] == 'Bottom')], key=lambda r: num(r[0])))
print('%s: %d parts for assembly (%d bottom), files in %s' % (
    board, len(rows), sum(1 for r in rows if r[3] == 'Bottom'), os.path.relpath(out, HERE)))
