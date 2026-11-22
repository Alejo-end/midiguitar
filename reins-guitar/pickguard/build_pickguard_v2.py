# REINS pickguard, version 2, for the owner's Squier Mustang. Writes into v2/.
#
# What it is (see README.md, "Version 2"):
#   Plate         3-ply pickguard (charcoal, orange, charcoal), printed top face down.
#   ControlFrame  one charcoal part on the plate over the REINS board: a 1.2 mm plinth with the
#                 OLED bezel at one end and the XIAO hood (USB-C port) at the other. Held by the
#                 four board screws from below, so no screw head shows on top.
#   Knob          encoder knob; its seat follows ENC_SHAFT (one parameter).
#   ButtonCap     mute button cap, captive: its flange sits under the plate.
#   Pod           black: iris with the ToF aperture, LED ring seat, ToF board mount, wire slot and tie bar.
#   Diffuser      white or natural: annulus over the ring with a 2 mm air gap, captive flange.
#   BatteryCradle sleeve for a 1S LiPo under the ToF board, hung on the pod screws.
#   Bought parts as models for the checks: board (KiCad STEP), ring (KiCad STEP), ToF board,
#   PEC12R, B3SN, EG1201 slide switch, LiPo, USB-C plug, pickups, stock toggle.
#
# Frame: X, Y of Sketch001 of cad-midiguitar.FCStd (X to the neck, Y to the bass side), Z = 0 on
# the body top (the underside of the plate), Z up. The same frame as build_pickguard.py.
#
# Run (from this folder):
#   /Applications/FreeCAD.app/Contents/Resources/bin/freecadcmd -c "exec(open('build_pickguard_v2.py').read())"
#   FC_EXIT=1 /Applications/FreeCAD.app/Contents/MacOS/FreeCAD build_pickguard_v2.py     (adds colours, renders)
# Parameters can also come from the environment, for example ENC_SHAFT=20 XIAO_STACK=10.6.

import json
import math
import os
import re
import subprocess

import FreeCAD as App
import Import
import MeshPart
import Part

V = App.Vector
try:
    HERE = os.path.dirname(os.path.abspath(__file__))
except NameError:
    HERE = os.getcwd()
OUT = os.path.join(HERE, 'v2')
REINS = os.path.abspath(os.path.join(HERE, '..'))
RING_DIR = os.path.abspath(os.path.join(HERE, '..', '..', 'reins-ring'))
KCLI = '/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli'
GUI = App.GuiUp
for d in ('', 'stl', 'web', 'web/boards', 'renders', 'boards', 'print', 'ref'):
    os.makedirs(os.path.join(OUT, d), exist_ok=True)
LOG = []


def log(*a):
    s = ' '.join(str(x) for x in a)
    print(s, flush=True)
    LOG.append(s)


def env(name, default):
    v = os.environ.get(name)
    return type(default)(v) if v not in (None, '') else default


# ================================================================ parameters
# ---- the stack. MEASURE THESE TWO with calipers before you print the plate:
# XIAO_STACK: from the REINS PCB top face to the underside of the XIAO PCB, with the XIAO fully
#   pushed into its two 8.5 mm SMD sockets (hanxia C46635843). Derived value: 8.5 socket + 2.54
#   header plastic + about 0.06 solder = 11.10. The KiCad model shows 9.13 (6.6 mm sockets).
# OLED_STACK: the same for the OLED module on J4 (Harwin M20-7820446, 8.50 mm) = 8.50 + 2.54.
XIAO_STACK = env('XIAO_STACK', 11.10)
OLED_STACK = env('OLED_STACK', 11.04)
XIAO_LIFT = env('XIAO_LIFT', 0.0)          # XIAO underside above the plate top
# ---- the encoder: ONE parameter, the shaft length of the PEC12R-4xxxF from its mounting face.
# Bourns PEC12R drawing: 15 mm shaft -> flat 5 mm (from 10), collar to 6.1 + 2.0;
# 17.5 to 30 mm shafts -> flat from 13 mm, collar to 6.1 + 5.0. Default PEC12R-4215F-S0024.
ENC_SHAFT = env('ENC_SHAFT', 15.0)
# ---- plate
PLY = (1.6, 0.6, 0.8)                      # bottom, middle, top ply, printed upside down
T = sum(PLY)
Z_ORANGE = (PLY[0], PLY[0] + PLY[1])        # orange band, Z 1.6 .. 2.2
BEVEL = 1.4
CSK = 90.0
GUARD_HOLE, GUARD_CSK = 3.5, 6.6           # stock #4 oval head pickguard screws
PU_HOLE = 3.4                              # pickup screws (pan heads, no countersink)
# ---- pickups: covers measured on IMG_2407 (rectified): 70 x 18 mm, parallel to their screws,
# centred between them. Openings: 0.3 mm per end, 0.6 mm per side.
PU_COVER = (70.0, 18.0)
PU_OPEN = (PU_COVER[0] + 0.6, PU_COVER[1] + 1.2)
# ---- pickup selector: the stock 3-way mini toggle (1/4 in bushing) moves over the pickup
# cavity, upper bass side between the pickup ends. None = no hole.
TOGGLE = (64.0, 153.5) if os.environ.get('TOGGLE', '1') != '0' else None
TOGGLE_HOLE = 6.6
# ---- battery switch for J5: E-Switch EG1201 (SPDT slide, ears 19.0 with 2 x 2.3 holes at
# 15.5, body 11.1 x 5.8 x 5.0, actuator 3 x 3, 4.0 above the body, travel 3.0).
SWITCH = (96.0, 81.0, 0.0)                 # x, y, angle of the slide axis (deg)
SW_BODY = (11.1, 5.8, 5.0)
SW_EARS, SW_EAR_W = 15.5, 5.0
SW_ACT, SW_ACT_H, SW_TRAVEL = 3.0, 4.0, 3.0
# ---- battery: 1S LiPo 500 mAh class, Adafruit 1578 is 36 x 29 x 4.75 mm, JST PH lead.
BATTERY = (36.0, 29.0, 4.75) if os.environ.get('BATTERY', '1') != '0' else None
# ---- the pod (ToF + ring)
POD_C = (81.312, 117.374)                  # ring centre of Sketch001
POD_ROT = -5.567                           # ToF board turn
TOF_W, TOF_T = 25.4, 1.6                   # SparkFun SEN-14722 (VL53L1X), 25.4 mm square
TOF_HOLES = [(-10.16, 10.16), (10.16, 10.16)]   # only two holes, 3.3 mm, on one edge
TOF_PADS = [(-10.16, -10.16), (10.16, -10.16)]  # the other edge rests on two pads
SENS = (4.9, 2.5, 1.56)                    # VL53L1X package
APT = [(+1.52, 0.42, 36.5), (-1.48, 0.54, 39.6)]  # emitter, receiver: x, radius at top, cone deg
FOV_HALF = 22.0                            # cone of the pod aperture (receiver 19.8 + margin)
APT_R0 = 2.7                               # aperture radius at the sensor top
RING_OD, RING_ID, RING_T, LED_H = 44.4, 31.75, 0.8, 1.6
DIF_T, AIR = 1.6, 2.0                      # diffuser thickness, air gap LED top to diffuser
Z_DIF0 = T - DIF_T
Z_LED_TOP = Z_DIF0 - AIR
Z_RING_TOP = Z_LED_TOP - LED_H
Z_RING_BOT = Z_RING_TOP - RING_T
GROOVE_D = 1.2                             # room for the 0603 caps and wire joints
# ---- wires. The ring has three round pads on its back, in a radial row: G at r 17.2, D at 19.0,
# 5 (5V) at 20.8 (reins-ring/make_ring.py). Each wire is soldered straight down onto its pad,
# drops through the pod slot, turns outward under the pod base and passes under a tie bar.
WIRE_OD = 1.3                              # 26 AWG silicone (the PH crimp takes 0.9 to 1.5 mm)
RING_PADS = (('G', 17.2, -2.4), ('D', 19.0, -0.8), ('5V', 20.8, 0.8))   # name, radius, side offset under the base
TIE_R = (23.0, 25.0)                       # tie bar, radial extent (pod slot frame)
TIE_GAP = (9.2, 2.8)                       # passage under the base: width, height
TIE_POST, TIE_BAR = 1.2, 1.2
Z_SEAT = Z_RING_BOT                        # ring rests here
Z_BASE1 = Z_RING_BOT - GROOVE_D            # base top (groove floor)
Z_BASE0 = Z_BASE1 - 1.2                    # base bottom
QWIIC_H = 2.9
Z_TOF_TOP = Z_BASE0 - QWIIC_H - 0.3
Z_SENS = Z_TOF_TOP + SENS[2]
IRIS_R = 15.55                             # ring bore 15.875
HOLE_R = 22.9                              # plate hole for the diffuser
DIF_R = 22.75
FL_R, FL_T = 25.2, 1.2                     # diffuser flange under the plate
POD_R = 25.6
EAR_R, EAR_A = 28.6, (90, 210, 330)        # pod screws, pod frame angles
Z_EAR = (-FL_T - 2.0, -FL_T)
# ---- control frame
PLINTH_H = 1.2
HOOD_WALL, HOOD_GAP = 1.4, 0.5
USB_PLUG = (12.2, 6.6)                     # USB-C cable overmould envelope, w x h
KNOB_R, KNOB_GAP, KNOB_H = 6.2, 1.0, 9.0
BTN_STEM_R, BTN_PROUD = 3.3, 1.4
MOUNT_PILOT = 0.85                         # M2 self tapping pilot radius
CHAR, ORANGE, BLACK, WHITE = (0.17, 0.18, 0.20), (0.94, 0.42, 0.08), (0.05, 0.05, 0.055), (0.95, 0.94, 0.90)

# ---- derived stack
Z_PCB_TOP = T + XIAO_LIFT - XIAO_STACK
PCB_T_STEP = 1.51                          # KiCad STEP board body
Z_PCB_BOT = Z_PCB_TOP - 1.6
Z_XIAO0 = Z_PCB_TOP + XIAO_STACK
XIAO_PCB = 1.2
Z_XIAO_TOP = Z_XIAO0 + XIAO_PCB + 3.21     # USB-C receptacle top (Seeed model)
Z_USB = Z_XIAO0 + XIAO_PCB + 1.6
Z_OLED0 = max(T + 0.01, Z_PCB_TOP + OLED_STACK)   # module rests on the plate if it is not higher
Z_GLASS = Z_OLED0 + 1.2 + 1.3              # module pcb 1.2, glass 1.3 (model)
ENC_FLAT0 = 10.0 if ENC_SHAFT < 17.5 else 13.0
ENC_COLLAR = 6.1 + (2.0 if ENC_SHAFT < 17.5 else 5.0)
Z_ENC_TOP = Z_PCB_TOP + ENC_SHAFT
Z_ENC_FLAT = Z_PCB_TOP + ENC_FLAT0
Z_ENC_COLLAR = Z_PCB_TOP + ENC_COLLAR
Z_BTN = Z_PCB_TOP + 3.1                     # B3SN-3012 actuator top
Z_FRAME = T + PLINTH_H
Z_HOOD = max(Z_XIAO_TOP + HOOD_GAP + 1.2, Z_USB + USB_PLUG[1] / 2 + 0.2 + 1.0)
Z_BEZEL = Z_GLASS + 0.3 + 1.6
log(f'stack: PCB top {Z_PCB_TOP:.2f}, XIAO underside {Z_XIAO0:.2f}, XIAO top {Z_XIAO_TOP:.2f}, USB axis {Z_USB:.2f}, '
    f'OLED module underside {Z_OLED0:.2f} (stack would put it at {Z_PCB_TOP + OLED_STACK:.2f}), glass top {Z_GLASS:.2f}')
log(f'encoder PEC12R shaft {ENC_SHAFT} mm: collar top {Z_ENC_COLLAR:.2f}, flat {Z_ENC_FLAT:.2f}..{Z_ENC_TOP:.2f}; '
    f'hood top {Z_HOOD:.2f}, bezel top {Z_BEZEL:.2f}, plinth top {Z_FRAME:.2f}')
log(f'pod: ring {Z_RING_BOT:.2f}..{Z_RING_TOP:.2f}, LED top {Z_LED_TOP:.2f}, diffuser {Z_DIF0:.2f}..{T:.2f}, '
    f'base {Z_BASE0:.2f}..{Z_BASE1:.2f}, ToF top {Z_TOF_TOP:.2f}, sensor top {Z_SENS:.2f}')

# ================================================================ reference data
REF = json.load(open(os.path.join(HERE, 'ref', 'sketch001.json')))
MEAS = json.load(open(os.path.join(HERE, 'ref', 'measured.json')))
SCAN = json.load(open(os.path.join(HERE, 'ref', 'scan_outline.json')))
POOL = json.load(open(os.path.join(OUT, 'ref', 'pickup_cavity.json')))

# board pose (same as version 1)
_bp = MEAS['board_pose']
_bc = (155.225, 127.295)
THETA = 180.0 + _bp['angle']
_t = math.radians(THETA)
TX = _bp['cx'] - (_bc[0] * math.cos(_t) + _bc[1] * math.sin(_t))
TY = _bp['cy'] - (_bc[0] * math.sin(_t) - _bc[1] * math.cos(_t))
BOARD = App.Placement(V(TX, TY, 0), App.Rotation(V(0, 0, 1), THETA))
POD = App.Placement(V(POD_C[0], POD_C[1], 0), App.Rotation(V(0, 0, 1), POD_ROT))


# ================================================================ helpers
def poly(pts, z=0.0):
    p = [V(x, y, z) for x, y in pts]
    return Part.Face(Part.makePolygon(p + [p[0]]))


def disc(x, y, r, z=0.0):
    return Part.Face(Part.Wire(Part.makeCircle(r, V(x, y, z))))


def rrect(cx, cy, w, h, r, z=0.0):
    x0, x1, y0, y1 = cx - w / 2, cx + w / 2, cy - h / 2, cy + h / 2
    r = min(r, w / 2 - 1e-3, h / 2 - 1e-3)
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


def krr(x0, y0, x1, y1, r, z=0.0):
    """Rounded rectangle between KiCad corners (board frame, y down)."""
    return rrect((x0 + x1) / 2, -(y0 + y1) / 2, x1 - x0, y1 - y0, r, z)


