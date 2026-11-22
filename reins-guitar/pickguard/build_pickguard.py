# Builds the REINS pickguard for the Squier Mustang: the pickguard plate with the REINS guitar
# board hanging under it, the guitar's SparkFun 1 in ToF board (VL53L4CD or VL53L1X) under a
# glowing 16 LED NeoPixel ring in the middle, an OLED bezel, an encoder knob and a button cap.
# The outline comes from a scan of a Bullet Mustang guard (ref/scan_outline.json). The pickup
# openings and slide switch slots come from the user's sketch (Body007 / Sketch001 in
# cad/cad-midiguitar.FCStd), stored in ref/sketch001.json. The screw holes and the board position
# come from photos of the guitar (ref/measured.json).
#
# The plate is printed in three colour plies (a filament change at two heights): charcoal,
# orange, charcoal. The 45 degree edge bevel, the countersinks, the chamfer around the ring and
# the engraved wave field cut through the top ply and show the orange ply, like the white line
# of a classic three ply pickguard. The colours match the REINS receiver lid.
#
# Export the board STEP first (from the reins-guitar folder):
#   kicad-cli pcb export step --subst-models --include-tracks --include-pads --include-zones \
#     -o /tmp/reins-guitar.step reins-guitar.kicad_pcb
# Then run the script in the FreeCAD app (colours and renders need the GUI):
#   BOARD_STEP=/tmp/reins-guitar.step FC_EXIT=1 \
#     /Applications/FreeCAD.app/Contents/MacOS/FreeCAD build_pickguard.py
#
# The script opens cad/cad-midiguitar.FCStd (CAD_FILE), replaces the "REINS_Pickguard" part in it
# and saves it. The first run copies the file to cad-midiguitar.pre-reins.FCStd. It also writes,
# next to this script:
#   reins-pickguard-assembly.step   the assembly for any CAD viewer
#   stl/*.stl                       the printed parts (the plate also as three plies)
#   renders/*.png                   views of the assembly (GUI only)
#
# Coordinates: X, Y of Sketch001 (X towards the neck, Y towards the bass side), Z = 0 on the
# body top, which is the underside of the plate.
#
# Stack (Z, mm):  ToF board -9.8..-8.2 | REINS PCB -7.6..-6.1 | body top 0 | plate top 3.0
#                 XIAO on its sockets from 3.04, OLED module from 3.09, knob 4.0..15.1
# The board hangs from four bosses under the plate (M2 screws from below). The XIAO plugs into
# its sockets through a window, so the USB-C port stays on top of the pickguard. The OLED module
# plugs into its header through a slot and rests on the plate, held by the bezel screws.

import json
import math
import os
import shutil
import sys

import FreeCAD as App
import Import
import Mesh
import Part

HERE = os.path.dirname(os.path.abspath(__file__))
CAD_FILE = os.environ.get('CAD_FILE', os.path.normpath(
    os.path.join(HERE, '..', '..', '..', 'cad', 'cad-midiguitar.FCStd')))
BOARD_STEP = os.environ.get('BOARD_STEP')
GUI = App.GuiUp
FONT = '/System/Library/Fonts/Supplemental/Arial Rounded Bold.ttf'
REF = json.load(open(os.path.join(HERE, 'ref', 'sketch001.json')))
# Measured on the real guitar from three photos, rectified with the pickguard screw holes:
# the nine pickguard screw holes, the two pickups (from their mounting screws) and the board
# cavity. See ref/measured.json and README.md.
MEAS = json.load(open(os.path.join(HERE, 'ref', 'measured.json')))

# ---------------------------------------------------------------- parameters
PLY = (1.6, 0.6, 0.8)            # bottom, middle, top ply (multiples of 0.2 mm layers)
T = sum(PLY)                     # plate thickness, 3.0
Z_MID, Z_TOP = PLY[0], PLY[0] + PLY[1]
GAP = 6.1                        # body top to REINS PCB top. Measure: PCB top to XIAO underside - T
BEVEL = 1.4                      # outer edge bevel, cuts through the top and middle ply
SLOT_CHAMFER = 1.0               # chamfer on the pickup openings and switch slots
CSK = 90.0                       # countersink angle

# Pickups: Squier Sonic Mustang ceramic single coils. The openings come from Sketch001 (see PK).
# The cover model below uses the screw positions from the photos and is only for the fit check.
PICKUP_COVER = (70.0, 18.0)      # length, width of the black cover at the pickguard
PICKUP_CLEAR = 0.1               # extra per side on the measured openings

# Board placement: centred in the measured cavity opening, which is only about 0.5 mm wider
# than the board. Fine tune with BOARD_DX / DY / DROT after the fit test print.
BOARD_DX, BOARD_DY, BOARD_DROT = 0.0, 0.0, 0.0
_bp = MEAS['board_pose']
_bc = (155.225, 127.295)         # board outline centre, KiCad
THETA = 180.0 + _bp['angle'] + BOARD_DROT
_t = math.radians(THETA)
TX = _bp['cx'] + BOARD_DX - (_bc[0] * math.cos(_t) + _bc[1] * math.sin(_t))
TY = _bp['cy'] + BOARD_DY - (_bc[0] * math.sin(_t) - _bc[1] * math.cos(_t))

# How the board and the ToF eye attach:
#   'boss'     printed bosses under the plate, M2 self tapping screws from below (default)
#   'insert'   printed bosses with M2 heat-set inserts (3.2 mm holes, 4 mm deep)
#   'standoff' no bosses: M2 x 6 standoffs, M2 countersunk screws from the top. Use this for
#              a laser cut plate, together with stl/../reins-pickguard-flat.dxf
MOUNT = os.environ.get('MOUNT', 'boss')

# Jumper wires from the ToF board and the ring to the REINS board: a groove under the plate
# over the body channel between the pickup cavity and the board cavity.
CABLE_GROOVE = (MEAS['channel']['x'], MEAS['channel']['y0'], MEAS['channel']['y1'], MEAS['channel']['width'], 0.8)
PCB_T = 1.51
Z_PCB_TOP = -GAP
Z_PCB_BOT = -GAP - PCB_T

