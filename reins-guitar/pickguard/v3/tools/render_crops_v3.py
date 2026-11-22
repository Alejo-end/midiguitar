# Crop close-ups of the REINS pickguard v3, made from the saved document v3/reins-pickguard-v3.FCStd.
# build_pickguard_v3.py runs this in a fresh FreeCAD process at the end (inside the long build run
# these renders came out blank now and then). Each image is retried until it is not blank.
# Run it alone (from the pickguard folder):
#   /Applications/FreeCAD.app/Contents/MacOS/FreeCAD v3/tools/render_crops_v3.py
import os
import time

import FreeCAD as App
import FreeCADGui as Gui
import Part

V = App.Vector
try:
    V3 = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
except NameError:
    V3 = os.path.join(os.getcwd(), 'v3')
doc = App.openDocument(os.path.join(V3, 'reins-pickguard-v3.FCStd'))
Gui.updateGui()
view = Gui.getDocument(doc.Name).activeView()
view.setCameraType('Orthographic')
CHAR, ORANGE, SCREW = (0.17, 0.18, 0.20), (0.94, 0.42, 0.08), (0.07, 0.07, 0.08)
COL = {'Plate_PlyBottom': CHAR, 'Plate_PlyMiddle': ORANGE, 'Plate_PlyTop': CHAR, 'Diffuser': (0.95, 0.94, 0.90),
       'Pod': (0.05, 0.05, 0.055), 'PodBase': (0.10, 0.10, 0.11), 'Switch_EG1201': (0.35, 0.36, 0.38), 'PickupScrews': SCREW,
       'GuardScrews': SCREW, 'SlideSwitches': (0.55, 0.56, 0.58), 'SlideScrews': SCREW, 'SlideCaps': ORANGE}
for o in list(doc.Objects):
    try:
        o.ViewObject.Visibility = False
    except Exception:
        pass
made = []


def crop(name, box, keys, cam, w, h):
    for o in made:
        doc.removeObject(o.Name)
    made.clear()
    for n in ['Plate_PlyBottom', 'Plate_PlyMiddle', 'Plate_PlyTop'] + list(keys):
        src = doc.getObject(n)
        if src is None:
            continue
        c = src.Shape.common(box)
        if c.Volume > 0.01:
            q = doc.addObject('Part::Feature', 'cx_' + n)
            q.Shape = c
            q.ViewObject.ShapeColor = COL[n]
            q.ViewObject.Deviation = 0.05
            if n == 'Diffuser':
                q.ViewObject.Transparency = 20
            made.append(q)
    doc.recompute()
    out = os.path.join(V3, 'renders', name)
    for _ in range(10):
        cam()
        view.fitAll()
        for _ in range(8):
            Gui.updateGui()
            time.sleep(0.25)
        view.saveImage(out, w, h, 'White')
        if os.path.getsize(out) > 15000:
            break
    print(f'crop {name}: {os.path.getsize(out)} bytes', flush=True)


sb = doc.getObject('SlideSwitches').Shape.BoundBox
sbox = Part.makeBox(sb.XLength + 20, 52, 50, V(sb.XMin - 10, sb.Center.y - 27, -25))
sk = ('SlideSwitches', 'SlideCaps', 'SlideScrews', 'GuardScrews', 'PickupScrews')
crop('v3-closeup-switches.png', sbox, sk, view.viewIsometric, 1800, 1200)
crop('v3-closeup-switches-top.png', sbox, sk, view.viewTop, 1800, 900)
crop('v3-closeup-axis.png', Part.makeBox(70, 125, 60, V(81.312 - 35, 68.0, -30)),
     ('Diffuser', 'Pod', 'PodBase', 'Switch_EG1201', 'SlideCaps', 'SlideScrews', 'GuardScrews', 'PickupScrews'), view.viewIsometric, 1600, 1600)
crop('v3-closeup-mark.png', Part.makeBox(70, 44, 40, V(115, 138, -5)), ('GuardScrews', 'PickupScrews'), view.viewTop, 1800, 1200)
os._exit(0)