def obround(cx, cy, length, width, angle, z=0.0):
    f = rrect(0, 0, length, width, width / 2 - 1e-3, z)
    f.rotate(V(0, 0, z), V(0, 0, 1), angle)
    f.translate(V(cx, cy, 0))
    return f


def prism(face, z0, z1):
    f = face.copy()
    f.translate(V(0, 0, z0 - f.Vertexes[0].Point.z))
    return f.extrude(V(0, 0, z1 - z0))


def cyl(x, y, r, z0, z1):
    return Part.makeCylinder(r, z1 - z0, V(x, y, z0))


def cone(x, y, r0, r1, z0, z1):
    return Part.makeCone(r0, r1, z1 - z0, V(x, y, z0))


def revolve(prof, x=0.0, y=0.0):
    pts = [V(x + r, y, z) for r, z in prof]
    s = Part.Face(Part.makePolygon(pts + [pts[0]])).revolve(V(x, y, 0), V(0, 0, 1), 360)
    return Part.Solid(s) if s.ShapeType != 'Solid' else s


def fuse(shapes):
    s = shapes[0]
    return s.fuse(shapes[1:]).removeSplitter() if len(shapes) > 1 else s


def placed(shape, pl):
    s = shape.copy()
    s.Placement = pl.multiply(s.Placement)
    return s


def on_board(shape):
    return placed(shape, BOARD)


def on_pod(shape):
    return placed(shape, POD)


def bpt(kx, ky):
    v = BOARD.multVec(V(kx, -ky, 0))
    return v.x, v.y


def ppt(x, y):
    v = POD.multVec(V(x, y, 0))
    return v.x, v.y


def loft_cut(face_lo, face_hi):
    return Part.makeLoft([face_lo.OuterWire, face_hi.OuterWire], True, True)


def chamfer_top(face, z_top, c):
    """Cutter: 45 degree chamfer of depth c around an opening face, at the top surface z_top."""
    lo = face.copy(); lo.translate(V(0, 0, z_top - c - lo.Vertexes[0].Point.z))
    hi = face.makeOffset2D(c + 0.01, 2, False, False, False)
    hi.translate(V(0, 0, z_top + 0.01 - hi.Vertexes[0].Point.z))
    return loft_cut(lo, hi)


def overlap(a, b):
    if not a.BoundBox.intersect(b.BoundBox):
        return 0.0
    try:
        return a.common(b).Volume
    except Exception:
        return -1.0


def gap(a, b):
    try:
        return a.distToShape(b)[0]
    except Exception:
        return float('nan')


# ================================================================ boards from KiCad
def kicad_export(kind, pcb, out, extra):
    if not os.path.exists(out):
        cmd = [KCLI, 'pcb', 'export', kind, '--subst-models', '-f', '-o', out] + extra + [pcb]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return out


BOARD_STEP = kicad_export('step', os.path.join(REINS, 'reins-guitar.kicad_pcb'), os.path.join(OUT, 'boards', 'reins-guitar.step'),
                          ['--include-pads', '--include-silkscreen'])
RING_STEP = kicad_export('step', os.path.join(RING_DIR, 'reins-ring.kicad_pcb'), os.path.join(OUT, 'boards', 'reins-ring.step'),
                         ['--include-pads', '--include-silkscreen'])
for pcb, name in ((os.path.join(REINS, 'reins-guitar.kicad_pcb'), 'reins-guitar.glb'),
                  (os.path.join(RING_DIR, 'reins-ring.kicad_pcb'), 'reins-ring.glb')):
    kicad_export('glb', pcb, os.path.join(OUT, 'web', 'boards', name), ['--include-pads', '--include-silkscreen', '--include-soldermask'])

doc = App.newDocument('reins_pickguard_v2')


def insert_step(path, label):
    before = set(doc.Objects)
    if GUI:
        import ImportGui
        ImportGui.insert(path, doc.Name)
    else:
        Import.insert(path, doc.Name)
    new = [o for o in doc.Objects if o not in before]
    roots = [o for o in new if not any(p in new for p in o.InList)]
    root = roots[0]
    if len(roots) > 1:
        root = doc.addObject('App::Part', label)
        for o in roots:
            root.addObject(o)
    root.Label = label
    return root


def children(o):
    return [c for c in getattr(o, 'Group', []) if not c.TypeId.startswith('App::Origin')
            and c.TypeId not in ('App::Line', 'App::Plane', 'App::Point')]


def find(root, *labels):
    o = root
    for lab in labels:
        hits = [c for c in children(o) if c.Label.startswith(lab)]
        assert hits, f'{lab} not found under {o.Label}'
        o = hits[0]
    return o


def leaves(root):
    out = []

    def rec(o, p):
        kids = children(o)
        if not kids:
            s = Part.getShape(root, p)
            if not s.isNull() and s.Volume > 1e-6:
                out.append((o.Label, s))
        for c in kids:
            rec(c, p + c.Name + '.')
    for c in children(root):
        rec(c, c.Name + '.')
    return out


board_root = insert_step(BOARD_STEP, 'Board_Guitar')
ring_root = insert_step(RING_STEP, 'Board_Ring')
# Real heights: lift the XIAO and the OLED module to the measured stacks (the KiCad models put
# them 9.13 and 9.19 above the PCB top), and drop the KiCad encoder and button models, which
# are replaced by the parametric PEC12R and the real B3SN below.
xiao_c = find(board_root, 'Seeed_XIAO', 'Generic_InSocket')
for c in children(xiao_c):
    if not c.Label.startswith('01x0'):
        c.Placement = App.Placement(V(0, 0, XIAO_STACK - 9.13), App.Rotation()).multiply(c.Placement)
oled_c = find(board_root, 'SSD1306_OLED_Module', 'SSD1306_OLED_Module')
for c in children(oled_c):
    if c.Label != 'socket':
        c.Placement = App.Placement(V(0, 0, (Z_OLED0 - Z_PCB_TOP) - 9.19), App.Rotation()).multiply(c.Placement)
for lab in ('PEC12R', 'SW_SPST_B3S'):
    o = find(board_root, lab)
    for x in [o] + o.OutListRecursive:
        try:
            doc.removeObject(x.Name)
        except Exception:
            pass
board_root.Placement = BOARD.multiply(App.Placement(V(0, 0, Z_PCB_TOP - PCB_T_STEP), App.Rotation()))
doc.recompute()

# ring: KiCad centre (100, 100); its wire pads sit at KiCad +X (PAD_ANGLE 0). Turn the ring so
# the pads lie over the wire slot of the pod, which points to the body channel.
ch = MEAS['channel']
ch_mouth = (ch['x'], POOL['ymin'] + 0.5)
WIRE_ANG = math.degrees(math.atan2(ch_mouth[1] - POD_C[1], ch_mouth[0] - POD_C[0])) - POD_ROT
ring_root.Placement = POD.multiply(App.Placement(V(0, 0, Z_RING_BOT), App.Rotation(V(0, 0, 1), WIRE_ANG))).multiply(
    App.Placement(V(-100, 100, 0), App.Rotation()))
doc.recompute()
board_parts = leaves(board_root)
ring_parts = leaves(ring_root)
log(f'board placement: rotate {THETA:.3f} deg about Z, then move ({TX:.3f}, {TY:.3f}, {Z_PCB_TOP - PCB_T_STEP:.3f}); '
    f'wire slot angle in the pod frame {WIRE_ANG:.2f} deg')

# board features, KiCad coordinates
HOLES = {'H1': (166.47, 114.38), 'H2': (187.07, 115.76), 'H3': (180.98, 140.26), 'H4': (151.53, 140.6)}
BOARD_EDGE = (121.14, 112.07, 189.31, 142.52)
XIAO = (167.73, 118.10, 188.20, 137.64)    # XIAO PCB (model)
USB_FACE, USB_Y = 189.70, 127.87
XIAO_WIN = (167.3, 117.7, 188.6, 138.05)   # the XIAO passes the plate here
TP1 = (168.09, 118.42)
OLED_MOD = (122.22, 113.71, 150.02, 141.01)
OLED_GLASS = (126.82, 114.01, 146.08, 140.71)
OLED_ACTIVE = (128.42, 116.49, 139.28, 138.23)
OLED_SLOT = (122.40, 121.95, 125.55, 132.85)   # J4 socket 2.5 x 10.56 plus 0.3
OLED_HOLES = [(124.57, 116.19), (147.68, 116.19), (124.57, 138.54), (147.68, 138.54)]
ENC = (158.25, 128.79)
ENC_BODY = (151.25, 121.79, 165.25, 135.79)
BUTTON = (156.89, 117.51)

# ================================================================ plate
def turn(a, b, c):
    v1, v2 = (b[0] - a[0], b[1] - a[1]), (c[0] - b[0], c[1] - b[1])
    return math.degrees(math.atan2(v1[0] * v2[1] - v1[1] * v2[0], v1[0] * v2[0] + v1[1] * v2[1]))


def smooth_outline(pts, corner=35.0, tol=0.12):
    P = list(pts)
    changed = True
    while changed:
        changed = False
        for i in range(len(P)):
            a, b, c = P[i - 1], P[i], P[(i + 1) % len(P)]
            if math.dist(a, b) < 0.3 or abs(turn(a, b, c)) > 150:
                P.pop(i); changed = True
                break
    n = len(P)
    corners = [i for i in range(n) if abs(turn(P[i - 1], P[i], P[(i + 1) % n])) > corner] or [0]
    edges = []
    for k, i0 in enumerate(corners):
        i1 = corners[(k + 1) % len(corners)]
        idx = [(i0 + j) % n for j in range(((i1 - i0) % n or n) + 1)]
        chain = [V(*P[j]) for j in idx]
        if len(chain) <= 3:
            edges += [Part.LineSegment(chain[j], chain[j + 1]).toShape() for j in range(len(chain) - 1)]
            continue
        bs = Part.BSplineCurve()
        bs.approximate(Points=chain, DegMin=3, DegMax=3, Tolerance=tol)
        edges.append(bs.toShape())
    return Part.Face(Part.Wire(Part.__sortEdges__(edges)))


outline = smooth_outline(SCAN['outline'])
steps = int(round(BEVEL / 0.2))
plate = prism(outline, 0, T - BEVEL)
for i in range(steps):
    inset = outline.makeOffset2D(-(i + 0.5) * 0.2, 0, False, False, False)
    plate = plate.fuse(prism(inset, T - BEVEL + i * 0.2, T - BEVEL + (i + 1) * 0.2))
plate = plate.removeSplitter()

guard = [(h['cx'], h['cy']) for h in MEAS['guard_screws']]
pu_screws = [tuple(s) for p in MEAS['pickups'].values() for s in p['screws']]
PICKUPS = []
for name, p in MEAS['pickups'].items():
    (x0, y0), (x1, y1) = p['screws']
    PICKUPS.append(dict(name=name, cx=(x0 + x1) / 2, cy=(y0 + y1) / 2, angle=math.degrees(math.atan2(y0 - y1, x0 - x1))))
cuts, chamfers = [], []
for pk in PICKUPS:
    f = obround(pk['cx'], pk['cy'], PU_OPEN[0], PU_OPEN[1], pk['angle'])
    cuts.append(prism(f, -1, T + 1))
    chamfers.append(chamfer_top(f, T, 0.8))
for x, y in guard:
    cuts += [cyl(x, y, GUARD_HOLE / 2, -1, T + 1),
             cone(x, y, GUARD_HOLE / 2, GUARD_CSK / 2, T - (GUARD_CSK - GUARD_HOLE) / 2, T), cyl(x, y, GUARD_CSK / 2, T, T + 1)]
for x, y in pu_screws:
    cuts += [cyl(x, y, PU_HOLE / 2, -1, T + 1), cone(x, y, PU_HOLE / 2, PU_HOLE / 2 + 0.4, T - 0.4, T + 0.001)]
if TOGGLE:
    cuts += [cyl(*TOGGLE, TOGGLE_HOLE / 2, -1, T + 1)]
    chamfers.append(chamfer_top(disc(*TOGGLE, TOGGLE_HOLE / 2), T, 0.6))

# battery switch: slot for the actuator, two blind pilots for the ears
sx, sy, sa = SWITCH
slot_f = rrect(0, 0, SW_ACT + SW_TRAVEL + 0.4, SW_ACT + 0.4, 0.6)
slot_f.rotate(V(0, 0, 0), V(0, 0, 1), sa); slot_f.translate(V(sx, sy, 0))
cuts.append(prism(slot_f, -1, T + 1))
chamfers.append(chamfer_top(slot_f, T, 0.6))
SW_EAR_XY = [(sx + d * math.cos(math.radians(sa)), sy + d * math.sin(math.radians(sa))) for d in (-SW_EARS / 2, SW_EARS / 2)]
for x, y in SW_EAR_XY:
    cuts.append(cyl(x, y, MOUNT_PILOT, -1, T - 0.6))
# ON mark: a dot engraved through the top ply at the +slide end
on_xy = (sx + (SW_ACT + SW_TRAVEL) / 2 * math.cos(math.radians(sa)) + 3.4 * math.cos(math.radians(sa)),
         sy + (SW_ACT + SW_TRAVEL) / 2 * math.sin(math.radians(sa)) + 3.4 * math.sin(math.radians(sa)))
cuts.append(cyl(*on_xy, 0.7, T - PLY[2] - 0.05, T + 1))

# board: four separate printed spacers (tubes) between the board and the plate, instead of tall
# thin bosses growing off the plate. Clearance holes in the plate for the four M2 screws.
H_R = 2.2                                  # spacer outer radius (J5 pad 1 is 3.4 mm from H1)
SPACER_ID = 2.2
spacers_s = Part.makeCompound([on_board(cyl(x, -y, H_R, Z_PCB_TOP + 0.05, 0.0).cut(cyl(x, -y, SPACER_ID / 2, -20, 5)))
                               for x, y in HOLES.values()])