# Heights above the PCB top, from the KiCad STEP of reins-guitar.
H_SOCKET_TOP = 9.14              # XIAO underside
H_OLED_PCB = 9.19                # OLED module underside
H_OLED_GLASS = 11.72
H_OLED_PINS = 11.39
H_ENC_BODY = 8.19
# Encoder PEC12R-4225F-S0024, from the Bourns drawing: collar to 11.1 mm, shaft flat from 13 mm.
# Saw the 25 mm shaft to ENC_SHAFT_CUT. Its shaft is plastic. The board STEP shows the 15 mm model.
H_ENC_COLLAR = 11.1
H_ENC_FLAT = 13.0
ENC_SHAFT_CUT = 20.0
ENC_HOLE_R = 3.8                 # plate and knob clearance around the 7 mm collar
H_BUTTON = 3.1                   # Omron B3SN top
BTN_R = 4.4                      # 8.8 mm cap. The knob edge is 11.36 - 6.5 = 4.86 mm from the centre
BTN_HOLE_R = 4.6
BTN_FLANGE_R = 5.4
BTN_PROUD = 2.8                  # cap height above the plate top

# ToF and LED ring, centred on the ring of Sketch001.
POD_C = (81.312, 117.374)
POD_ROT = -5.567                 # ToF board turn, from the four ToF holes of Sketch001
RING_ID, RING_OD, RING_T = 31.75, 44.5, 2.5      # Adafruit NeoPixel Ring 16 (1463)
LED_R = (RING_ID + RING_OD) / 4
TOF_W, TOF_T, TOF_HOLE = 25.4, 1.6, 10.16        # SparkFun Qwiic 1 in board, holes on 0.8 in
Z_RING = -0.6                    # ring PCB underside
Z_TOF_TOP = -8.2                 # ToF board top
HOLE_R = 23.0                    # plate hole for the diffuser
COLLAR = (26.2, 29.5, -4.0)      # collar under the plate: inner r, outer r, bottom z
SCREW_R = 27.85                  # eye screws (M2) into the collar

# Art
NAME = 'REINS'
NAME_SIZE = 7.0
NAME_AT = (POD_C[0], 153.6)
RECEIVER = (-35.0, -35.0)        # centre of the receiver's rings, off the bridge side corner
PITCH = 3.0
W_NEAR, W_FAR = 1.1, 0.6
MARGIN = BEVEL + 2.6
HALO = 2.0
SEAM = 1.6
ENGRAVE = PLY[2]                 # grooves go through the top ply to the orange ply

# ---------------------------------------------------------------- helpers
def V(x, y, z=0.0):
    return App.Vector(x, y, z)


def poly(pts, z=0.0):
    p = [V(x, y, z) for x, y in pts]
    return Part.Face(Part.makePolygon(p + [p[0]]))


def disc(x, y, r, z=0.0):
    return Part.Face(Part.Wire(Part.makeCircle(r, V(x, y, z))))


def rect(cx, cy, w, h, z=0.0):
    return poly([(cx - w / 2, cy - h / 2), (cx + w / 2, cy - h / 2), (cx + w / 2, cy + h / 2), (cx - w / 2, cy + h / 2)], z)


def rrect(cx, cy, w, h, r, z=0.0):
    """Rounded rectangle from four lines and four quarter arcs."""
    x0, x1, y0, y1 = cx - w / 2, cx + w / 2, cy - h / 2, cy + h / 2
    n = V(0, 0, 1)
    e = [Part.LineSegment(V(x0 + r, y0, z), V(x1 - r, y0, z)).toShape(),
         Part.makeCircle(r, V(x1 - r, y0 + r, z), n, 270, 360),
         Part.LineSegment(V(x1, y0 + r, z), V(x1, y1 - r, z)).toShape(),
         Part.makeCircle(r, V(x1 - r, y1 - r, z), n, 0, 90),
         Part.LineSegment(V(x1 - r, y1, z), V(x0 + r, y1, z)).toShape(),
         Part.makeCircle(r, V(x0 + r, y1 - r, z), n, 90, 180),
         Part.LineSegment(V(x0, y1 - r, z), V(x0, y0 + r, z)).toShape(),
         Part.makeCircle(r, V(x0 + r, y0 + r, z), n, 180, 270)]
    return Part.Face(Part.Wire(Part.__sortEdges__(e)))


def kbox(x0, y0, x1, y1, z=0.0):
    """Rectangle in KiCad coordinates (y down), in the board frame."""
    return poly([(x0, -y0), (x1, -y0), (x1, -y1), (x0, -y1)], z)


def kdisc(x, y, r, z=0.0):
    return disc(x, -y, r, z)


def krrect(x0, y0, x1, y1, r, z=0.0):
    return rrect((x0 + x1) / 2, -(y0 + y1) / 2, x1 - x0, y1 - y0, r, z)


def obround(cx, cy, length, width, angle, z=0.0):
    r = width / 2
    s = length / 2 - r
    f = Part.Face(Part.Wire([Part.LineSegment(V(-s, -r, z), V(s, -r, z)).toShape(),
                             Part.Arc(V(s, -r, z), V(s + r, 0, z), V(s, r, z)).toShape(),
                             Part.LineSegment(V(s, r, z), V(-s, r, z)).toShape(),
                             Part.Arc(V(-s, r, z), V(-s - r, 0, z), V(-s, -r, z)).toShape()]))
    f.rotate(V(0, 0, z), V(0, 0, 1), angle)
    f.translate(V(cx, cy, 0))
    return f


def prism(face, z0, z1):
    """Extrude a flat face from z0 to z1. (A B-spline face has a loose bounding box, so the
    face height comes from a vertex.)"""
    f = face.copy()
    f.translate(V(0, 0, z0 - f.Vertexes[0].Point.z))
    return f.extrude(V(0, 0, z1 - z0))


def cyl(x, y, r, z0, z1):
    return Part.makeCylinder(r, z1 - z0, V(x, y, z0))


def cone(x, y, r0, r1, z0, z1):
    return Part.makeCone(r0, r1, z1 - z0, V(x, y, z0))


def union(shapes):
    s = shapes[0]
    for o in shapes[1:]:
        s = s.fuse(o)
    return s.removeSplitter()


BOARD = App.Placement(V(TX, TY, 0), App.Rotation(V(0, 0, 1), THETA))
POD = App.Placement(V(POD_C[0], POD_C[1], 0), App.Rotation(V(0, 0, 1), POD_ROT))


def on_board(shape):
    s = shape.copy()
    s.Placement = BOARD.multiply(s.Placement)
    return s


def on_pod(shape):
    s = shape.copy()
    s.Placement = POD.multiply(s.Placement)
    return s


def bpt(kx, ky):
    v = BOARD.multVec(V(kx, -ky, 0))
    return v.x, v.y


def zb(h):
    """Height above the PCB top -> Z."""
    return Z_PCB_TOP + h


