# Export the JLCPCB order files for the REINS guitar board.
#
#   <KiCad python> export_jlc.py            (writes fab/jlc/reins-guitar/)
#   (KiCad python: /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3)
#
# For each board:
#   <board>-gerbers.zip   Gerbers and drill files: upload this as the PCB
#   <board>-bom.csv       Comment, Designator, Footprint, LCSC: the parts JLCPCB assembles
#   <board>-cpl.csv       Designator, Mid X, Mid Y, Layer, Rotation: where they go
# Only SMD footprints with an LCSC field go into the BOM and the CPL. You solder the through-hole
# parts (headers, buttons) yourself: that saves the JLCPCB hand-soldering fee.
import csv, os, shutil, subprocess, sys, zipfile
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
CLI = '/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli'
LAYERS = 'F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts'


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
        if not fp.HasField('LCSC') or not fp.GetFieldText('LCSC'):
            continue
        if fp.GetAttributes() & pcbnew.FP_THROUGH_HOLE:     # through-hole parts: you solder them
            continue
        lcsc = fp.GetFieldText('LCSC')
        key = (fp.GetValue(), fp.GetFPID().GetLibItemName().wx_str(), lcsc)
        groups.setdefault(key, []).append(fp.GetReference())
        pos = fp.GetPosition()
        rows.append([fp.GetReference(), '%.3fmm' % pcbnew.ToMM(pos.x), '%.3fmm' % -pcbnew.ToMM(pos.y),
                     'Bottom' if fp.IsFlipped() else 'Top', '%.1f' % (fp.GetOrientationDegrees() % 360)])
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