bcuts = [on_board(cyl(x, -y, 1.1, Z_PCB_TOP - 1, T + 1)) for x, y in HOLES.values()]
# XIAO window (the XIAO passes the plate), TP1 wire, OLED header slot, encoder, button
bcuts.append(on_board(prism(krr(*XIAO_WIN, 0.8).fuse(Part.Face(Part.Wire(Part.makeCircle(1.9, V(TP1[0], -TP1[1], 0))))).removeSplitter(), -1, T + 1)))
# the USB-C receptacle overhangs the XIAO by 1.5 mm: the window reaches under it, so the board can come out
bcuts.append(on_board(prism(krr(XIAO_WIN[2] - 1.0, USB_Y - 5.0, USB_FACE + 0.4, USB_Y + 5.0, 0.5), -1, T + 1)))
bcuts.append(on_board(prism(krr(*OLED_SLOT, 0.5), -1, T + 1)))
ENC_HOLE_R = 5.0                           # room for a 1.5 mm knob hub wall
bcuts.append(on_board(cyl(ENC[0], -ENC[1], ENC_HOLE_R, -1, T + 1)))
BTN_HOLE_R = BTN_STEM_R + 0.2
bcuts.append(on_board(cyl(BUTTON[0], -BUTTON[1], BTN_HOLE_R, -1, T + 1)))
for x, y in OLED_HOLES:          # the OLED module is held by the frame; these let M2 screws pass if wanted
    pass
# USB-C plug lane: the plug body lies 2.3 mm below the USB axis + clearance, into the orange ply
LANE_FLOOR = Z_ORANGE[0] + 0.3
lane = krr(USB_FACE + 0.0, USB_Y - USB_PLUG[0] / 2 - 0.4, USB_FACE + 34.0, USB_Y + USB_PLUG[0] / 2 + 0.4, 1.5)
bcuts.append(on_board(prism(lane, LANE_FLOOR, T + 1)))
plate = plate.cut(Part.makeCompound(bcuts)).removeSplitter()

# pod: plate hole with chamfer, three screw bosses under the plate (blind pilots)
pcut = [cyl(0, 0, HOLE_R, -1, T + 1)]
chamfers.append(on_pod(chamfer_top(disc(0, 0, HOLE_R), T, 0.8)))
pod_boss, pod_pilot = [], []
for a in EAR_A:
    x, y = EAR_R * math.cos(math.radians(a)), EAR_R * math.sin(math.radians(a))
    pod_boss.append(cyl(x, y, 2.6, Z_EAR[1] + 0.0, 0.01))
    pod_pilot.append(cyl(x, y, MOUNT_PILOT, Z_EAR[1] - 0.1, T - 0.6))
plate = plate.fuse(on_pod(fuse(pod_boss))).cut(on_pod(Part.makeCompound(pcut + pod_pilot))).removeSplitter()

# cable groove over the body channel, keeping a flat seat for the board spacers
gx, gy0, gy1, gw = ch['x'], ch['y0'], ch['y1'], ch['width']
groove = prism(rrect(gx, (gy0 + gy1) / 2, gw, gy1 - gy0, gw / 2 - 0.01), -1, 0.8)
groove = groove.cut(Part.makeCompound([on_board(cyl(x, -y, H_R + 0.6, -2, 2)) for x, y in HOLES.values()]))
plate = plate.cut(groove)
plate = plate.cut(Part.makeCompound(cuts)).cut(Part.makeCompound(chamfers)).removeSplitter()


# The logo, direction B "Two fields" (ref/logo/reins-b-lockup.svg, a copy of reins-site/public/logo/):
# two sensors, each with two rings, facing each other like )) ((, and the word REINS. It is cut
# through the top ply, so the orange ply shows. It sits in the calm field along the bass side edge,
# between the two pickups, centred on the ring (sensor) axis, along the strings. It reads upright
# for the audience, like the rest of the guitar.
# Widths in SVG units (measured on a raster of the outlines): mark strokes 6, mark gaps 4, letter
# strokes 4.5, gaps in and between the letters 5. At 0.38 mm per unit: mark strokes 2.3 mm, gaps
# 1.5 mm, letter strokes 1.7 mm, letter gaps 1.9 mm (the owner asked for 1.2 or more). Only the
# sharp inside corners of R and N are narrower. The lockup is 77 x 12 mm, the capitals 10 mm.
LOGO_SVG = os.path.join(HERE, 'ref', 'logo', 'reins-b-lockup.svg')
LOGO_S = env('LOGO_S', 0.38)
LOGO_AT = (POD_C[0], env('LOGO_Y', 167.5))


NUM = r'-?(?:\d+\.?\d*|\.\d+)(?:[eE]-?\d+)?'


def svg_subpaths(d, seg=12):
    """Absolute SVG path data to subpaths. Each is (points, closed, arcs): the points of the outline
    (lines, quadratic and cubic curves flattened) and the arcs as (x0, y0, r, large, sweep, x1, y1)."""
    toks = re.findall(r'[MLHVQCAZ]|' + NUM, d)
    subs, pts, arcs, i, cmd, p = [], [], [], 0, None, (0.0, 0.0)

    def num():
        nonlocal i
        i += 1
        return float(toks[i - 1])

    def flush(closed):
        nonlocal pts, arcs
        if len(pts) > 1 or arcs:
            subs.append((pts, closed, arcs))
        pts, arcs = [], []
    while i < len(toks):
        if toks[i] in 'MLHVQCAZ':
            cmd = toks[i]; i += 1
        if cmd == 'M':
            flush(False)
            p = (num(), num()); pts = [p]; cmd = 'L'
        elif cmd == 'L':
            p = (num(), num()); pts.append(p)
        elif cmd == 'H':
            p = (num(), p[1]); pts.append(p)
        elif cmd == 'V':
            p = (p[0], num()); pts.append(p)
        elif cmd == 'Q':
            c, e = (num(), num()), (num(), num())
            for k in range(1, seg + 1):
                t = k / seg
                pts.append(((1 - t) ** 2 * p[0] + 2 * (1 - t) * t * c[0] + t * t * e[0],
                            (1 - t) ** 2 * p[1] + 2 * (1 - t) * t * c[1] + t * t * e[1]))
            p = e
        elif cmd == 'C':
            c1, c2, e = (num(), num()), (num(), num()), (num(), num())
            for k in range(1, seg + 1):
                t = k / seg
                b = ((1 - t) ** 3, 3 * (1 - t) ** 2 * t, 3 * (1 - t) * t * t, t ** 3)
                pts.append(tuple(b[0] * p[j] + b[1] * c1[j] + b[2] * c2[j] + b[3] * e[j] for j in (0, 1)))
            p = e
        elif cmd == 'A':
            r, _, _, fa, fs, x1, y1 = num(), num(), num(), num(), num(), num(), num()
            arcs.append((p[0], p[1], r, int(fa), int(fs), x1, y1))
            p = (x1, y1); pts.append(p)
        elif cmd == 'Z':
            flush(True)
    flush(False)
    return subs


def simple_loop(pts):
    """Cut the small self loops off a closed polygon: where two edges cross, keep the larger side."""
    q = [p for i, p in enumerate(pts) if math.dist(p, pts[i - 1]) > 1e-7]

    def cross(p1, p2, p3, p4):
        d = (p2[0] - p1[0]) * (p4[1] - p3[1]) - (p2[1] - p1[1]) * (p4[0] - p3[0])
        if abs(d) < 1e-12:
            return None
        t = ((p3[0] - p1[0]) * (p4[1] - p3[1]) - (p3[1] - p1[1]) * (p4[0] - p3[0])) / d
        u = ((p3[0] - p1[0]) * (p2[1] - p1[1]) - (p3[1] - p1[1]) * (p2[0] - p1[0])) / d
        return (p1[0] + t * (p2[0] - p1[0]), p1[1] + t * (p2[1] - p1[1])) if 1e-9 < t < 1 - 1e-9 and 1e-9 < u < 1 - 1e-9 else None
    area = lambda r: abs(sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(r, r[1:] + r[:1]))) / 2
    changed = True
    while changed:
        changed = False
        n = len(q)
        for i in range(n):
            for j in range(i + 2, n):
                if i == 0 and j == n - 1:
                    continue
                x = cross(q[i], q[i + 1], q[j], q[(j + 1) % n])
                if x:
                    inner, outer = [x] + q[i + 1:j + 1], q[:i + 1] + [x] + q[j + 1:]
                    q = outer if area(inner) < area(outer) else inner
                    changed = True
                    break
            if changed:
                break
    return q


def logo_solids(path, scale, at, z0, z1):
    """The SVG logo as solids between z0 and z1: SVG point (x, y) -> at + scale * (x - cx, cy - y).
    Handles filled circles, filled paths (nonzero fill), stroked circular arcs with round caps
    and polygon clip paths on groups, which is what the REINS logo files use.
    The letter outlines of the lockup (Nunito ExtraBold) cross themselves in a few tiny loops
    (R, N). simple_loop() cuts those loops off, so every outline is a simple polygon."""
    svg = open(path).read()
    vb = [float(v) for v in re.search(r'viewBox="([^"]+)"', svg).group(1).replace(',', ' ').split()]
    cxu, cyu = vb[0] + vb[2] / 2, vb[1] + vb[3] / 2
    h = z1 - z0
    tr = lambda x, y: V(at[0] + scale * (x - cxu), at[1] - scale * (y - cyu), z0)

    def wire(pts):
        q = [pts[0]] + [b for a, b in zip(pts, pts[1:]) if math.dist(a, b) > 1e-6]
        if math.dist(q[0], q[-1]) < 1e-6:
            q.pop()
        vs = [tr(*x) for x in q]
        return Part.makePolygon(vs + [vs[0]])

    def disc(x, y, r):
        return Part.makeCylinder(r * scale, h, tr(x, y))

    def arc_band(x0, y0, r, fa, fs, x1, y1, sw):
        xp, yp = (x0 - x1) / 2, (y0 - y1) / 2
        k = math.sqrt(max((r * r - xp * xp - yp * yp) / max(xp * xp + yp * yp, 1e-12), 0.0)) * (1 if fa != fs else -1)
        cx, cy = k * yp + (x0 + x1) / 2, -k * xp + (y0 + y1) / 2
        a0, a1 = math.atan2(y0 - cy, x0 - cx), math.atan2(y1 - cy, x1 - cx)
        da = a1 - a0
        if fs and da < 0:
            da += 2 * math.pi
        if not fs and da > 0:
            da -= 2 * math.pi
        if abs(da) > 2 * math.pi - 0.02:           # a full ring
            return disc(cx, cy, r + sw / 2).cut(disc(cx, cy, r - sw / 2))
        n = max(16, int(abs(da) / (2 * math.pi) * 192))
        arc = lambda rr: [(cx + rr * math.cos(a0 + da * j / n), cy + rr * math.sin(a0 + da * j / n)) for j in range(n + 1)]
        band = Part.Face(wire(arc(r + sw / 2) + arc(r - sw / 2)[::-1])).extrude(V(0, 0, h))
        return band.fuse([disc(x0, y0, sw / 2), disc(x1, y1, sw / 2)]).removeSplitter()

    def element(tag, attrs):
        a = dict(re.findall(r'([\w-]+)="([^"]*)"', attrs))
        if tag == 'circle':
            return [disc(float(a['cx']), float(a['cy']), float(a['r']))] if a.get('fill', 'black') != 'none' else []
        if tag == 'polygon':
            pts = [float(v) for v in re.findall(NUM, a['points'])]
            return [Part.Face(wire(list(zip(pts[0::2], pts[1::2])))).extrude(V(0, 0, h))]
        if tag != 'path':
            return []
        subs = svg_subpaths(a['d'])
        if a.get('fill') == 'none':                 # stroked: circular arcs only
            sw = float(a['stroke-width'])
            return [arc_band(*arc, sw) for _, _, arcs in subs for arc in arcs]
        # SVG fills by the nonzero rule: loops turning like the largest loop fill, the others are holes
        loops = [simple_loop(pts) for pts, _, _ in subs if len(pts) > 2]
        if not loops:
            return []
        area = lambda q: sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(q, q[1:] + q[:1])) / 2
        sgn = math.copysign(1, area(max(loops, key=lambda q: abs(area(q)))))
        solid = lambda q: Part.Face(wire(q)).extrude(V(0, 0, h))
        fill = [solid(q) for q in loops if area(q) * sgn > 0]
        holes = [solid(q) for q in loops if area(q) * sgn < 0]
        body = fuse(fill)
        return [body.cut(fuse(holes)).removeSplitter() if holes else body]

    clips = {cid: element('polygon', body) for cid, body in re.findall(r'<clipPath id="([^"]+)">\s*<polygon([^>]*)/?>', svg)}
    out = []
    for cid, body in re.findall(r'<g clip-path="url\(#([^)]+)\)">(.*?)</g>', svg, re.S):
        for tag, attrs in re.findall(r'<(circle|path)\b([^>]*)>', body):
            out += [s.common(clips[cid][0]) for s in element(tag, attrs)]
    rest = re.sub(r'<defs>.*?</defs>|<g clip-path=.*?</g>', '', svg, flags=re.S)
    for tag, attrs in re.findall(r'<(circle|path)\b([^>]*)>', rest):
        out += element(tag, attrs)
    return [s for s in out if s.Volume > 1e-6]


logo = logo_solids(LOGO_SVG, LOGO_S, LOGO_AT, T - PLY[2] - 0.05, T + 1)
plate = plate.cut(Part.makeCompound(logo)).removeSplitter()
_lb = Part.makeCompound(logo).optimalBoundingBox()
log(f'logo: {os.path.basename(LOGO_SVG)} at {LOGO_S} mm per unit, {_lb.XLength:.1f} x {_lb.YLength:.1f} mm, '
    f'x {_lb.XMin:.1f}..{_lb.XMax:.1f}, y {_lb.YMin:.1f}..{_lb.YMax:.1f}, cut {PLY[2]:.1f} mm deep to the orange ply')
plate = plate.common(prism(outline, Z_PCB_TOP - 2, T + 1))
assert plate.isValid(), 'plate not valid'
log(f'plate: {len(plate.Solids)} solid(s), volume {plate.Volume / 1000:.1f} cm3, bbox {plate.BoundBox.XLength:.1f} x {plate.BoundBox.YLength:.1f}')