# ---------------------------------------------------------------- document
if os.path.exists(CAD_FILE):
    backup = CAD_FILE.replace('.FCStd', '.pre-reins.FCStd')
    if not os.path.exists(backup):
        shutil.copy2(CAD_FILE, backup)
        print('backup', backup)
    doc = App.openDocument(CAD_FILE)
else:
    doc = App.newDocument('reins_pickguard')

old = doc.getObject('REINS_Pickguard')
if old:
    for o in old.OutListRecursive:
        try:
            doc.removeObject(o.Name)
        except Exception:
            pass
    doc.removeObject(old.Name)
    doc.recompute()
group = doc.addObject('App::Part', 'REINS_Pickguard')
group.Label = 'REINS Pickguard'


def static(name, shape, parent=group):
    o = doc.addObject('Part::Feature', name)
    o.Label = name
    o.Shape = shape
    parent.addObject(o)
    return o


# ---------------------------------------------------------------- sketch features
def turn(a, b, c):
    v1, v2 = (b[0] - a[0], b[1] - a[1]), (c[0] - b[0], c[1] - b[1])
    return math.degrees(math.atan2(v1[0] * v2[1] - v1[1] * v2[0], v1[0] * v2[0] + v1[1] * v2[1]))


def smooth_outline(pts, corner=35.0, tol=0.12):
    """The sketch outline is a traced polygon with a few zero width spikes. Drop the spikes and
    the points closer than 0.3 mm, then fit one B-spline per stretch between sharp corners."""
    P = list(pts)
    changed = True
    while changed:
        changed = False
        n = len(P)
        for i in range(n):
            a, b, c = P[i - 1], P[i], P[(i + 1) % n]
            if math.dist(a, b) < 0.3 or abs(turn(a, b, c)) > 150:
                P.pop(i); changed = True
                break
    n = len(P)
    corners = [i for i in range(n) if abs(turn(P[i - 1], P[i], P[(i + 1) % n])) > corner]
    if not corners:
        corners = [0]
    edges = []
    for k, i0 in enumerate(corners):
        i1 = corners[(k + 1) % len(corners)]
        idx = [(i0 + j) % n for j in range(((i1 - i0) % n or n) + 1)]
        chain = [V(*P[j]) for j in idx]
        if len(chain) <= 3:
            edges += [Part.LineSegment(chain[j], chain[j + 1]).toShape() for j in range(len(chain) - 1)]
            continue
        if all(abs(turn(P[idx[j - 1]], P[idx[j]], P[idx[j + 1]])) < 0.5 for j in range(1, len(idx) - 1)):
            edges.append(Part.LineSegment(chain[0], chain[-1]).toShape())
            continue
        bs = Part.BSplineCurve()
        bs.approximate(Points=chain, DegMin=3, DegMax=3, Tolerance=tol)
        edges.append(bs.toShape())
    return Part.Face(Part.Wire(Part.__sortEdges__(edges)))


# The outline comes from a scan of a Bullet Mustang guard, the same shape as the Sonic, fitted to
# the nine measured screw holes (ref/scan_outline.json). The old sketch outline had the wrong
# bridge cut.
SCAN = json.load(open(os.path.join(HERE, 'ref', 'scan_outline.json')))
outline = smooth_outline(SCAN['outline'])
print(f'outline: {len(outline.Edges)} edges, area {outline.Area:.0f} mm2, valid {outline.isValid()}')
# Pickup openings: the owner's measured openings in Sketch001, plus PICKUP_CLEAR per side.
PK = [dict(cx=p['cx'], cy=p['cy'], length=p['length'] + 2 * PICKUP_CLEAR,
           width=p['width'] + 2 * PICKUP_CLEAR, angle=p['angle']) for p in REF['pickups']]
pickups = [obround(p['cx'], p['cy'], p['length'], p['width'], p['angle']) for p in PK]
slides = [poly(r['pts']) for r in REF['rects'] if r['cy'] > 150]
ring_c = [c for c in REF['circles'] if c['n'] == 24]


def near_slide(c):
    # two screws per switch, on the slot axis next to each end
    return any(abs(c['cy'] - r['cy']) < 3.0 and abs(c['cx'] - r['cx']) < 18 for r in REF['rects'] if r['cy'] > 150)


switch_screws = [c for c in ring_c if near_slide(c)]
# Pickguard screws: the nine measured holes. Pickup screws: the four measured screws.
guard_screws = [dict(cx=h['cx'], cy=h['cy']) for h in MEAS['guard_screws']]
guard_screws += [dict(cx=s[0], cy=s[1]) for p in MEAS['pickups'].values() for s in p['screws']]
print(f'{len(guard_screws)} pickguard and pickup screw holes, {len(switch_screws)} switch screw holes')

# ---------------------------------------------------------------- plate
# 45 degree bevel on the outer edge, as 0.2 mm steps: the same steps the printer makes with
# 0.2 mm layers. (A loft or a chamfer twists at the sharp corners of the traced outline.)
steps = int(round(BEVEL / 0.2))
plate = prism(outline, 0, T - BEVEL)
for i in range(steps):
    inset = outline.makeOffset2D(-(i + 0.5) * 0.2, 0, False, False, False)
    plate = plate.fuse(prism(inset, T - BEVEL + i * 0.2, T - BEVEL + (i + 1) * 0.2))
plate = plate.removeSplitter()

cuts = []
for f in pickups + slides:
    cuts.append(prism(f, -1, T + 1))
for c, d, dk in [(c, 3.5, 7.0) for c in guard_screws] + [(c, 3.0, 6.0) for c in switch_screws]:
    cuts.append(cyl(c['cx'], c['cy'], d / 2, -1, T + 1))
    depth = (dk - d) / 2 / math.tan(math.radians(CSK / 2))
    cuts.append(cone(c['cx'], c['cy'], d / 2, dk / 2, T - depth, T))
    cuts.append(cyl(c['cx'], c['cy'], dk / 2, T, T + 1))
plate = plate.cut(union(cuts))

# Chamfer on the pickup openings and switch slots (shows the orange ply): lofted cutters.
c = SLOT_CHAMFER
tools = []
for p_, f in zip(PK, pickups):
    big = obround(p_['cx'], p_['cy'], p_['length'] + 2 * c, p_['width'] + 2 * c, p_['angle'])
    lo, hi = f.OuterWire.copy(), big.OuterWire.copy()
    lo.translate(V(0, 0, T - c)); hi.translate(V(0, 0, T + 0.001))
    tools.append(Part.makeLoft([lo, hi], True, True))
for f in slides:
    lo, hi = f.OuterWire.copy(), f.makeOffset2D(c, 2, False, False, False).OuterWire.copy()
    lo.translate(V(0, 0, T - c)); hi.translate(V(0, 0, T + 0.001))
    tools.append(Part.makeLoft([lo, hi], True, True))
