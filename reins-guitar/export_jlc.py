# Export the JLCPCB order files for the REINS guitar board.
#
#   <KiCad python> export_jlc.py            (writes fab/jlc/reins-guitar/)
#   (KiCad python: /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3)
#
# For each board:
#   <board>-gerbers.zip   Gerbers and drill files: upload this as the PCB
#   <board>-bom.csv       Comment, Designator, Footprint, LCSC: the parts JLCPCB assembles
#   <board>-cpl.csv       Designator, Mid X, Mid Y, Layer, Rotation: where they go
# Only SMD footprints with an LCSC field (or an EXTRA entry), and the XIAO sockets of SPLIT, go into
# the BOM and the CPL. You solder the through-hole parts (headers, encoder, OLED) yourself: that saves
# the JLCPCB hand-soldering fee.
import csv, os, shutil, subprocess, sys, zipfile
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
CLI = '/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli'
LAYERS = 'F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts'
# REV_B item 1: the XIAO footprint holds two 1 x 7 SMD sockets (C46635843, hanxia 8.5 mm, staggered legs).
# Same footprint, same orientation (-90) and same offsets as the receiver (heat/kicad/export_jlc.py SPLIT),
# where they came from the JLCPCB placement preview.
SPLIT = {
    'Module_XIAO_Generic_SocketSMD': [
        ('A', [str(n) for n in range(1, 8)], 'C46635843', 270),
        ('B', [str(n) for n in range(8, 15)], 'C46635843', 90),
    ],
}
# REV_B item 2: parts with no LCSC field on the board. Reference: (LCSC, rotation offset).
# SW1 B3SN-3012 (C2733649, 3.1 mm high, the part the pickguard button cap is drawn for). The offset is
# not confirmed: in the preview the four legs must sit on the four pads (two on each side, 7 mm apart).
EXTRA = {'SW1': ('C2733649', 0)}


def centre(pads):
    """Centre of the pad bounding box (the mean drifts for the staggered socket rows)."""
    xs, ys = [p.x for p in pads], [p.y for p in pads]
    return (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2


def run(*args):
    subprocess.run([CLI] + list(args), check=True, stdout=subprocess.DEVNULL)


for board in ("reins-guitar",):
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
        for suffix, numbers, lcsc, offset in SPLIT.get(fp.GetFPID().GetLibItemName().wx_str(), []):
            ref = fp.GetReference() + suffix
            groups.setdefault(('1x7 socket', 'PinSocket_1x07_P2.54mm_SMD', lcsc), []).append(ref)
            x, y = centre([p.GetPosition() for p in fp.Pads() if p.GetNumber() in numbers])
            rows.append([ref, '%.3fmm' % pcbnew.ToMM(x), '%.3fmm' % -pcbnew.ToMM(y),
                         'Bottom' if fp.IsFlipped() else 'Top', '%.1f' % ((fp.GetOrientationDegrees() + offset) % 360)])
        lcsc, offset = EXTRA.get(fp.GetReference(), (fp.GetFieldText('LCSC') if fp.HasField('LCSC') else '', 0))
        if not lcsc:
            continue
        if fp.GetAttributes() & pcbnew.FP_THROUGH_HOLE:     # through-hole parts: you solder them
            continue
        key = (fp.GetValue(), fp.GetFPID().GetLibItemName().wx_str(), lcsc)
        groups.setdefault(key, []).append(fp.GetReference())
        pos = fp.GetPosition()
        rows.append([fp.GetReference(), '%.3fmm' % pcbnew.ToMM(pos.x), '%.3fmm' % -pcbnew.ToMM(pos.y),
                     'Bottom' if fp.IsFlipped() else 'Top', '%.1f' % ((fp.GetOrientationDegrees() + offset) % 360)])
    with open(os.path.join(out, board + '-bom.csv'), 'w', newline='') as fh:
        w = csv.writer(fh)
        w.writerow(['Comment', 'Designator', 'Footprint', 'LCSC'])
        for (val, foot, lcsc), refs in sorted(groups.items()):
            w.writerow([val, ','.join(sorted(refs)), foot, lcsc])
    with open(os.path.join(out, board + '-cpl.csv'), 'w', newline='') as fh:
        w = csv.writer(fh)
        w.writerow(['Designator', 'Mid X', 'Mid Y', 'Layer', 'Rotation'])
        w.writerows(sorted(rows))
    print('%-13s %d parts for assembly, files in %s' % (board, len(rows), os.path.relpath(out, HERE)))