# ================================================================ control frame
# Plinth over the board (board outline + 0.8) and the bezel over the OLED end: one part, printed
# plinth down. The hood over the XIAO end is a separate part on the plinth, printed top down, so it
# has no ceiling to bridge. The four board screws come from below: H4 into the frame, H1 to H3
# through the plinth into the hood. So the plinth is clamped between the plate and the hood.
bx0, by0, bx1, by1 = BOARD_EDGE
plinth_f = krr(bx0 - 0.8, by0 - 0.8, USB_FACE + 1.6, by1 + 0.8, 3.0)
frame = prism(plinth_f, T, Z_FRAME)
frame = frame.makeChamfer(0.5, [e for e in frame.Edges if abs(e.BoundBox.ZMin - Z_FRAME) < 1e-6 and abs(e.BoundBox.ZMax - Z_FRAME) < 1e-6])
# bezel block
BZ = (bx0 - 0.8, by0 - 0.8, OLED_MOD[2] + 0.9, by1 + 0.8)
bez = prism(krr(*BZ, 3.0), T, Z_BEZEL)
# the bezel reaches round screw H4 (it was in the 1.2 mm plinth only), clear of the knob
top_ch = lambda sh, c, z: sh.makeChamfer(c, [e for e in sh.Edges if abs(e.BoundBox.ZMin - z) < 1e-6 and abs(e.BoundBox.ZMax - z) < 1e-6])
bez = top_ch(bez, 0.7, Z_BEZEL).fuse(top_ch(prism(krr(BZ[2] - 3.0, 136.6, 154.6, by1 + 0.8, 1.5), T, Z_BEZEL), 0.7, Z_BEZEL)).removeSplitter()
# hood block
HD = (XIAO[0] - 0.4 - HOOD_WALL - 0.4, by0 - 0.8, USB_FACE + 1.6, by1 + 0.8)
fr = frame.fuse([bez]).removeSplitter()
hood = top_ch(prism(krr(*HD, 3.0), Z_FRAME, Z_HOOD), 0.8, Z_HOOD).fuse(
    top_ch(prism(krr(HD[0] - 1.8, HD[1], HD[0] + 2.0, 117.2, 1.0), Z_FRAME, Z_HOOD), 0.8, Z_HOOD)).removeSplitter()   # wall round H1
fcuts = []
# OLED pocket: module + 0.3, up to the glass top + 0.3; header pins and parts; window drafted 35 deg
m = OLED_MOD
fcuts.append(prism(krr(m[0] - 0.3, m[1] - 0.3, m[2] + 0.3, m[3] + 0.3, 0.6), T - 1, Z_OLED0 + 1.2 + 0.15))
g = OLED_GLASS
fcuts.append(prism(krr(g[0] - 0.3, g[1] - 0.3, g[2] + 0.3, g[3] + 0.3, 0.4), T - 1, Z_GLASS + 0.3))
fcuts.append(prism(krr(122.9, 121.6, 125.1, 133.2, 0.3), T - 1, Z_OLED0 + 1.2 + 1.8))     # header solder joints
fcuts.append(prism(krr(123.7, 117.2, 125.2, 137.1, 0.3), T - 1, Z_OLED0 + 1.2 + 0.8))     # small parts
fcuts.append(prism(krr(146.0, 121.0, 147.8, 133.7, 0.3), T - 1, Z_OLED0 + 1.2 + 0.6))     # fpc fold
# The window starts at the glass pocket (no ledge over the glass border, which would be a ring
# shaped overhang in the print) and opens 20 degrees to the top. The module is held down by the
# two straight ledges over its ends, which print as plain bridges.
z_w0 = Z_GLASS + 0.3
grow_ = (Z_BEZEL - z_w0) * math.tan(math.radians(20))
w_lo = krr(g[0] - 0.3, g[1] - 0.3, g[2] + 0.3, g[3] + 0.3, 0.4, z_w0 - 0.01)
w_hi = krr(g[0] - 0.3 - grow_, g[1] - 0.3 - grow_, g[2] + 0.3 + grow_, g[3] + 0.3 + grow_, 0.4 + grow_, Z_BEZEL + 0.01)
fcuts.append(loft_cut(w_lo, w_hi))
# hood cavity over the XIAO (and its antenna connector, FPC antenna on the ceiling)
x_ = XIAO
fcuts.append(prism(krr(x_[0] - 0.4, x_[1] - 0.4, USB_FACE + 0.2, x_[3] + 0.4, 0.8), T - 1, Z_XIAO_TOP + HOOD_GAP))
# USB-C port through the end wall, with a 0.5 mm chamfer, and the matching cut in the plinth
pw, ph = USB_PLUG[0] + 0.4, USB_PLUG[1] + 0.4


def port_face(w, h, r, x):
    f = rrect(0, 0, w, h, r)
    f.rotate(V(0, 0, 0), V(0, 1, 0), 90)            # into the YZ plane, normal along X
    f.translate(V(x, -USB_Y, Z_USB))
    return f


port = Part.Face(port_face(pw, ph, 2.2, USB_FACE - 0.6).OuterWire).extrude(V(USB_FACE + 3.0 - (USB_FACE - 0.6), 0, 0))
# the port starts below the hood (Z 2.3), so in the hood it is a slot open at the bottom: printed
# top down, it needs no bridge
port_ch = Part.makeLoft([port_face(pw, ph, 2.2, USB_FACE + 1.6 - 0.5).OuterWire, port_face(pw + 1.02, ph + 1.02, 2.7, USB_FACE + 1.61).OuterWire], True)
fcuts += [port, port_ch]
# knob hub and button stem through the plinth, chamfered
fcuts.append(cyl(ENC[0], -ENC[1], ENC_HOLE_R, T - 1, Z_FRAME + 1))
fcuts.append(chamfer_top(disc(ENC[0], -ENC[1], ENC_HOLE_R), Z_FRAME, 0.4))
fcuts.append(cyl(BUTTON[0], -BUTTON[1], BTN_HOLE_R, T - 1, Z_FRAME + 1))
fcuts.append(chamfer_top(disc(BUTTON[0], -BUTTON[1], BTN_HOLE_R), Z_FRAME, 0.4))
# the four screw pilots (M2 self tapping): through the plinth, up to 0.9 mm under the top
for k, (x, y) in HOLES.items():
    fcuts.append(cyl(x, -y, MOUNT_PILOT, T - 1, (Z_BEZEL if k == 'H4' else Z_HOOD) - 0.9))
fr = fr.cut(Part.makeCompound(fcuts)).removeSplitter()
frame_s = on_board(fr)
assert frame_s.isValid() and len(frame_s.Solids) == 1, f'frame: {len(frame_s.Solids)} solids'
hood_s = on_board(hood.cut(Part.makeCompound(fcuts)).removeSplitter())
assert hood_s.isValid() and len(hood_s.Solids) == 1, f'hood: {len(hood_s.Solids)} solids'

# ================================================================ knob
# Seat from ENC_SHAFT: the hub goes down into the plate hole to 1.0 mm above the collar, the
# body starts KNOB_GAP above the plinth. D bore over the flat, round bore below, two crush ribs.
z_k0 = Z_FRAME + KNOB_GAP
z_hub = max(Z_ENC_COLLAR + 1.0, T - 2.0)
z_k1 = max(Z_ENC_TOP + 1.2, z_k0 + KNOB_H)
kn = cyl(0, 0, KNOB_R, z_k0, z_k1)
kn = kn.makeChamfer(0.9, [e for e in kn.Edges if abs(e.BoundBox.ZMin - z_k1) < 1e-6]).makeChamfer(
    0.4, [e for e in kn.Edges if abs(e.BoundBox.ZMax - z_k0) < 1e-6 and abs(e.BoundBox.ZMin - z_k0) < 1e-6])
for i in range(12):                     # 12 flutes, 1.8 mm wide (was 24 at 0.84 mm)
    a = 2 * math.pi * (i + 0.5) / 12
    kn = kn.cut(cyl((KNOB_R + 0.3) * math.cos(a), (KNOB_R + 0.3) * math.sin(a), 0.9, z_k0 + 1.0, z_k1 - 1.3))
if z_hub < z_k0:                        # hub wall 1.44 mm (was 0.74)
    kn = kn.fuse(cyl(0, 0, ENC_HOLE_R - 0.5, z_hub, z_k0 + 0.01))
flat_x = 4.5 - 3.0
d_cut = Part.makeBox(10, 10, 60, V(flat_x + 0.08, -5, -30))     # removes the round bore past the flat
bore = cyl(0, 0, 3.06, z_hub - 1, Z_ENC_TOP + 0.3)
bore_d = bore.common(cyl(0, 0, 10, Z_ENC_FLAT - 0.2, Z_ENC_TOP + 0.3)).cut(d_cut)
bore_r = bore.common(cyl(0, 0, 10, z_hub - 1, Z_ENC_FLAT - 0.2))
kn = kn.cut(bore_d.fuse(bore_r))
kn = kn.cut(cone(0, 0, 3.5, 3.06, z_hub - 0.01, z_hub + 0.44))       # lead-in
for a in (120, 240):                                                 # crush ribs on the round side
    r_ = Part.makeBox(0.8, 0.8, Z_ENC_TOP - Z_ENC_FLAT - 0.5, V(-0.4, -0.4, Z_ENC_FLAT + 0.3))
    r_.rotate(V(0, 0, 0), V(0, 0, 1), 45)
    r_.translate(V(3.06 + 0.37 - 0.2 * 0, 0, 0))
    r_.rotate(V(0, 0, 0), V(0, 0, 1), a)
    kn = kn.fuse(r_.common(cyl(0, 0, 3.06, -50, 50)))
kn = kn.cut(Part.makeBox(4.0, 1.2, 0.6, V(flat_x + 0.6, -0.6, z_k1 - 0.6)))   # pointer on the flat side, 1.2 mm wide
kn.translate(V(ENC[0], -ENC[1], 0))
knob = on_board(kn.removeSplitter())
ENGAGE = Z_ENC_TOP - max(z_hub, Z_ENC_COLLAR)
ENGAGE_FLAT = Z_ENC_TOP - max(z_hub, Z_ENC_FLAT)
log(f'knob: {2 * KNOB_R:.1f} mm, Z {z_k0:.2f}..{z_k1:.2f}, hub down to {z_hub:.2f}; shaft in the knob {ENGAGE:.2f} mm, '
    f'of it on the flat {ENGAGE_FLAT:.2f} mm')

# ================================================================ button cap
# Stem guided by the plate and plinth (4.2 mm), flange under the plate (captive), foot on the
# B3SN plunger. The flange stays 0.3 mm under the plate at rest.
z_top = Z_FRAME + BTN_PROUD
z_fl1 = -0.3
# Printed top down. The flange is a 45 degree cone, so it grows out of the stem without an
# overhang; its cone meets the edge of the plate hole as the up stop. The top is flat.
prof = [(0, Z_BTN), (1.6, Z_BTN), (1.6, z_fl1 - 1.0), (BTN_STEM_R + 0.9, z_fl1 - 1.0), (BTN_STEM_R + 0.9, z_fl1 - 0.9),
        (BTN_STEM_R, z_fl1), (BTN_STEM_R, z_top - 0.4), (BTN_STEM_R - 0.4, z_top), (0, z_top)]
cap = revolve(prof)
cap = cap.cut(Part.makeBox(20, 10, 20, V(-10, -BTN_STEM_R - 0.6 - 10, z_fl1 - 5)).cut(cyl(0, 0, BTN_STEM_R + 0.01, -30, 30)))  # flat towards the encoder
cap.translate(V(BUTTON[0], -BUTTON[1], 0))
cap = on_board(cap.removeSplitter())

# ================================================================ pod
# Two parts, so that each prints flat side down with no support:
#   Pod      the ring part: iris, inner seat rim, seat posts, outer wall and the three ears, from the
#            base top (Z_BASE1) up. Printed base side down. The ears go down to the base top, so
#            nothing overhangs.
#   PodBase  the base disc with the ear tabs, the ToF bosses, the light skirt and the tie bar.
#            Printed base top down; everything grows up from it, the tie bar is a 9 mm bridge.
# The three M2 screws go through the cradle, the PodBase tabs and the Pod ears into the plate.
iris_cone_r = lambda z: APT_R0 + (z - Z_SENS) * math.tan(math.radians(FOV_HALF))
z_top_ = T + 0.01
slot = rrect(19.2, 0, 7.4, 6.0, 1.0)
slot.rotate(V(0, 0, 0), V(0, 0, 1), WIRE_ANG)
slot_cut = prism(slot, Z_BASE0 - 1, Z_SEAT + 0.01)
aperture = cone(0, 0, iris_cone_r(Z_SENS + 0.3) - 0.001, iris_cone_r(z_top_), Z_SENS + 0.29, z_top_).fuse(
    cone(0, 0, iris_cone_r(T - 0.5), iris_cone_r(T) + 0.5, T - 0.5, z_top_))


def ear_shape(a, z0, z1):
    x, y = EAR_R * math.cos(math.radians(a)), EAR_R * math.sin(math.radians(a))
    e = cyl(x, y, 3.0, z0, z1).fuse(
        prism(poly([(x - 3.0 * math.sin(math.radians(a)), y + 3.0 * math.cos(math.radians(a))),
                    (x + 3.0 * math.sin(math.radians(a)), y - 3.0 * math.cos(math.radians(a))),
                    (0.9 * x + 3.0 * math.sin(math.radians(a)), 0.9 * y - 3.0 * math.cos(math.radians(a))),
                    (0.8 * x - 3.0 * math.sin(math.radians(a)), 0.8 * y + 3.0 * math.cos(math.radians(a)))]), z0, z1))
    return e, cyl(x, y, 1.15, -30, 10)


# the ring part
pod = cyl(0, 0, IRIS_R, Z_BASE1, T)                                          # iris
pod = pod.fuse(cyl(0, 0, POD_R, Z_BASE1, Z_EAR[1]).cut(cyl(0, 0, DIF_R + 0.1, -20, 20)))   # outer wall
pod = pod.fuse(cyl(0, 0, 17.0, Z_BASE1, Z_SEAT - 0.01).cut(cyl(0, 0, 15.0, -20, 20)))  # inner seat rim (r 15.0..17.0)
# three seat spokes from the rim to the wall: they join the iris and the wall in this part, and
# carry the ring. They sit midway between two LEDs (ring angles 67.5, 180, 292.5 from the pad
# row), so they stay clear of the caps, which sit under the LEDs.
for a in (67.5, 180.0, 292.5):
    p_ = Part.makeBox(DIF_R + 0.2 - 16.0, 2.4, Z_SEAT - Z_BASE1 - 0.005, V(16.0, -1.2, Z_BASE1))
    p_.rotate(V(0, 0, 0), V(0, 0, 1), a + WIRE_ANG)
    pod = pod.fuse(p_)