plate = plate.cut(union(tools))

# ---------------------------------------------------------------- board features
XIAO_WIN = (168.5, 118.5, 187.3, 137.2)          # both socket rows and the space between
TP1 = (168.09, 118.42)                           # BAT+ wire up to the XIAO underside
USB_GROOVE = (183.0, 127.87 - 7.0, 216.0, 127.87 + 7.0)
OLED_MOD = (122.2, 113.7, 150.0, 141.0)
OLED_GLASS = (126.82, 114.01, 146.08, 140.71)
OLED_ACTIVE = (128.42, 116.49, 139.28, 138.23)
OLED_HEADER = (122.5, 121.9, 125.5, 132.8)
OLED_PINS = (123.0, 122.0, 125.0, 132.7)          # header pins
OLED_PARTS = [(123.7, 117.45, 125.1, 119.5), (123.7, 120.3, 125.1, 122.5), (123.7, 134.8, 125.1, 137.0)]
OLED_HOLES = [(124.57, 116.19), (147.68, 116.19), (124.57, 138.54), (147.68, 138.54)]
OLED_POCKET = (126.6, 118.6, 145.6, 136.1)
ENC = (158.25, 128.79)
ENC_BODY = (151.55, 121.79, 164.95, 135.79)
BUTTON = (156.89, 117.51)
HOLES = [(166.47, 114.38), (187.07, 115.76), (180.98, 140.26), (151.53, 140.6)]   # H1..H4

z_enc_top = zb(H_ENC_BODY)
bcuts = [
    prism(kbox(*XIAO_WIN).fuse(kdisc(*TP1, 1.9)).removeSplitter(), -1, T + 1),
    prism(krrect(*USB_GROOVE, 3.0), T - 1.2, T + 1),
    prism(krrect(*OLED_HEADER, 0.5), -1, T + 1),
    prism(krrect(*OLED_POCKET, 1.0), T - 1.2, T + 1),
    prism(krrect(ENC_BODY[0] - 0.5, ENC_BODY[1] - 0.5, ENC_BODY[2] + 0.5, ENC_BODY[3] + 0.5, 1.5), -1, z_enc_top + 0.2),
    cyl(ENC[0], -ENC[1], ENC_HOLE_R, -1, T + 1),
    cyl(BUTTON[0], -BUTTON[1], BTN_HOLE_R, -1, T + 1),
] + [cyl(x, -y, 0.9, -1, T + 1) for x, y in OLED_HOLES]
r_boss = 2.6 if MOUNT == 'insert' else 2.2
if MOUNT == 'standoff':
    # through holes, 90 degree countersink for M2 flat heads from the top
    pilots = union([cyl(x, -y, 1.2, -1, T + 1).fuse(cone(x, -y, 1.2, 2.2, T - 1.0, T)).fuse(cyl(x, -y, 2.2, T, T + 1))
                    for x, y in HOLES])
    plate = plate.cut(on_board(union(bcuts))).cut(on_board(pilots))
else:
    r_pilot, d_pilot = (1.6, 4.2) if MOUNT == 'insert' else (0.85, GAP + 1.7)
    bosses = union([cyl(x, -y, r_boss, Z_PCB_TOP + 0.1, 0.01) for x, y in HOLES])   # 0.1 mm over the copper
    pilots = union([cyl(x, -y, r_pilot, Z_PCB_TOP - 0.1, Z_PCB_TOP - 0.1 + d_pilot) for x, y in HOLES])
    plate = plate.fuse(on_board(bosses)).cut(on_board(union(bcuts))).cut(on_board(pilots))

# USB groove must stay inside the outline: clip the plate again to the outline prism.
plate = plate.common(prism(outline, Z_PCB_TOP - 1, T + 1))

# ---------------------------------------------------------------- ToF and ring pod
cx, cy = POD_C
board_mid = bpt(155.2, 127.3)
wire_ang = math.degrees(math.atan2(board_mid[1] - cy, board_mid[0] - cx)) - POD_ROT

collar = cyl(0, 0, COLLAR[1], COLLAR[2], 0.01).cut(cyl(0, 0, COLLAR[0], COLLAR[2] - 1, 1))
collar_pilots = union([cyl(SCREW_R * math.cos(math.radians(a)), SCREW_R * math.sin(math.radians(a)), 0.85, COLLAR[2] - 0.1, 1.2)
                       for a in (90, 210, 330)])
pod_cut = cyl(0, 0, HOLE_R, -1, T + 1).fuse(cone(0, 0, HOLE_R, HOLE_R + 0.9, T - 0.9, T)).fuse(cyl(0, 0, HOLE_R + 0.9, T, T + 1))
if MOUNT == 'standoff':
    # no collar: three M2 x 4 standoffs, M2 countersunk screws from the top
    collar_pilots = union([cyl(SCREW_R * math.cos(math.radians(a)), SCREW_R * math.sin(math.radians(a)), 1.2, -1, T + 1)
                           .fuse(cone(SCREW_R * math.cos(math.radians(a)), SCREW_R * math.sin(math.radians(a)), 1.2, 2.2, T - 1.0, T))
                           for a in (90, 210, 330)])
    plate = plate.cut(on_pod(pod_cut)).cut(on_pod(collar_pilots)).removeSplitter()
else:
    plate = plate.fuse(on_pod(collar)).cut(on_pod(pod_cut)).cut(on_pod(collar_pilots)).removeSplitter()

# Cable groove under the plate over the body channel (jumper wires to the board).
gx, gy0, gy1, gw, gd = CABLE_GROOVE
# The groove keeps a column around each board screw: H3 stands in the groove, and a plain cut would
# part its boss from the plate.
groove = prism(rrect(gx, (gy0 + gy1) / 2, gw, gy1 - gy0, gw / 2 - 0.01), -1, gd)
groove = groove.cut(on_board(union([cyl(x, -y, r_boss, -2, gd + 1) for x, y in HOLES])))
plate = plate.cut(groove).removeSplitter()

# Diffuser (translucent): a cap over the ring, flange under the plate.
dif = cyl(0, 0, HOLE_R - 0.15, Z_RING - 0.2, T).cut(cyl(0, 0, 15.1, Z_RING - 1, T + 1))
dif = dif.cut(cyl(0, 0, 22.35, Z_RING - 1, Z_RING + RING_T + 0.1).cut(cyl(0, 0, 15.8, Z_RING - 2, T)))
dif = dif.fuse(cyl(0, 0, 26.0, Z_RING - 0.2, 0).cut(cyl(0, 0, 22.35, Z_RING - 1, 1)))
diffuser = on_pod(dif.removeSplitter())

