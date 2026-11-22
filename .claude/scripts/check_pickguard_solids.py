# Interference checks on the saved REINS_Pickguard part. The FreeCAD app hides the prints of
# build_pickguard.py, so this repeats them from the saved file and changes nothing.
# Run: CHECK_FILE=<copy of cad-midiguitar.FCStd> freecadcmd check_pickguard_solids.py
import os
import FreeCAD as App
import Part

out = open(os.environ['CHECK_OUT'], 'w')


def print(*a):
    out.write(' '.join(str(x) for x in a) + '\n')
    out.flush()


doc = App.openDocument(os.environ['CHECK_FILE'])
group = doc.getObject('REINS_Pickguard')
kids = {o.Name: o for o in group.Group}
print('objects', sorted(kids)[:30])
printed = [kids[n] for n in ('PlyBottom', 'PlyMiddle', 'PlyTop', 'Diffuser', 'ToF_Eye', 'OLED_Bezel', 'Knob', 'ButtonCap')]
others = [(kids[n].Label, kids[n].Shape) for n in ('NeoPixel_Ring16', 'NeoPixel_LEDs', 'ToF_Board', 'ToF_Sensor', 'Pickups_Measured')]
board = [o for o in group.Group if o.Label == 'REINS guitar board']
if board:
    for c in board[0].Group:
        if hasattr(c, 'Shape'):
            others.append((c.Label, Part.getShape(board[0], c.Name + '.')))
print('board parts', len(others) - 5)
bad = 0
for p in printed:
    for label, s in others:
        if s.isNull():
            continue
        v = p.Shape.common(s).Volume
        if v > 0.01:
            print(f'  HIT {p.Label} x {label}: {v:.2f} mm3')
            bad += 1
for i in range(len(printed)):
    for j in range(i + 1, len(printed)):
        v = printed[i].Shape.common(printed[j].Shape).Volume
        if v > 0.01:
            print(f'  HIT {printed[i].Label} x {printed[j].Label}: {v:.2f} mm3')
            bad += 1
print('contacts', bad)
whole = kids['Pickguard'].Shape
print('plate solids', len(whole.Solids), 'valid', whole.isValid(), 'volume', round(whole.Volume))
for s in whole.Solids:
    print('  solid volume', round(s.Volume, 1))
os._exit(0)