for a in EAR_A:                                                              # ears, full height
    e, h_ = ear_shape(a, Z_BASE1, Z_EAR[1])
    pod = pod.fuse(e.cut(cyl(0, 0, DIF_R + 0.1, -20, 20))).cut(h_)
pod = pod.cut(aperture).cut(slot_cut).removeSplitter()

# the base
pb = cyl(0, 0, POD_R, Z_BASE0, Z_BASE1)
for a in EAR_A:
    e, h_ = ear_shape(a, Z_BASE0, Z_BASE1)
    pb = pb.fuse(e).cut(h_)
# ToF mount: two bosses with M3 pilots at the holes, two pads at the other edge
for x, y in TOF_HOLES:
    pb = pb.fuse(cyl(x, y, 2.9, Z_TOF_TOP, Z_BASE0 + 0.01)).cut(cyl(x, y, 1.25, Z_TOF_TOP - 1, Z_BASE0 - 0.4))
for x, y in TOF_PADS:
    pb = pb.fuse(cyl(x, y, 2.2, Z_TOF_TOP, Z_BASE0 + 0.01))
# light skirt down to 0.3 mm above the sensor
pb = pb.fuse(cyl(0, 0, iris_cone_r(Z_BASE0) + 1.0, Z_SENS + 0.3, Z_BASE0 + 0.01))
# tie bar under the base, just outside the slot: the ring wires (and the Qwiic cable) pass between
# the base and the bar. A 2.5 mm cable tie or waxed thread round the bar and the wires takes the
# pull off the ring pads.
z_bar = Z_BASE0 - TIE_GAP[1]
tie = [Part.makeBox(TIE_R[1] - TIE_R[0], TIE_POST, TIE_GAP[1] + 0.01, V(TIE_R[0], sg * (TIE_GAP[0] / 2) - (TIE_POST if sg < 0 else 0), z_bar))
       for sg in (-1, 1)]
tie.append(Part.makeBox(TIE_R[1] - TIE_R[0], TIE_GAP[0] + 2 * TIE_POST, TIE_BAR, V(TIE_R[0], -TIE_GAP[0] / 2 - TIE_POST, z_bar - TIE_BAR)))
tie = fuse(tie)
tie.rotate(V(0, 0, 0), V(0, 0, 1), WIRE_ANG)
pb = pb.fuse(tie).cut(aperture).cut(slot_cut).removeSplitter()
podring_s = on_pod(pod)
podbase_s = on_pod(pb)
for nm_, s_ in (('pod', podring_s), ('pod base', podbase_s)):
    assert s_.isValid() and len(s_.Solids) == 1, f'{nm_}: {len(s_.Solids)} solids ' + str([(round(x.Volume, 2), [round(v, 1) for v in (x.BoundBox.Center.x, x.BoundBox.Center.y, x.BoundBox.Center.z)]) for x in s_.Solids])
pod_s = podring_s.fuse(podbase_s).removeSplitter()          # both together, for the checks

# ================================================================ diffuser
# The flange under the plate is a 45 degree cone (it was a flat 2 mm ledge). Printed top face
# down, it grows out of the skirt without an overhang. Its cone meets the bottom edge of the plate
# hole, so the diffuser stays captive and flush. The flange bottom rests on the pod wall.
FL_TOP = HOLE_R - 0.15                     # 22.75 at Z 0.15
# one closed profile: the top annulus, the outer skirt down to 0.05 mm over the ring edge (it
# holds the ring on its seat) and the cone flange
dif = revolve([(IRIS_R + 0.05, Z_DIF0), (21.95, Z_DIF0), (21.95, Z_RING_TOP + 0.05), (DIF_R, Z_RING_TOP + 0.05),
               (DIF_R, -FL_T), (FL_TOP + 0.15 + FL_T, -FL_T), (FL_TOP, 0.15), (FL_TOP, T - 0.3), (HOLE_R - 0.45, T),
               (IRIS_R + 0.35, T), (IRIS_R + 0.05, T - 0.3)])
dif_s = on_pod(dif.removeSplitter())
assert dif_s.isValid() and len(dif_s.Solids) == 1, f'diffuser: {len(dif_s.Solids)} solids'

# ================================================================ battery cradle
cradle_s = None
if BATTERY:
    bl, bw, bt = BATTERY
    z_ceil = Z_TOF_TOP - TOF_T - 0.8          # ceiling top, under the ToF board
    z_c1 = z_ceil - 1.0                       # cell top space
    bump = 0.6
    z_f1 = z_c1 - bt - bump - 0.15            # floor top
    z_f0 = z_f1 - 1.2
    in_x, in_y = bl + 0.8, bw + 0.8
    out_x, out_y = in_x + 2.4, in_y + 2.4
    # No ceiling (it was a 30 x 37 mm bridge). Two lips with 45 degree undersides along the side
    # walls hold the cell edges against the bumps. The walls stop 0.6 mm over the cell top space.
    z_wt = z_c1 + 0.6
    shell = prism(rrect(0, 0, out_x, out_y, 2.0), z_f0, z_wt).cut(prism(rrect(0, 0, in_x, in_y, 0.6), z_f1, z_wt + 1))
    for sxn in (-1, 1):
        w_ = sxn * in_x / 2
        lip = Part.Face(Part.makePolygon([V(w_, -in_y / 2, z_c1 - 0.5), V(w_ - sxn * 1.1, -in_y / 2, z_wt), V(w_, -in_y / 2, z_wt), V(w_, -in_y / 2, z_c1 - 0.5)]))
        shell = shell.fuse(lip.extrude(V(0, in_y, 0)))
    shell = shell.cut(prism(rrect(0, -in_y / 2 - 1.3, in_x, 2.8, 0.3), z_f1, z_wt + 1))   # open at -y for the cell
    # two flex tongues in the floor with a bump each
    for sxn in (-1, 1):
        tx = sxn * bl / 4
        shell = shell.cut(prism(poly([(tx - 3.0, -8), (tx + 3.0, -8), (tx + 3.0, 8), (tx - 3.0, 8)]), z_f0 - 1, z_f1 + 0.01).cut(
            prism(poly([(tx - 2.5, -8.1), (tx + 2.5, -8.1), (tx + 2.5, 7.5), (tx - 2.5, 7.5)]), z_f0 - 2, z_f1 + 1)))
        # tongue 0.6 thick on the bed side, so it prints flat on the bed (not in the air)
        shell = shell.cut(Part.makeBox(5.0, 15.6, z_f1 - z_f0 - 0.6 + 0.01, V(tx - 2.5, -8.1, z_f0 + 0.6)))
        shell = shell.fuse(revolve([(0, z_f0 + 0.59), (1.8, z_f0 + 0.59), (0.9, z_f1 + bump), (0, z_f1 + bump)], tx, -6.5))
    shell = shell.fuse(Part.makeBox(in_x - 6, 1.0, 0.45, V(-(in_x - 6) / 2, -in_y / 2 + 0.6, z_f1 - 0.01)))  # detent lip
    # posts to the pod ears, and arms
    parts_ = [shell]
    for a in EAR_A:
        x, y = EAR_R * math.cos(math.radians(a)), EAR_R * math.sin(math.radians(a))
        parts_.append(cyl(x, y, 2.4, z_f0, Z_BASE0))
        L = math.hypot(x, y)
        ux, uy = x / L, y / L
        r0_ = 15.0 if a == 90 else 19.0          # start outside the floor tongues
        arm = poly([(ux * r0_ - uy * 2.5, uy * r0_ + ux * 2.5), (x - uy * 2.5, y + ux * 2.5), (x + uy * 2.5, y - ux * 2.5), (ux * r0_ + uy * 2.5, uy * r0_ - ux * 2.5)])
        parts_.append(prism(arm, z_f0, z_f0 + 2.4).cut(prism(rrect(0, 0, in_x, in_y, 0.6), z_f1, z_wt + 1)))
    cr = fuse(parts_)
    for a in EAR_A:
        x, y = EAR_R * math.cos(math.radians(a)), EAR_R * math.sin(math.radians(a))
        cr = cr.cut(cyl(x, y, 1.15, z_f0 - 1, Z_BASE0 + 1)).cut(cyl(x, y, 2.1, z_f0 - 1, z_f0 + 1.6))
    cradle_s = on_pod(cr.removeSplitter())
    CELL = on_pod(prism(rrect(0, 0, bl, bw, 1.0), z_f1 + bump, z_f1 + bump + bt))
    log(f'battery cradle: Z {z_f0:.2f}..{z_wt:.2f}; the pickup cavity must be at least {-z_f0 + 1.0:.1f} mm deep under the pod')

# ================================================================ bought parts (models for checks)
tof = Part.makeBox(TOF_W, TOF_W, TOF_T, V(-TOF_W / 2, -TOF_W / 2, Z_TOF_TOP - TOF_T))
for x, y in TOF_HOLES:
    tof = tof.cut(cyl(x, y, 1.65, Z_TOF_TOP - 3, Z_TOF_TOP + 1))
sensor = Part.makeBox(*SENS, V(-SENS[0] / 2, -SENS[1] / 2, Z_TOF_TOP))
qwiic = fuse([Part.makeBox(4.25, 6.0, QWIIC_H, V(sx_ * (TOF_W / 2) - (4.25 if sx_ > 0 else 0), -3.0, Z_TOF_TOP)) for sx_ in (-1, 1)])
tof_all = on_pod(fuse([tof, sensor, qwiic]))
cones = []
for sgn in (1, -1):        # both orientations of the package
    for x, r0, full in APT:
        h = T + 8 - Z_SENS
        cones.append(cone(sgn * x, 0, r0, r0 + h * math.tan(math.radians(full / 2)), Z_SENS, Z_SENS + h))
excl = on_pod(fuse(cones))
excl_grown = on_pod(fuse([cone(c_.BoundBox.Center.x, 0, 0.0001, 0.0001, 0, 1) for c_ in []] or [c.makeOffsetShape(0.3, 0.01) for c in cones]))

L, F = ENC_SHAFT, ENC_SHAFT - ENC_FLAT0
enc = Part.makeBox(14.0, 14.0, 6.1, V(ENC[0] - 7.0, -ENC[1] - 7.0, 0)).fuse(cyl(ENC[0], -ENC[1], 3.5, 6.0, ENC_COLLAR))
shaft = cyl(ENC[0], -ENC[1], 3.0, ENC_COLLAR - 0.01, L).cut(Part.makeBox(10, 10, F + 0.01, V(ENC[0] + 1.5, -ENC[1] - 5, ENC_FLAT0)))
enc = enc.fuse(shaft)
enc.translate(V(0, 0, Z_PCB_TOP))
enc_s = on_board(enc)
b3sn = Part.makeBox(6, 6, 2.0, V(BUTTON[0] - 3, -BUTTON[1] - 3, Z_PCB_TOP)).fuse(cyl(BUTTON[0], -BUTTON[1], 1.5, Z_PCB_TOP + 1.9, Z_BTN))
b3sn_s = on_board(b3sn)
plug = Part.makeBox(26, USB_PLUG[0], USB_PLUG[1], V(USB_FACE + 0.5, -USB_Y - USB_PLUG[0] / 2, Z_USB - USB_PLUG[1] / 2))
plug = plug.fuse(Part.makeBox(7.0, 8.3, 2.5, V(USB_FACE - 6.5, -USB_Y - 4.15, Z_USB - 1.25)))
plug_s = on_board(plug)
sw = Part.makeBox(SW_BODY[0], SW_BODY[1], SW_BODY[2], V(-SW_BODY[0] / 2, -SW_BODY[1] / 2, -SW_BODY[2]))
sw = sw.fuse(Part.makeBox(19.0, SW_EAR_W, 0.5, V(-9.5, -SW_EAR_W / 2, -0.5)))
sw = sw.fuse(Part.makeBox(SW_ACT, SW_ACT, SW_ACT_H, V(-SW_TRAVEL / 2 - SW_ACT / 2, -SW_ACT / 2, 0)))      # actuator at one end
sw = sw.fuse(fuse([cyl(dx, 0, 0.4, -SW_BODY[2] - 3.5, -SW_BODY[2]) for dx in (-3, 0, 3)]))
for dx in (-SW_EARS / 2, SW_EARS / 2):
    sw = sw.cut(cyl(dx, 0, 1.15, -2, 1))
sw.rotate(V(0, 0, 0), V(0, 0, 1), sa); sw.translate(V(sx, sy, 0))
sw_s = sw
pickups = []
for pk in PICKUPS:
    cover = prism(obround(pk['cx'], pk['cy'], PU_COVER[0], PU_COVER[1], pk['angle']), -2, T + 5.5)
    body = prism(obround(pk['cx'], pk['cy'], 74.0, PU_COVER[1] + 0.5, pk['angle']), -20, -2)    # bobbin (ears not modelled)
    pickups.append(cover.fuse(body))
pickups_s = fuse(pickups)
toggle_s = None
if TOGGLE:
    toggle_s = Part.makeBox(13.0, 8.0, 12.0, V(TOGGLE[0] - 6.5, TOGGLE[1] - 4.0, -12.0)).fuse(cyl(*TOGGLE, 3.1, -0.01, T + 7)).fuse(
        cyl(*TOGGLE, 1.6, T + 7, T + 18))
pool_f = poly(POOL['outline'])
cavity_s = prism(Part.Face(Part.makePolygon([V(x, y, 0) for x, y in MEAS['cavity']] + [V(*MEAS['cavity'][0], 0)])), -30, 0)


# ================================================================ wires (models for the checks and the close-up)
def tube(pts, r):
    segs = []
    for a, b in zip(pts, pts[1:]):
        a, b = V(*a), V(*b)
        segs.append(Part.makeCylinder(r, (b - a).Length, a, b - a))
    segs += [Part.makeSphere(r, V(*p)) for p in pts[1:-1]]
    return fuse(segs)


def slot_xy(u, n):
    """Pod frame point from the slot frame (u along the slot, n across it)."""
    w = math.radians(WIRE_ANG)
    return u * math.cos(w) - n * math.sin(w), u * math.sin(w) + n * math.cos(w)