# Eye (opaque): iris with the sensor aperture, ring seat, flange clamp, base, ToF bosses.
z_base = COLLAR[2] - 1.0
eye = cyl(0, 0, COLLAR[1], z_base, COLLAR[2])                                     # base
eye = eye.fuse(cyl(0, 0, 14.9, COLLAR[2] - 0.01, T))                               # iris
eye = eye.fuse(cyl(0, 0, 22.2, COLLAR[2] - 0.01, Z_RING).cut(cyl(0, 0, 15.9, COLLAR[2] - 1, 1)))  # ring seat
eye = eye.fuse(cyl(0, 0, 26.1, COLLAR[2] - 0.01, Z_RING - 0.2).cut(cyl(0, 0, 22.95, COLLAR[2] - 1, 1)))  # flange clamp
for sx in (-1, 1):
    for sy in (-1, 1):
        eye = eye.fuse(cyl(sx * TOF_HOLE, sy * TOF_HOLE, 2.7, Z_TOF_TOP, z_base + 0.01))
eye = eye.fuse(cyl(0, 0, 3.9, -6.4, z_base + 0.01))                                # light skirt
eye = eye.cut(cone(0, 0, 2.8, 4.2, z_base, T + 0.001)).cut(cyl(0, 0, 3.1, -7, z_base + 0.01))
eye = eye.cut(cone(0, 0, 4.2, 4.8, T - 0.6, T + 0.001))                            # rim chamfer
for sx in (-1, 1):
    for sy in (-1, 1):
        eye = eye.cut(cyl(sx * TOF_HOLE, sy * TOF_HOLE, 1.25, Z_TOF_TOP - 0.1, z_base - 0.5))
for a in (90, 210, 330):
    x, y = SCREW_R * math.cos(math.radians(a)), SCREW_R * math.sin(math.radians(a))
    eye = eye.cut(cyl(x, y, 1.15, z_base - 1, COLLAR[2] + 0.1)).cut(cyl(x, y, 2.1, z_base - 1, z_base + 0.0))
slot = rect(19.0, 0, 5.0, 6.0)
slot.rotate(V(0, 0, 0), V(0, 0, 1), wire_ang)
eye = eye.cut(prism(slot, z_base - 1, Z_RING + 0.01))                              # ring wires
eye_o = on_pod(eye.removeSplitter())

# ---------------------------------------------------------------- bezel, knob, button cap
bz0, bz_skin0 = T, zb(H_OLED_GLASS) + 0.1
bz1 = bz_skin0 + 1.2
m = OLED_MOD
bez = prism(krrect(m[0] - 1.5, m[1] - 1.5, m[2] + 1.1, m[3] + 1.5, 2.0), bz0, bz1)
bez = bez.cut(prism(kbox(m[0] - 0.2, m[1] - 0.2, m[2] + 0.2, m[3] + 0.2), bz0 - 1, zb(H_OLED_PCB + 1.2) + 0.1))
g = OLED_GLASS
bez = bez.cut(prism(kbox(g[0] - 0.25, g[1] - 0.25, g[2] + 0.25, g[3] + 0.25), bz0, bz_skin0))
bez = bez.cut(prism(kbox(*OLED_PINS), bz0, bz_skin0))
for b_ in OLED_PARTS:                                                            # small parts on the module
    bez = bez.cut(prism(kbox(*b_), bz0, zb(H_OLED_PCB + 1.2) + 0.8))
a = OLED_ACTIVE
win_lo = kbox(a[0] - 0.5, a[1] - 0.5, a[2] + 0.5, a[3] + 0.5, bz_skin0 - 0.01)
win_hi = kbox(a[0] - 1.7, a[1] - 1.7, a[2] + 1.7, a[3] + 1.7, bz1 + 0.001)
bez = bez.cut(Part.makeLoft([win_lo.OuterWire, win_hi.OuterWire], True, True))
for x, y in OLED_HOLES:
    bez = bez.cut(cyl(x, -y, 1.15, bz0 - 1, bz1 + 1)).cut(cyl(x, -y, 1.95, bz1 - 1.1, bz1 + 1))   # M2 head in the skin
bezel = on_board(bez.removeSplitter())

z_k0 = T + 1.0                   # 1 mm above the plate, room for the encoder push
z_k1 = zb(ENC_SHAFT_CUT) + 1.2   # 1.2 mm over the shaft end
knob = cyl(0, 0, 6.5, z_k0, z_k1 - 1.0).fuse(cone(0, 0, 6.5, 5.5, z_k1 - 1.0, z_k1))
for i in range(20):
    ang = math.radians(i * 18)
    knob = knob.cut(cyl(6.75 * math.cos(ang), 6.75 * math.sin(ang), 0.55, z_k0 + 1.2, z_k1 + 1))
knob = knob.cut(prism(rect(3.2, 0, 4.4, 1.0), z_k1 - 0.6, z_k1 + 1))            # pointer
# Bore for PEC12R-4225F-S0024: room for the 5 mm collar, then the round shaft, then a D for the
# flat, which is 4.5 mm across and starts 13 mm above the board.
collar_bore = cyl(0, 0, ENC_HOLE_R, z_k0 - 1, zb(H_ENC_COLLAR) + 0.4)
round_bore = cyl(0, 0, 3.05, zb(H_ENC_COLLAR) + 0.3, zb(H_ENC_FLAT) + 0.2)
d_bore = cyl(0, 0, 3.05, zb(H_ENC_FLAT) + 0.1, zb(ENC_SHAFT_CUT) + 0.3).common(
    prism(rect(-1.5 + 0.05, 0, 6.1, 8), zb(H_ENC_FLAT), z_k1))
knob = knob.cut(collar_bore).cut(round_bore).cut(d_bore)
knob.translate(V(ENC[0], -ENC[1], 0))
knob_o_shape = on_board(knob.removeSplitter())

# Button cap: one revolved piece, wide on top, with a dished finger seat and a flange under the
# plate. It goes in from below before the board. The flange is cut flat on the encoder side.
z_b0 = zb(H_BUTTON)              # rests on the B3SN actuator
z_top = T + BTN_PROUD
prof = [(0, z_b0), (BTN_FLANGE_R, z_b0), (BTN_FLANGE_R, z_b0 + 1.4), (BTN_R, z_b0 + 1.4), (BTN_R, z_top - 0.8),
        (BTN_R - 0.8, z_top), (BTN_R - 1.2, z_top), (BTN_R - 2.4, z_top - 0.45), (0, z_top - 0.7)]