def plen(pts):
    return sum((V(*b) - V(*a)).Length for a, b in zip(pts, pts[1:]))


WR = WIRE_OD / 2
Z_WIRE = Z_BASE0 - 0.15 - WR                 # wire axis under the base
U_END = TIE_R[1] + 6.0                       # modelled up to here; the rest of the route is a polyline
WIRES, WIRE_PTS = {}, {}
for nm, r_, n_ in RING_PADS:
    pts = [(r_, 0.0, Z_RING_BOT - WR), (r_, n_, Z_WIRE), (U_END, n_, Z_WIRE)]
    pts = [(*slot_xy(u, n), z) for u, n, z in pts]
    WIRES['Wire_' + nm] = on_pod(tube(pts, WR))
    WIRE_PTS[nm] = [tuple(POD.multVec(V(*p))) for p in pts]
# Qwiic plug (JST SH, SparkFun Qwiic socket at the +x edge of the ToF board) and its cable, a round
# bundle of 4 x 28 AWG, 2.3 mm across. It runs between the ToF board and the pod base, then through
# the tie bar beside the ring wires.
QW_R = 1.15
z_q = Z_TOF_TOP + 1.3
qplug = Part.makeBox(4.5, 5.0, 2.6, V(TOF_W / 2 - 1.0, -2.5, Z_TOF_TOP))
z_qc = Z_BASE0 - 0.15 - QW_R
q_pts = [(TOF_W / 2 + 3.5, 0.0, z_q), (18.0, 0.0, z_qc), (18.0, -12.0, z_qc),
         (*slot_xy(TIE_R[0] - 0.6, 3.0), z_qc), (*slot_xy(U_END, 3.0), z_qc)]
WIRES['Qwiic_Cable'] = on_pod(qplug.fuse(tube(q_pts, QW_R)))
WIRE_PTS['Qwiic'] = [tuple(POD.multVec(V(*p))) for p in q_pts]
WCOL = {'Wire_G': (0.10, 0.10, 0.11), 'Wire_D': (0.10, 0.60, 0.20), 'Wire_5V': (0.85, 0.10, 0.10), 'Qwiic_Cable': (0.20, 0.35, 0.80)}

# ================================================================ checks
printed = {'Plate': plate, 'BoardSpacers': spacers_s, 'ControlFrame': frame_s, 'Hood': hood_s, 'Knob': knob, 'ButtonCap': cap,
           'Pod': podring_s, 'PodBase': podbase_s, 'Diffuser': dif_s}
if cradle_s:
    printed['BatteryCradle'] = cradle_s
hardware = {'ToF_Board': tof_all, 'Encoder_PEC12R': enc_s, 'Button_B3SN': b3sn_s, 'Switch_EG1201': sw_s, 'Pickups': pickups_s}
if BATTERY:
    hardware['LiPo'] = CELL
if toggle_s:
    hardware['Toggle'] = toggle_s
INTENDED = {('Knob', 'Encoder_PEC12R'): 'crush ribs on the shaft'}
others = [('board:' + l, s) for l, s in board_parts if not l.startswith('reins-guitar_silk')] + \
         [('ring:' + l, s) for l, s in ring_parts if 'silk' not in l] + list(hardware.items()) + list(WIRES.items())
log('\nInterference at rest (mm3), parts that must not touch (wires included):')
bad = 0
names = list(printed)
for i, a in enumerate(names):
    for lab, s in others + [(n, printed[n]) for n in names[i + 1:]]:
        v = overlap(printed[a], s)
        if v > 0.01:
            key = next((k for k in INTENDED if a in k and any(x in lab for x in k if x != a)), None)
            if key:
                log(f'  intended: {a} x {lab}: {v:.2f} ({INTENDED[key]})')
            else:
                log(f'  {a} x {lab}: {v:.2f}'); bad += 1
for lab, s in [('Switch_EG1201', sw_s), ('Pickups', pickups_s), ('ToF_Board', tof_all), ('LiPo', CELL if BATTERY else None)] + \
              ([('Toggle', toggle_s)] if toggle_s else []):
    if s is None:
        continue
    for lab2, s2 in [('board', Part.makeCompound([x for _, x in board_parts]))]:
        v = overlap(s, s2)
        if v > 0.01:
            log(f'  {lab} x {lab2}: {v:.2f}'); bad += 1
log('  none' if not bad else f'  {bad} contacts')

xiao_s = Part.getShape(board_root, find(board_root, 'Seeed_XIAO').Name + '.' + xiao_c.Name + '.' + find(xiao_c, 'nRF52840').Name + '.')
oled_glass_s = Part.makeCompound([s for l, s in board_parts if l == 'glass'])
oled_pcb_s = Part.makeCompound([s for l, s in board_parts if l == 'pcb'])
ring_leds = Part.makeCompound([s for l, s in ring_parts if l.startswith('LED')])
ring_pcb = Part.makeCompound([s for l, s in ring_parts if l.endswith('_PCB')])
ring_caps = Part.makeCompound([s for l, s in ring_parts if l.startswith('C_')])
board_pcb = Part.makeCompound([s for l, s in board_parts if l.endswith('_PCB')])
# pool (pickup cavity) and board cavity: the hanging parts must stay inside, 1 mm off the walls
pool_in = prism(pool_f.makeOffset2D(-1.0, 0, False, False, False), -40, -0.01)
cav_in = prism(Part.Face(Part.makePolygon([V(x, y, 0) for x, y in MEAS['cavity']] + [V(*MEAS['cavity'][0], 0)])), -40, -0.01)
hang_pool = [('Pod', pod_s), ('Diffuser', dif_s), ('Switch_EG1201', sw_s), ('Pickups bodies', fuse([p.common(Part.makeBox(400, 400, 20, V(-100, -100, -20.01))) for p in pickups]))]
if cradle_s:
    hang_pool.append(('BatteryCradle', cradle_s))
if toggle_s:
    hang_pool.append(('Toggle', toggle_s))
rows = []
for nm, s in hang_pool:
    below = s.common(Part.makeBox(400, 400, 40, V(-100, -100, -40.01)))
    out_ = below.cut(pool_in).Volume if below.Volume > 0 else 0.0
    rows.append((f'{nm}: volume under the plate outside the pickup cavity less 1 mm (mm3, must be 0)', out_))
pcb_fp = prism(krr(*BOARD_EDGE, 1.0), Z_PCB_TOP - 3, Z_PCB_TOP + 1)
cav_w = Part.makePolygon([V(x, y, 0) for x, y in MEAS['cavity']] + [V(*MEAS['cavity'][0], 0)])
cav_face = Part.Face(cav_w)
worst = 0.0
for kx, ky in ((BOARD_EDGE[0], BOARD_EDGE[1]), (BOARD_EDGE[2], BOARD_EDGE[1]), (BOARD_EDGE[2], BOARD_EDGE[3]), (BOARD_EDGE[0], BOARD_EDGE[3])):
    px_, py_ = bpt(kx, ky)
    if not cav_face.isInside(V(px_, py_, 0), 0.001, True):
        worst = max(worst, cav_w.distToShape(Part.Vertex(V(px_, py_, 0)))[0])
rows.append(('REINS PCB corner outside the traced board cavity, worst (mm; trace good to about 1 mm, the fit test decides)', worst))

log('\nMinimum clearances (mm):')
tof_board_only = on_pod(tof)
rows += [
    ('plastic inside the VL53L1X exclusion cones, both orientations (mm3, must be 0)', overlap(excl, Part.makeCompound(list(printed.values())))),
    ('plastic inside the cones grown by 0.3 mm for board placement (mm3, must be 0)', overlap(excl_grown, Part.makeCompound(list(printed.values())))),
    ('VL53L1X sensor to the pod (nothing presses on it)', gap(on_pod(sensor), pod_s)),
    ('ToF board parts (Qwiic) to the pod base', gap(on_pod(qwiic), pod_s)),
    ('ToF board to the battery cradle', gap(tof_board_only, cradle_s) if cradle_s else float('nan')),
    ('aperture radius at the plate top', iris_cone_r(T)),
    ('ring LEDs to the diffuser', gap(ring_leds, dif_s)),
    ('ring LEDs to the pod', gap(ring_leds, pod_s)),
    ('ring board edge to the pod wall (radial)', DIF_R + 0.1 - RING_OD / 2),
    ('ring bore to the iris (radial)', RING_ID / 2 - IRIS_R),
    ('ring caps to the pod (groove)', gap(ring_caps, pod_s)),
    ('diffuser skirt over the ring top (design 0.05)', gap(ring_pcb, dif_s)),
    ('LED top to the diffuser underside (air gap for even light)', Z_DIF0 - Z_LED_TOP),
    ('XIAO to the hood', gap(xiao_s, hood_s)),
    ('XIAO to the plate (window)', gap(xiao_s, plate)),
    ('OLED glass to the bezel', gap(oled_glass_s, frame_s)),
    ('OLED module pcb to the plate (0 = rests on the plate)', gap(oled_pcb_s, plate)),
    ('USB-C plug envelope 12.2 x 6.6 to the hood and plate', gap(plug_s, Part.makeCompound([frame_s, hood_s, plate]))),
    ('knob to the control frame and hood', gap(knob, Part.makeCompound([frame_s, hood_s]))),
    ('knob to the plate', gap(knob, plate)),
    ('knob to the button cap', gap(knob, cap)),
    ('knob hub bottom above the encoder collar', z_hub - Z_ENC_COLLAR),
    ('knob bottom above the plinth (push travel 0.4 +0.3)', z_k0 - Z_FRAME),
    ('encoder body to the plate underside', gap(enc_s.common(Part.makeBox(400, 400, 6.1, V(-100, -100, Z_PCB_TOP - 0.01))), plate)),
    ('button cap to the plate and frame (radial guide)', gap(cap, Part.makeCompound([plate, frame_s]))),
    ('button cap flange under the plate at rest (up stop)', -z_fl1),
    ('button cap to the B3SN body', gap(cap, b3sn_s.common(Part.makeBox(400, 400, 2.0, V(-100, -100, Z_PCB_TOP))))),
    ('button cap to the encoder', gap(cap, enc_s)),
    ('battery switch to the pod', gap(sw_s, pod_s)),
    ('battery switch to the pickups', gap(sw_s, pickups_s)),
    ('pickup covers to the plate openings', gap(pickups_s, plate)),
    ('board PCB to the spacers (0 = clamped)', gap(board_pcb, spacers_s)),
    ('spacers to the plate (0 = clamped)', gap(spacers_s, plate)),
    ('frame to the guard screw countersinks', min(gap(frame_s, cyl(x, y, GUARD_CSK / 2, T - 0.1, T + 20)) for x, y in guard)),
    ('USB lane to the guard screw countersinks', min(gap(on_board(prism(lane, LANE_FLOOR, T)), cyl(x, y, GUARD_CSK / 2, 0, T)) for x, y in guard)),
]
if toggle_s:
    rows += [('toggle to the pickups', gap(toggle_s, pickups_s)), ('toggle to the pod', gap(toggle_s, pod_s))]
if cradle_s:
    rows += [('cell to the cradle (no squeeze; bumps touch)', gap(CELL, cradle_s.cut(Part.makeCompound([]))) if False else gap(CELL, cradle_s)),
             ('battery cradle to the pickups', gap(cradle_s, pickups_s))]
# wires: the ring wires must not touch any printed part except where they lie (no pinch)
ring_w = Part.makeCompound([WIRES['Wire_' + n] for n, _, _ in RING_PADS])
ring_pcb_only = Part.makeCompound([s for l, s in ring_parts if l.endswith('_PCB')])
rows += [
    ('ring wires to the pod (slot walls, base, tie bar)', gap(ring_w, pod_s)),
    ('ring wires to the diffuser', gap(ring_w, dif_s)),
    ('ring wires to the plate', gap(ring_w, plate)),
    ('ring wires to the ring caps and LEDs', gap(ring_w, Part.makeCompound([ring_caps, ring_leds]))),
    ('ring wires to each other', min(gap(WIRES['Wire_' + a[0]], WIRES['Wire_' + b[0]]) for a, b in ((RING_PADS[0], RING_PADS[1]), (RING_PADS[1], RING_PADS[2])))),
    ('ring wires to the ToF board', gap(ring_w, tof_all)),
    ('ring wires to the battery cradle', gap(ring_w, cradle_s) if cradle_s else float('nan')),
    ('ring wires to the LiPo', gap(ring_w, CELL) if BATTERY else float('nan')),
    ('ring wire G to the cradle ceiling edge if it dropped straight down (no tie bar)',
     gap(on_pod(cyl(*slot_xy(RING_PADS[0][1], 0), WR, Z_TOF_TOP - 6, Z_RING_BOT)), cradle_s) if cradle_s else float('nan')),
    ('Qwiic plug and cable to the pod', gap(WIRES['Qwiic_Cable'], pod_s)),
    ('Qwiic plug and cable to the battery cradle', gap(WIRES['Qwiic_Cable'], cradle_s) if cradle_s else float('nan')),
    ('Qwiic cable to the ring wires', gap(WIRES['Qwiic_Cable'], ring_w)),
    ('tie bar passage height (base to bar)', TIE_GAP[1]),
    ('tie bar to the ToF board', gap(on_pod(tie), tof_all)),
]
for nm, v in rows:
    log(f'  {nm}: {v:.3f}')

# Cable routes, from one end to the other. The part in the pod is modelled (above). The rest is a
# polyline: out of the pickup cavity at the channel mouth, along the body channel under the plate
# groove, into the board cavity, under the board to the connector. The channel depth is not
# measured: the wires are taken at Z_CH. Under the board the wires leave the PH housing at Z_EXIT.
Z_CH = -2.0
Z_EXIT = Z_PCB_BOT - 6.1 - 1.4
cav_pts = MEAS['cavity']
ys = []
for (x0, y0), (x1, y1) in zip(cav_pts, cav_pts[1:] + cav_pts[:1]):
    if (x0 - ch['x']) * (x1 - ch['x']) <= 0 and x0 != x1:
        ys.append(y0 + (ch['x'] - x0) / (x1 - x0) * (y1 - y0))
cav_y = max(y for y in ys if y < POOL['ymin'])
MOUTH = (ch['x'], POOL['ymin'], Z_CH)
CAV_IN = [(ch['x'], cav_y, Z_CH), (ch['x'], cav_y - 3.0, Z_EXIT)]
KPIN = {'J1': (128.49, 127.12), 'J2': (171.84, 114.42), 'J3': (179.14, 114.42), 'J5': (163.09, 115.39)}   # KiCad, middle of the pins
conn = {k: (*bpt(*p), Z_EXIT) for k, p in KPIN.items()}
conn['J5'] = (*bpt(*KPIN['J5']), Z_PCB_BOT)
cell_exit = tuple(POD.multVec(V(0, -(BATTERY[1] / 2 + 1.6), CELL.BoundBox.Center.z))) if BATTERY else None
ROUTES = {
    'ring wires, pads to J3': WIRE_PTS['G'] + [MOUTH] + CAV_IN + [conn['J3']],
    'ToF Qwiic cable, plug to J1': WIRE_PTS['Qwiic'] + [MOUTH] + CAV_IN + [conn['J1']],
    'switch wires, EG1201 pins to J5': [(sx, sy, -SW_BODY[2] - 2.0), MOUTH] + CAV_IN + [conn['J5']],
}
if BATTERY:
    ROUTES['LiPo lead, cradle opening to J2'] = [cell_exit, MOUTH] + CAV_IN + [conn['J2']]
log(f'\nCable routes (mm), channel taken at Z {Z_CH}, PH plugs left at Z {Z_EXIT:.1f}; add slack yourself:')
for k, p in ROUTES.items():
    log(f'  {k}: {plen(p):.0f}')
for k, p in conn.items():
    log(f'  {k} middle pin at ({p[0]:.1f}, {p[1]:.1f}); channel mouth ({MOUTH[0]:.1f}, {MOUTH[1]:.1f}), board cavity edge at Y {cav_y:.1f}')


# Assembly paths (the plate lies face down on a cloth while the board side goes together)
def sweep(shape, direction, obstacles, dist, step=0.5):
    base = {lab: max(overlap(shape, o), 0) for lab, o in obstacles}
    d = step
    while d <= dist + 1e-9:
        s = shape.translated(direction * d)
        for lab, o in obstacles:
            if overlap(s, o) > base[lab] + 0.01:
                return f'blocked by {lab} at {d:.1f} mm'
        d += step
    return 'free'


board_c = Part.makeCompound([s for l, s in board_parts if l not in ('pcb', 'glass', 'pixels', 'fpc', 'header', 'pins', 'parts')] + [enc_s, b3sn_s])
xiao_only = xiao_s
log('\nAssembly paths (assembled frame, Z up):')
for nm, shp, dirn, obs, dist in [
        ('Knob off the shaft, +Z', knob, V(0, 0, 1), [('ControlFrame', frame_s), ('Plate', plate)], 20),
        ('Board with the XIAO plugged in (OLED module unplugged first), down out of the plate, -Z', board_c, V(0, 0, -1), [('Plate', plate), ('ButtonCap', cap)], 20),
        ('Hood off the plinth, +Z (board screws out)', hood_s, V(0, 0, 1), [('ControlFrame', frame_s), ('Plate', plate), ('board', board_c)], 20),
        ('ControlFrame off the plate, +Z (board screws out, knob and hood off)', frame_s, V(0, 0, 1), [('Plate', plate), ('board', board_c)], 20),
        ('PodBase down off the Pod, -Z', podbase_s, V(0, 0, -1), [('Pod', podring_s), ('Plate', plate)], 20),
        ('ButtonCap out of the plate, -Z (board off)', cap, V(0, 0, -1), [('Plate', plate)], 15),
        ('Pod down, -Z', pod_s, V(0, 0, -1), [('Plate', plate), ('Diffuser', dif_s)], 20),
        ('Diffuser down out of the plate, -Z (pod off)', dif_s, V(0, 0, -1), [('Plate', plate)], 15),
        ('USB-C plug out along its axis by the 6.6 mm shell length', plug_s, BOARD.Rotation.multVec(V(1, 0, 0)),
         [('Plate', plate), ('ControlFrame', frame_s), ('Hood', hood_s)], 7)] + \
        ([('Battery cell out of the cradle sleeve, pod -y', CELL, POD.Rotation.multVec(V(0, -1, 0)), [('BatteryCradle', cradle_s)], 35)] if BATTERY else []):
    log(f'  {nm}: {sweep(shp, dirn, obs, dist)}')

# ================================================================ document, STEP, STL
COL = {'Plate': CHAR, 'ControlFrame': CHAR, 'Hood': CHAR, 'BoardSpacers': (0.35, 0.36, 0.38), 'Knob': ORANGE, 'ButtonCap': ORANGE,
       'Pod': BLACK, 'PodBase': (0.10, 0.10, 0.11), 'Diffuser': WHITE,
       'BatteryCradle': (0.12, 0.12, 0.13), 'ToF_Board': (0.75, 0.10, 0.08), 'Encoder_PEC12R': (0.55, 0.57, 0.60),
       'Button_B3SN': (0.25, 0.25, 0.27), 'Switch_EG1201': (0.35, 0.36, 0.38), 'Pickups': (0.06, 0.06, 0.06),
       'LiPo': (0.62, 0.64, 0.68), 'Toggle': (0.75, 0.76, 0.78)}
objs = {}
ply_objs = []
for i, (z0, z1) in enumerate(((-30, Z_ORANGE[0]), Z_ORANGE, (Z_ORANGE[1], T + 1))):
    o = doc.addObject('Part::Feature', ('Plate_PlyBottom', 'Plate_PlyMiddle', 'Plate_PlyTop')[i])
    o.Shape = plate.common(Part.makeBox(500, 500, z1 - z0, V(-200, -200, z0)))
    ply_objs.append(o)
for k, s in list(printed.items()) + list(hardware.items()):
    if k == 'Plate':
        continue
    o = doc.addObject('Part::Feature', k)
    o.Shape = s
    objs[k] = o
plate_o = doc.addObject('Part::Feature', 'Plate')
plate_o.Shape = plate
wire_objs = {}
for k, s in WIRES.items():
    o = doc.addObject('Part::Feature', k)
    o.Shape = s
    wire_objs[k] = o
doc.recompute()
if GUI:
    for k, o in wire_objs.items():
        o.ViewObject.ShapeColor = WCOL[k]
        o.ViewObject.Deviation = 0.02
    for o, col in zip(ply_objs, (CHAR, ORANGE, CHAR)):
        o.ViewObject.ShapeColor = col
        o.ViewObject.Deviation = 0.05
    plate_o.ViewObject.Visibility = False
    for k, o in objs.items():
        o.ViewObject.ShapeColor = COL[k]
        o.ViewObject.Deviation = 0.05
    objs['Diffuser'].ViewObject.Transparency = 25
    for o in board_root.OutListRecursive + ring_root.OutListRecursive:
        if hasattr(o, 'ViewObject') and o.ViewObject and o.Label.endswith('_PCB'):
            o.ViewObject.ShapeColor = (0.05, 0.05, 0.06)
doc.saveAs(os.path.join(OUT, 'reins-pickguard-v2.FCStd'))
asm = ply_objs + list(objs.values()) + list(wire_objs.values()) + [board_root, ring_root]
if GUI:
    import ImportGui
    ImportGui.export(asm, os.path.join(OUT, 'reins-pickguard-v2-assembly.step'))
else:
    Import.export(asm, os.path.join(OUT, 'reins-pickguard-v2-assembly.step'))


def stl(shape, path):
    mm = MeshPart.meshFromShape(Shape=shape, LinearDeflection=0.02, AngularDeflection=0.15, Relative=False)
    mm.write(path)


def to_bed(s, flip=False):
    p = s.copy()
    if flip:
        p.rotate(V(0, 0, 0), V(1, 0, 0), 180)
    p.translate(V(-p.BoundBox.XMin, -p.BoundBox.YMin, -p.BoundBox.ZMin))
    return p


# print orientation. The plate goes top face down; its long side along the CORE One X axis.
plate_print = plate.copy()
plate_print.rotate(V(0, 0, 0), V(1, 0, 0), 180)
bb = plate_print.BoundBox
if bb.YLength > bb.XLength:
    plate_print.rotate(V(0, 0, 0), V(0, 0, 1), 90)
plate_print = to_bed(plate_print)
PRINT = {'plate': plate_print, 'control-frame': to_bed(frame_s), 'hood': to_bed(hood_s, True), 'knob': to_bed(knob, True),
         'button-cap': to_bed(cap, True), 'pod': to_bed(podring_s), 'pod-base': to_bed(podbase_s, True), 'diffuser': to_bed(dif_s, True)}
# the four board spacers in a row, upright
_sp = [to_bed(sv) for sv in spacers_s.Solids]
for i_, sv in enumerate(_sp):
    sv.translate(V(i_ * 8.0, 0, 0))
PRINT['board-spacers'] = Part.makeCompound(_sp)
if cradle_s:
    PRINT['battery-cradle'] = to_bed(cradle_s)
for fn, s in PRINT.items():
    stl(s, os.path.join(OUT, 'stl', f'reins-pg2-{fn}.stl'))
# plies for a multi tool printer, in the same print orientation as the plate
def print_pose(shape):
    q = shape.copy()
    q.rotate(V(0, 0, 0), V(1, 0, 0), 180)
    if plate.BoundBox.YLength > plate.BoundBox.XLength:
        q.rotate(V(0, 0, 0), V(0, 0, 1), 90)
    return q


_ref = print_pose(plate).BoundBox
for i, o in enumerate(ply_objs):
    q = print_pose(o.Shape)
    q.translate(V(-_ref.XMin, -_ref.YMin, -_ref.ZMin))
    stl(q, os.path.join(OUT, 'stl', 'reins-pg2-plate-ply%d-%s.stl' % (i + 1, ('bottom-charcoal', 'middle-orange', 'top-charcoal')[i])))
log(f'plate on the bed: {plate_print.BoundBox.XLength:.1f} x {plate_print.BoundBox.YLength:.1f} x {plate_print.BoundBox.ZLength:.1f} mm')

# fit test: 0.6 mm sheet with every opening, a hollow board dummy down to the header tips and a
# depth probe under the pod down to the cradle floor. It lies flat only if both cavities are deep enough.
ft = prism(outline, 0, 0.6).cut(Part.makeCompound([c for c in cuts if c.BoundBox.ZMin < 0] + [on_pod(cyl(0, 0, HOLE_R, -1, 2))] +
                                                  [on_board(prism(krr(*XIAO_WIN, 0.8), -1, 2)), on_board(cyl(ENC[0], -ENC[1], ENC_HOLE_R, -1, 2))]))
dummy = prism(krr(*BOARD_EDGE, 1.0), Z_PCB_BOT - 6.1, 0.01).cut(prism(krr(BOARD_EDGE[0] + 1.2, BOARD_EDGE[1] + 1.2, BOARD_EDGE[2] - 1.2, BOARD_EDGE[3] - 1.2, 0.5), Z_PCB_BOT - 7, -0.5))
ft = ft.fuse(on_board(dummy))
if cradle_s:
    ft = ft.fuse(on_pod(cyl(0, 0, 4.0, cradle_s.BoundBox.ZMin - 1.0, 0.01).cut(cyl(0, 0, 2.8, cradle_s.BoundBox.ZMin - 2, -0.5))))
ft = ft.removeSplitter()
stl(to_bed(ft, True), os.path.join(OUT, 'stl', 'reins-pg2-fit-test.stl'))

# ================================================================ web export
EXPLODE = {   # order of the real assembly (README), offset in mm in the frame
    'Plate': (1, (0, 0, 0)), 'ButtonCap': (2, (0, 0, -26)), 'ControlFrame': (3, (0, 0, 30)), 'Hood': (3, (0, 0, 40)),
    'BoardSpacers': (4, (0, 0, -20)), 'Board_Guitar': (4, (0, 0, -40)), 'PodBase': (9, (0, 0, -52)),
    'Knob': (5, (0, 0, 52)), 'Switch_EG1201': (6, (0, 0, -22)), 'Diffuser': (7, (0, 0, -16)), 'Board_Ring': (8, (0, 0, -30)),
    'Pod': (9, (0, 0, -44)), 'ToF_Board': (10, (0, 0, -58)), 'LiPo': (11, (0, 0, -78)), 'BatteryCradle': (12, (0, 0, -88)),
    'Encoder_PEC12R': (4, (0, 0, -40)), 'Button_B3SN': (4, (0, 0, -40)), 'Pickups': (0, (0, 0, 0)), 'Toggle': (13, (0, 0, 24))}
MAT = {'Plate': 'PLA or PETG in three plies: charcoal, orange, charcoal', 'ControlFrame': 'PLA, charcoal, matte', 'Knob': 'PLA, orange',
       'Hood': 'PLA, charcoal, matte', 'BoardSpacers': 'PLA, any colour, 4 tubes', 'PodBase': 'PLA, black, matte',
       'ButtonCap': 'PLA, orange', 'Pod': 'PLA, black, matte', 'Diffuser': 'PETG natural or PLA white, translucent',
       'BatteryCradle': 'PLA or PETG, black', 'ToF_Board': 'SparkFun SEN-14722 (VL53L1X), simple model',
       'Encoder_PEC12R': f'Bourns PEC12R, {ENC_SHAFT:g} mm shaft, simple model', 'Button_B3SN': 'Omron B3SN-3012, simple model',
       'Switch_EG1201': 'E-Switch EG1201, simple model', 'Pickups': 'stock pickup covers, measured positions (hide on the site if wanted)',
       'LiPo': '1S LiPo 36 x 29 x 4.75 (Adafruit 1578 class)', 'Toggle': 'stock 3-way mini toggle, envelope only'}
web = os.path.join(OUT, 'web')
parts = []
for k in list(printed) + list(hardware):
    s = printed.get(k, hardware.get(k))
    stl(s, os.path.join(web, k + '.stl'))
    o, e = EXPLODE[k]
    parts.append({'name': k, 'file': k + '.stl', 'color': '#%02X%02X%02X' % tuple(round(v * 255) for v in COL[k]),
                  'material': MAT[k], 'printed': k in printed, 'order': o, 'explode': list(e)})
for i, (nm, col) in enumerate((('Plate_PlyBottom', CHAR), ('Plate_PlyMiddle', ORANGE), ('Plate_PlyTop', CHAR))):
    stl(ply_objs[i].Shape, os.path.join(web, nm + '.stl'))