pts_ = [V(r, 0, z) for r, z in prof]
cap = Part.Face(Part.makePolygon(pts_ + [pts_[0]])).revolve(V(0, 0, 0), V(0, 0, 1), 360)
cap = Part.Solid(cap) if cap.ShapeType != 'Solid' else cap
cap = cap.cut(Part.makeBox(30, 10, z_top - z_b0 + 2, V(-15, -3.65 - 10, z_b0 - 1)).cut(cyl(0, 0, BTN_R, z_b0 - 2, z_top + 2)))
cap.translate(V(BUTTON[0], -BUTTON[1], 0))
cap_shape = on_board(cap.removeSplitter())

# ---------------------------------------------------------------- engraving
def text(s, size, x, y):
    faces = [Part.makeFace(ch, 'Part::FaceMakerBullseye') for ch in Part.makeWireString(s, FONT, size, 0) if ch]
    comp = Part.makeCompound(faces)
    bb = comp.BoundBox
    comp.translate(V(x - bb.Center.x, y - bb.Center.y, 0))
    return comp


def grow(face, d):
    out = []
    for f in face.Faces:
        try:
            g = Part.Face(Part.Wire(f.OuterWire)).makeOffset2D(d, 0, False, False, False)
        except Exception:
            bb = f.BoundBox
            g = rrect(bb.Center.x, bb.Center.y, bb.XLength + 2 * d, bb.YLength + 2 * d, d)
        out += g.Faces
    return union(out)


def half_plane(src, other, gap):
    dx, dy = other[0] - src[0], other[1] - src[1]
    n = math.hypot(dx, dy)
    dx, dy = dx / n, dy / n
    mx, my = (src[0] + other[0]) / 2 - dx * gap / 2, (src[1] + other[1]) / 2 - dy * gap / 2
    tx, ty, L = -dy, dx, 800.0
    return poly([(mx + tx * L, my + ty * L), (mx - tx * L, my - ty * L),
                 (mx - tx * L - dx * L, my - ty * L - dy * L), (mx + tx * L - dx * L, my + ty * L - dy * L)])


def rings(src, r0, region):
    out = []
    bb = region.BoundBox
    far = max(math.hypot(px - src[0], py - src[1]) for px in (bb.XMin, bb.XMax) for py in (bb.YMin, bb.YMax))
    n = int((far - r0) / PITCH) + 1
    for i in range(n):
        r = r0 + i * PITCH
        w = W_NEAR + (W_FAR - W_NEAR) * min(i / max(n - 1, 1), 1)
        ring = disc(src[0], src[1], r + w / 2).cut(disc(src[0], src[1], r - w / 2))
        piece = ring.common(region)
        out += [f for f in piece.Faces if f.Area > 1.5]
    return out


def flat(shape):
    """Footprint of a solid on the XY plane."""
    bb = shape.BoundBox
    sec = shape.slice(V(0, 0, 1), (bb.ZMin + bb.ZMax) / 2)
    return union([Part.Face(w) for w in sec]) if sec else None


name = text(NAME, NAME_SIZE, *NAME_AT)
keep = [grow(f, HALO) for f in pickups + slides]
keep += [disc(c['cx'], c['cy'], 3.5 + HALO) for c in guard_screws] + [disc(c['cx'], c['cy'], 3.0 + HALO) for c in switch_screws]
keep += [disc(cx, cy, HOLE_R + 0.9 + HALO)]
if MOUNT == 'standoff':
    keep += [disc(cx + SCREW_R * math.cos(math.radians(a + POD_ROT)), cy + SCREW_R * math.sin(math.radians(a + POD_ROT)), 2.2 + HALO)
             for a in (90, 210, 330)]
    for x, y in HOLES:
        hx, hy = bpt(x, y)
        keep.append(disc(hx, hy, 2.2 + HALO))
board_keep = [kbox(167.4, 118.0, 189.8, 137.7), krrect(*USB_GROOVE, 3.0), krrect(m[0] - 1.5, m[1] - 1.5, m[2] + 1.1, m[3] + 1.5, 2.0),
              krrect(ENC_BODY[0] - 0.5, ENC_BODY[1] - 0.5, ENC_BODY[2] + 0.5, ENC_BODY[3] + 0.5, 1.5), kdisc(*BUTTON, BTN_HOLE_R)]
for f in board_keep:
    g = grow(f, HALO)
    g.Placement = BOARD.multiply(g.Placement)
    keep.append(g)
keep.append(grow(name, HALO * 0.8))
keep = union(keep)
field = outline.makeOffset2D(-MARGIN, 0, False, False, False).cut(keep)
grooves = rings(POD_C, HOLE_R + 0.9 + HALO + 1.5, field.common(half_plane(POD_C, RECEIVER, SEAM)))
grooves += rings(RECEIVER, 3.0, field.common(half_plane(RECEIVER, POD_C, SEAM)))
art = Part.makeCompound(grooves + name.Faces)
art.translate(V(0, 0, T - ENGRAVE))
art_tool = art.extrude(V(0, 0, ENGRAVE + 0.5))
plate = plate.cut(art_tool).removeSplitter()

# ---------------------------------------------------------------- purchased parts (simple models)
ring = cyl(0, 0, RING_OD / 2, Z_RING, Z_RING + 0.9).cut(cyl(0, 0, RING_ID / 2, Z_RING - 1, 1))
leds = []
for i in range(16):
    led = Part.makeBox(5.0, 5.0, 1.6, V(-2.5, -2.5, Z_RING + 0.9))
    led.translate(V(LED_R, 0, 0))
    led.rotate(V(0, 0, 0), V(0, 0, 1), i * 22.5 + 11.25)
    leds.append(led)
tofb = Part.makeBox(TOF_W, TOF_W, TOF_T, V(-TOF_W / 2, -TOF_W / 2, Z_TOF_TOP - TOF_T))
for sx in (-1, 1):
    for sy in (-1, 1):
        tofb = tofb.cut(cyl(sx * TOF_HOLE, sy * TOF_HOLE, 1.65, Z_TOF_TOP - 3, 1))
sensor = Part.makeBox(4.4, 2.4, 1.0, V(-2.2, -1.2, Z_TOF_TOP))
qwiic = union([Part.makeBox(4.3, 6.0, 2.9, V(sx * (TOF_W / 2 - 2.6) - 2.15, -3.0, Z_TOF_TOP)) for sx in (-1, 1)])