pb = plate.BoundBox
ring_pl = POD.multiply(App.Placement(V(0, 0, Z_RING_BOT), App.Rotation(V(0, 0, 1), WIRE_ANG)))
webj = {
    'units': 'mm',
    'frame': 'Guitar frame: X towards the neck, Y towards the bass side, Z up, 0 on the body top (underside of the plate). '
             'The same frame as Sketch001 of cad-midiguitar.FCStd.',
    'threejs': 'Put all STLs in one group with rotation.x = -PI/2 (Z up to Y up).',
    'plate_bbox': {'min': [round(pb.XMin, 3), round(pb.YMin, 3), round(pb.ZMin, 3)], 'max': [round(pb.XMax, 3), round(pb.YMax, 3), round(pb.ZMax, 3)],
                   'center': [round(pb.Center.x, 3), round(pb.Center.y, 3), round(pb.Center.z, 3)]},
    'plate_plies': [{'file': 'Plate_PlyBottom.stl', 'color': '#2B2E33'}, {'file': 'Plate_PlyMiddle.stl', 'color': '#F06B14'},
                    {'file': 'Plate_PlyTop.stl', 'color': '#2B2E33'}],
    'parts': sorted(parts, key=lambda p: p['order']),
    'boards': [
        {'name': 'Board_Guitar', 'kicad': 'midiguitar/reins-guitar/reins-guitar.kicad_pcb', 'glb': 'boards/reins-guitar.glb',
         'order': EXPLODE['Board_Guitar'][0], 'explode': list(EXPLODE['Board_Guitar'][1]), 'color_pcb': '#111214',
         'kicad_to_frame': f'p = (x, -y); rotate p by {THETA:.4f} deg about Z; add ({TX:.4f}, {TY:.4f}); Z = z + {Z_PCB_TOP - PCB_T_STEP:.4f}',
         'matrix_kicad_mm_to_frame_rowmajor': [[round(math.cos(_t), 6), round(math.sin(_t), 6), 0, round(TX, 4)],
                                               [round(math.sin(_t), 6), round(-math.cos(_t), 6), 0, round(TY, 4)],
                                               [0, 0, 1, round(Z_PCB_TOP - PCB_T_STEP, 4)], [0, 0, 0, 1]],
         'module_lift_mm': {'M1 (XIAO)': round(XIAO_STACK - 9.13, 3), 'J4 (OLED)': round(Z_OLED0 - Z_PCB_TOP - 9.19, 3)}},
        {'name': 'Board_Ring', 'kicad': 'midiguitar/reins-ring/reins-ring.kicad_pcb', 'glb': 'boards/reins-ring.glb',
         'order': EXPLODE['Board_Ring'][0], 'explode': list(EXPLODE['Board_Ring'][1]), 'color_pcb': '#111214',
         'kicad_to_frame': f'p = (x - 100, -(y - 100)); rotate p by {POD_ROT + WIRE_ANG:.4f} deg about Z; add ({POD_C[0]}, {POD_C[1]}); Z = z + {Z_RING_BOT:.4f}',
         'matrix_kicad_mm_to_frame_rowmajor': None}],
    'body_outline': 'body_outline.json',
}
ra = math.radians(POD_ROT + WIRE_ANG)
c_, s_ = math.cos(ra), math.sin(ra)
webj['boards'][1]['matrix_kicad_mm_to_frame_rowmajor'] = [
    [round(c_, 6), round(s_, 6), 0, round(POD_C[0] - 100 * c_ - 100 * s_, 4)],
    [round(s_, 6), round(-c_, 6), 0, round(POD_C[1] - 100 * s_ + 100 * c_, 4)],
    [0, 0, 1, round(Z_RING_BOT, 4)], [0, 0, 0, 1]]
with open(os.path.join(web, 'parts.json'), 'w') as fh:
    json.dump(webj, fh, indent=1)

# ================================================================ renders (FreeCAD app only)
if GUI:
    import time
    import FreeCADGui as Gui
    Gui.updateGui()
    view = Gui.getDocument(doc.Name).activeView()
    view.setCameraType('Orthographic')
    rdir = os.path.join(OUT, 'renders')
    for o in doc.Objects:
        if o.TypeId.startswith('Part::Feature') and o not in objs.values() and o not in ply_objs \
                and o not in board_root.OutListRecursive and o not in ring_root.OutListRecursive:
            o.ViewObject.Visibility = False

    def show(names, boards=True, plies=True):
        for k, o in objs.items():
            o.ViewObject.Visibility = k in names
        board_root.ViewObject.Visibility = ring_root.ViewObject.Visibility = boards
        for o in ply_objs:
            o.ViewObject.Visibility = plies

    def shot(name, setup, w=1600, h=1400, fit=True, min_size=15000):
        path = os.path.join(rdir, name)
        for _ in range(6):
            setup()
            if fit:
                view.fitAll()
            for _ in range(5):
                Gui.updateGui(); time.sleep(0.1)
            view.saveImage(path, w, h, 'White')
            if os.path.getsize(path) > min_size:
                return path
        log('render failed:', name)
        return path

    shown = [k for k in objs if k not in ('Pickups', 'Toggle')]
    show(shown)
    shot('v2-top.png', view.viewTop)
    shot('v2-iso.png', view.viewIsometric)
    show(shown + ['Pickups'])
    shot('v2-under.png', view.viewBottom)
    show(shown)
    # exploded
    saved = {k: App.Placement(o.Placement) for k, o in objs.items()}
    bsave, rsave = App.Placement(board_root.Placement), App.Placement(ring_root.Placement)
    for k, o in objs.items():
        o.Placement = App.Placement(V(*EXPLODE[k][1]), App.Rotation()).multiply(o.Placement)
    board_root.Placement = App.Placement(V(*EXPLODE['Board_Guitar'][1]), App.Rotation()).multiply(board_root.Placement)
    ring_root.Placement = App.Placement(V(*EXPLODE['Board_Ring'][1]), App.Rotation()).multiply(ring_root.Placement)
    shot('v2-exploded.png', view.viewIsometric, 1600, 1800)
    for k, o in objs.items():
        o.Placement = saved[k]
    board_root.Placement, ring_root.Placement = bsave, rsave
    # on the guitar: the rectified owner's photo under the plate
    photo = os.path.join(OUT, 'ref', 'IMG_2407-rectified.png')
    if os.path.exists(photo):
        import Image as ImageMod  # noqa: F401
        ip = doc.addObject('Image::ImagePlane', 'Photo_IMG_2407')
        ip.ImageFile = photo
        R = json.load(open(os.path.join(OUT, 'ref', 'rectified.json')))
        ip.XSize, ip.YSize = R['x1'] - R['x0'], R['y1'] - R['y0']
        ip.Placement = App.Placement(V((R['x0'] + R['x1']) / 2, (R['y0'] + R['y1']) / 2, -0.3), App.Rotation())
        doc.recompute()
        show(shown + ['Pickups'])
        objs['Pickups'].ViewObject.Visibility = False
        shot('v2-on-guitar.png', view.viewTop, 1700, 1500)
        ip.ViewObject.Visibility = False
    # close-ups: pod section and the controls
    sec = []

    def section(name, box, cam, w=1600, h=1100):
        for o in sec:
            doc.removeObject(o.Name)
        sec.clear()
        for k, s in list(printed.items()) + list(hardware.items()):
            if k == 'Pickups':
                continue
            c = s.common(box)
            if c.Volume > 0.01:
                o = doc.addObject('Part::Feature', 'sec_' + k); o.Shape = c
                o.ViewObject.ShapeColor = COL[k] if k != 'Plate' else CHAR
                sec.append(o)
        for lab, s in board_parts + ring_parts:
            c = s.common(box)
            if not c.isNull() and c.Volume > 0.01:
                o = doc.addObject('Part::Feature', 'secb'); o.Shape = c
                o.ViewObject.ShapeColor = (0.05, 0.05, 0.06) if lab.endswith('_PCB') else (0.6, 0.6, 0.62)
                sec.append(o)
        co = doc.addObject('Part::Feature', 'sec_cones'); co.Shape = excl.common(box)
        co.ViewObject.ShapeColor = (1.0, 0.85, 0.2); co.ViewObject.Transparency = 60
        sec.append(co)
        for o in objs.values():
            o.ViewObject.Visibility = False
        for o in ply_objs:
            o.ViewObject.Visibility = False
        board_root.ViewObject.Visibility = ring_root.ViewObject.Visibility = False
        doc.recompute()
        shot(name, cam, w, h)

    pod_box = Part.makeBox(80, 40, 50, V(-40, 0, -30))
    pod_box.rotate(V(0, 0, 0), V(0, 0, 1), POD_ROT)
    pod_box.translate(V(POD_C[0], POD_C[1], 0))
    section('v2-closeup-pod-section.png', pod_box, view.viewFront)
    ctrl_box = Part.makeBox(90, 20, 40, V(115, -127.0, -25))
    section('v2-closeup-controls-section.png', on_board(ctrl_box), lambda: view.viewFront() or view.viewRotateLeft() if False else view.viewFront())
    for o in sec:
        doc.removeObject(o.Name)
    sec.clear()
    def zoom_to(names, cam):
        def setup():
            cam()
            Gui.Selection.clearSelection()
            for n in names:
                Gui.Selection.addSelection(objs[n])
            Gui.SendMsgToActiveView('ViewSelection')
            Gui.Selection.clearSelection()
        return setup

    show(shown)
    shot('v2-closeup-controls.png', zoom_to(['ControlFrame', 'Hood', 'Knob', 'ButtonCap', 'Switch_EG1201'], view.viewIsometric), 1800, 1400, fit=False)
    show(['Pod', 'PodBase', 'Diffuser', 'ToF_Board', 'BatteryCradle', 'LiPo'], boards=False, plies=False)
    ring_root.ViewObject.Visibility = True
    shot('v2-closeup-pod.png', view.viewIsometric, 1600, 1400)
    shot('v2-closeup-pod-under.png', view.viewBottom, 1600, 1400)

    # close-ups of the ring pads with the three wires, the tie bar and the Qwiic cable
    def wire_shot(name, box, vdir, w=1600, h=1200, pod_transp=0):
        for o in sec:
            doc.removeObject(o.Name)
        sec.clear()
        items = [('Pod', pod_s, BLACK), ('Diffuser', dif_s, WHITE), ('ToF_Board', tof_all, COL['ToF_Board'])]
        if cradle_s:
            items += [('BatteryCradle', cradle_s, COL['BatteryCradle']), ('LiPo', CELL, COL['LiPo'])]
        items += [(k, s, WCOL[k]) for k, s in WIRES.items()]
        items += [('ring', s, (0.05, 0.05, 0.06) if l.endswith('_PCB') else (0.85, 0.65, 0.2) if 'Cu' in l or 'pad' in l.lower() else (0.6, 0.6, 0.62))
                  for l, s in ring_parts]
        for nm, s, col in items:
            c = s.common(box)
            if not c.isNull() and c.Volume > 0.001:
                o = doc.addObject('Part::Feature', 'wsec_' + nm); o.Shape = c
                o.ViewObject.ShapeColor = col
                if nm == 'Pod':
                    o.ViewObject.Transparency = pod_transp
                if nm == 'Diffuser':
                    o.ViewObject.Transparency = 40
                sec.append(o)
        for o in list(objs.values()) + ply_objs + list(wire_objs.values()):
            o.ViewObject.Visibility = False
        board_root.ViewObject.Visibility = ring_root.ViewObject.Visibility = False
        doc.recompute()
        d = V(*vdir).normalize()
        zc = -d
        yc = (V(0, 0, 1) - zc * zc.z).normalize()
        xc = yc.cross(zc)
        m = App.Matrix(xc.x, yc.x, zc.x, 0, xc.y, yc.y, zc.y, 0, xc.z, yc.z, zc.z, 0, 0, 0, 0, 1)
        shot(name, lambda: view.setCameraOrientation(App.Rotation(m).Q), w, h)

    a_ = math.radians(POD_ROT + WIRE_ANG)
    u_, n_ = V(math.cos(a_), math.sin(a_), 0), V(-math.sin(a_), math.cos(a_), 0)
    wb = Part.makeBox(24, 22, 16, V(12, -9, -11))           # slot frame
    wb.rotate(V(0, 0, 0), V(0, 0, 1), WIRE_ANG)
    wire_shot('v2-closeup-ring-wires.png', on_pod(wb), tuple(-0.55 * u_ - 0.45 * n_ + V(0, 0, 0.7)), pod_transp=30)
    ws = Part.makeBox(24, 14, 16, V(12, -2.9, -11))          # cut on the plane 2.9 mm beside the slot axis
    ws.rotate(V(0, 0, 0), V(0, 0, 1), WIRE_ANG)
    wire_shot('v2-closeup-ring-wires-section.png', on_pod(ws), tuple(n_))
    # close-up of the logo on the plate, from the top
    for o in sec:
        doc.removeObject(o.Name)
    sec.clear()
    lb_ = Part.makeCompound(logo).BoundBox
    lbox = Part.makeBox(lb_.XLength + 24, lb_.YLength + 16, 10, V(lb_.XMin - 12, lb_.YMin - 8, -5))
    for o, col in zip(ply_objs, (CHAR, ORANGE, CHAR)):
        c = o.Shape.common(lbox)
        if c.Volume > 0.01:
            q = doc.addObject('Part::Feature', 'lsec_' + o.Name); q.Shape = c
            q.ViewObject.ShapeColor = col
            sec.append(q)
    for o in list(objs.values()) + ply_objs + list(wire_objs.values()):
        o.ViewObject.Visibility = False
    board_root.ViewObject.Visibility = ring_root.ViewObject.Visibility = False
    doc.recompute()
    shot('v2-closeup-logo.png', view.viewTop, 1800, 800, min_size=8000)
    for o in sec:
        doc.removeObject(o.Name)
    sec.clear()
    show(shown)
    view.viewIsometric(); view.fitAll()
    doc.save()

with open(os.path.join(OUT, 'checks.txt'), 'w') as fh:
    fh.write('\n'.join(LOG) + '\n')
log('done')
if os.environ.get('FC_EXIT'):
    os._exit(0)