# ---------------------------------------------------------------- objects
plies = []
for i, (z0, z1) in enumerate(((Z_PCB_BOT - 5, Z_MID), (Z_MID, Z_TOP), (Z_TOP, T + 1))):
    plies.append(static(('PlyBottom', 'PlyMiddle', 'PlyTop')[i], plate.common(Part.makeBox(400, 400, z1 - z0, V(-100, -100, z0)))))
diffuser_o = static('Diffuser', diffuser)
eye_o2 = static('ToF_Eye', eye_o)
bezel_o = static('OLED_Bezel', bezel)
knob_o = static('Knob', knob_o_shape)
cap_o = static('ButtonCap', cap_shape)
ring_o = static('NeoPixel_Ring16', on_pod(ring))
leds_o = static('NeoPixel_LEDs', on_pod(union(leds)))
tof_o = static('ToF_Board', on_pod(tofb))
sensor_o = static('ToF_Sensor', on_pod(sensor.fuse(qwiic)))
# The two pickups (cover size, measured position) and the measured cavity opening, for checks.
covers = [prism(obround(p['cx'], p['cy'], PICKUP_COVER[0], PICKUP_COVER[1], p['angle']), -18.0, T + 8.0)
          for p in MEAS['pickups'].values()]
pickups_o = static('Pickups_Measured', union(covers))
cav_wire = Part.makePolygon([V(x, y, 0.02) for x, y in MEAS['cavity']] + [V(*MEAS['cavity'][0], 0.02)])
cavity_o = static('BoardCavity_Measured', cav_wire)
printed = [*plies, diffuser_o, eye_o2, bezel_o, knob_o, cap_o]

board_root = None
if BOARD_STEP and os.path.exists(BOARD_STEP):
    before = set(doc.Objects)
    if GUI:
        import ImportGui
        ImportGui.insert(BOARD_STEP, doc.Name)
    else:
        Import.insert(BOARD_STEP, doc.Name)
    new = [o for o in doc.Objects if o not in before]
    roots = [o for o in new if not any(p in new for p in o.InList)]
    board_root = roots[0]
    board_root.Label = 'REINS guitar board'
    board_root.Placement = App.Placement(V(TX, TY, Z_PCB_BOT), App.Rotation(V(0, 0, 1), THETA))
    group.addObject(board_root)
else:
    print('BOARD_STEP not set: no board in the assembly')
doc.recompute()

# ---------------------------------------------------------------- checks
def parts_of(root):
    if root is None:
        return []
    return [(c.Label, Part.getShape(root, c.Name + '.')) for c in root.Group if hasattr(c, 'Shape')]


print('Interference (mm3, should be empty):')
others = parts_of(board_root) + [(o.Label, o.Shape) for o in (ring_o, leds_o, tof_o, sensor_o, pickups_o)]
bad = 0
for p in printed:
    for label, shp in others:
        if shp.isNull():
            continue
        v = p.Shape.common(shp).Volume
        if v > 0.01:
            print(f'  {p.Label} x {label}: {v:.2f}'); bad += 1
for i in range(len(printed)):
    for j in range(i + 1, len(printed)):
        v = printed[i].Shape.common(printed[j].Shape).Volume
        if v > 0.01:
            print(f'  {printed[i].Label} x {printed[j].Label}: {v:.2f}'); bad += 1
print('  none' if not bad else f'  {bad} contacts')

# Lift-off check: with the XIAO and the OLED module unplugged and the four M2 screws out,
# nothing of the board may stand above the plate underside inside the plate footprint.
footprint = prism(Part.makeFace(plate.slice(V(0, 0, 1), 0.8), 'Part::FaceMakerBullseye'), 0.0, 40.0)
blockers = []
for label, shp in parts_of(board_root):
    if 'XIAO' in label or 'OLED' in label or shp.isNull():
        continue
    if shp.common(footprint).Volume > 0.01:
        blockers.append(label)
print('Board parts in the way of lifting the plate:', blockers or 'none')

# The board outline against the measured cavity opening (the trace is good to about 1 mm).
cav_face = Part.Face(cav_wire)
pcb_fp = on_board(prism(krrect(121.19, 112.12, 189.26, 142.47, 4.0), -0.5, 0.5))
outside = pcb_fp.cut(prism(cav_face, -1, 1))
worst = 0.0
for v in pcb_fp.Vertexes:
    if not cav_face.isInside(V(v.Point.x, v.Point.y, 0.02), 0.001, True):
        worst = max(worst, cav_wire.distToShape(Part.Vertex(V(v.Point.x, v.Point.y, 0.02)))[0])
print(f'Board outside the traced cavity: {outside.Volume:.1f} mm3, worst corner {worst:.2f} mm')

# ---------------------------------------------------------------- looks and files
CHAR, ORANGE = (0.20, 0.21, 0.23), (0.95, 0.42, 0.08)
if GUI:
    looks = [(plies[0], CHAR, 0), (plies[1], ORANGE, 0), (plies[2], CHAR, 0), (diffuser_o, (0.97, 0.96, 0.92), 35),
             (eye_o2, (0.08, 0.08, 0.09), 0), (bezel_o, CHAR, 0), (knob_o, ORANGE, 0), (cap_o, ORANGE, 0),
             (ring_o, (0.05, 0.05, 0.05), 0), (leds_o, (1.0, 0.85, 0.55), 0), (tof_o, (0.75, 0.10, 0.10), 0),
             (sensor_o, (0.1, 0.1, 0.1), 0), (pickups_o, (0.06, 0.06, 0.06), 0)]
    cavity_o.ViewObject.LineColor = (0.0, 0.8, 0.9)
    cavity_o.ViewObject.LineWidth = 2
    for o, col, tr in looks:
        o.ViewObject.ShapeColor = col
        o.ViewObject.Transparency = tr
        o.ViewObject.Deviation = 0.05          # finer display mesh: the plies have many holes

os.makedirs(os.path.join(HERE, 'stl'), exist_ok=True)
whole = static('Pickguard', plate)
if GUI:
    whole.ViewObject.Visibility = False
import MeshPart


def export_stl(obj, path):
    m = MeshPart.meshFromShape(Shape=obj.Shape, LinearDeflection=0.02, AngularDeflection=0.2, Relative=False)
    m.removeDuplicatedFacets()
    m.removeDuplicatedPoints()
    m.harmonizeNormals()
    m.write(path)


for o, n in ((whole, 'pickguard'), (plies[0], 'pickguard-ply1-bottom'), (plies[1], 'pickguard-ply2-middle'),
             (plies[2], 'pickguard-ply3-top'), (diffuser_o, 'ring-diffuser'), (eye_o2, 'tof-eye'),
             (bezel_o, 'oled-bezel'), (knob_o, 'knob'), (cap_o, 'button-cap')):
    export_stl(o, os.path.join(HERE, 'stl', f'reins-{n}.stl'))

# Flat cut file for a laser cut plate: the through cuts of the plate at 1 mm height, plus
# 2.4 mm holes for standoffs at the board holes and the eye screws.
def write_dxf(face, path):
    lines = ['0', 'SECTION', '2', 'ENTITIES']
    for w in face.Wires:
        pts = []
        for e in w.OrderedEdges:
            q = e.discretize(Deflection=0.02)
            pts += q if not pts else q[1:]
        lines += ['0', 'LWPOLYLINE', '8', 'CUT', '90', str(len(pts)), '70', '1']
        for q in pts:
            lines += ['10', f'{q.x:.4f}', '20', f'{q.y:.4f}']
    lines += ['0', 'ENDSEC', '0', 'EOF']
    open(path, 'w').write('\n'.join(lines) + '\n')


flat = Part.makeFace(plate.slice(V(0, 0, 1), 1.0), 'Part::FaceMakerBullseye')
extra = [disc(*bpt(x, y), 1.2) for x, y in HOLES]
extra += [disc(cx + SCREW_R * math.cos(math.radians(a + POD_ROT)), cy + SCREW_R * math.sin(math.radians(a + POD_ROT)), 1.2)
          for a in (90, 210, 330)]
flat = flat.cut(union(extra)).Faces[0]
write_dxf(flat, os.path.join(HERE, 'reins-pickguard-flat.dxf'))

# Fit test: a 0.6 mm sheet with every hole and opening at its real size, and a hollow dummy of
# the board under it. Lay it on the guitar: the screws must go in, the pickups must pass, and
# the dummy must drop into the board cavity.
ft = prism(outline, 0, 0.6)
ft = ft.cut(union([prism(f, -1, 2) for f in pickups + slides] +
                  [cyl(c['cx'], c['cy'], 1.75, -1, 2) for c in guard_screws] +
                  [cyl(c['cx'], c['cy'], 1.5, -1, 2) for c in switch_screws] +
                  [cyl(cx, cy, HOLE_R, -1, 2)]))
dummy = prism(krrect(121.19, 112.12, 189.26, 142.47, 4.0), -8.0, 0.01).cut(prism(krrect(122.39, 113.32, 188.06, 141.27, 2.8), -9.0, -0.5))
ft = ft.fuse(on_board(dummy)).removeSplitter()
fit_o = static('FitTest', ft)
if GUI:
    fit_o.ViewObject.Visibility = False
export_stl(fit_o, os.path.join(HERE, 'stl', 'reins-fit-test.stl'))

assembly = printed + [ring_o, leds_o, tof_o, sensor_o] + ([board_root] if board_root else [])
if GUI:
    import ImportGui
    ImportGui.export(assembly, os.path.join(HERE, 'reins-pickguard-assembly.step'))
else:
    Import.export(assembly, os.path.join(HERE, 'reins-pickguard-assembly.step'))

# The old milled board model sits where the REINS board now is: hide it.
oldb = doc.getObject('finalMidiGuitar_1')
if oldb is not None and GUI:
    oldb.ViewObject.Visibility = False
doc.recompute()
if doc.FileName:
    doc.save()
else:
    doc.saveAs(os.path.join(HERE, 'reins-pickguard.FCStd'))
print('saved', doc.FileName)

if GUI:
    import time
    import FreeCADGui as Gui
    Gui.updateGui()
    gdoc = Gui.getDocument(doc.Name)
    view = gdoc.activeView()
    view.setCameraType('Orthographic')
    out = os.path.join(HERE, 'renders')
    os.makedirs(out, exist_ok=True)
    mine = set([group.Name] + [o.Name for o in group.OutListRecursive])
    shown = {o.Name: o.ViewObject.Visibility for o in doc.Objects if hasattr(o, 'ViewObject') and o.ViewObject}
    for o in doc.Objects:
        if o.Name not in mine and hasattr(o, 'ViewObject') and o.ViewObject and o.TypeId != 'App::Part':
            o.ViewObject.Visibility = False
    whole.ViewObject.Visibility = False

    def shot(fname, setup, w=1600, h=1400):
        path = os.path.join(out, fname)
        for _ in range(6):
            setup()
            view.fitAll()
            for _ in range(5):
                Gui.updateGui(); time.sleep(0.1)
            view.saveImage(path, w, h, 'White')     # the first image can carry stale clipping planes
            view.fitAll()
            for _ in range(5):
                Gui.updateGui(); time.sleep(0.1)
            view.saveImage(path, w, h, 'White')
            if os.path.getsize(path) > 15000:
                return
        print('render failed:', fname)

    shot('pickguard-top.png', view.viewTop)
    shot('pickguard-iso.png', view.viewIsometric)
    shot('pickguard-under.png', view.viewBottom)
    moves = [(diffuser_o, 30), (eye_o2, -30), (ring_o, 10), (leds_o, 10), (tof_o, -45), (sensor_o, -45),
             (bezel_o, 30), (knob_o, 45), (cap_o, 25)] + ([(board_root, -35)] if board_root else [])
    saved = [(o, App.Placement(o.Placement)) for o, _ in moves]
    for o, dz in moves:
        pl = App.Placement(o.Placement)
        pl.move(V(0, 0, dz))
        o.Placement = pl
    shot('pickguard-exploded.png', view.viewIsometric, 1600, 1600)
    for o, pl in saved:
        o.Placement = pl
    photo = doc.getObject('IMG_2407_Background_Removed')
    if photo is not None:
        photo.ViewObject.Visibility = True
        for prop, val in (('Lighting', 'Two side'), ('ShapeColor', (1.0, 1.0, 1.0))):
            if hasattr(photo.ViewObject, prop):
                try:
                    setattr(photo.ViewObject, prop, val)
                except Exception:
                    pass
        print('photo view properties:', photo.ViewObject.PropertiesList)
        shot('pickguard-on-guitar.png', view.viewTop, 1300, 1700)
        photo.ViewObject.Visibility = False
    for name_, vis in shown.items():
        o = doc.getObject(name_)
        if o is not None and o.ViewObject:
            o.ViewObject.Visibility = vis
    if oldb is not None:
        oldb.ViewObject.Visibility = False
    whole.ViewObject.Visibility = False
    view.viewIsometric(); view.fitAll()
    doc.save()
print('done')
if os.environ.get('FC_EXIT'):
    os._exit(0)
