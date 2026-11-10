# Generate the REINS LED ring board (replaces the Adafruit NeoPixel Ring 16).
#
#   <KiCad python> make_ring.py        (writes reins-ring.kicad_pcb and reins-ring.kicad_pro)
#   (KiCad python: /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3)
#
# There is no schematic: this script is the source of the netlist. Run kicad-cli DRC on the result.
#
# Layers:
#   F.Cu  16 LEDs, the DIN chain (short links), short stubs to the vias
#   B.Cu  outer band = VDD (5V), inner band = GND, one 100 nF cap per LED between the two bands,
#         and three round wire pads (GND, DIN, 5V) in a radial row in the gap between LED16 and LED1
# The board is a plain annulus. Order it 0.8 mm thick: LED (about 1.6 mm) plus board must stay under 2.4 mm.
#
# Angles are counter clockwise as seen from the top, in degrees, with 0 pointing to +X on the sheet.
import json, math, os, shutil
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
FPLIB = '/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints'
mm = pcbnew.FromMM

# ---- parameters -------------------------------------------------------------------------------
PAD_ANGLE = 0.0        # where the wire pad row sits, as seen from the top (degrees). The ring turns on its seat, so any value works.
BOARD_T = 0.8          # board thickness in mm (JLCPCB option)
OD, ID = 44.4, 31.75   # ring outer and inner (ToF bore) diameter
LED_R = 18.9           # radius of the LED centres (a little under the 19.06 mm of the Adafruit ring, so the VDD vias keep 0.3 mm from the edge)
N = 16
CX, CY = 100.0, 100.0  # board centre on the sheet (mm)
WIRE_PAD_D = 1.5       # wire pad diameter
WIRE_PAD_R = {'GND': 17.2, 'DIN': 19.0, '5V': 20.8}   # radius of each wire pad centre
BAND_GAP = (LED_R - 0.2, LED_R + 0.2)  # B.Cu: GND band below the first radius, VDD band above the second
LED_LCSC, CAP_LCSC = 'C114586', 'C14663'


def P(x, y):
    return pcbnew.VECTOR2I(mm(x), mm(y))


def polar(r, a_deg, extra=0.0):
    a = math.radians(a_deg)
    return (CX + r * math.cos(a), CY - r * math.sin(a))


def pad_xy(radial, tang):
    """Point in the pad frame: radial along the pad angle, tang 90 degrees counter clockwise from it."""
    a = math.radians(PAD_ANGLE)
    return (CX + radial * math.cos(a) - tang * math.sin(a), CY - radial * math.sin(a) - tang * math.cos(a))


board = pcbnew.BOARD()
board.SetCopperLayerCount(2)
board.GetDesignSettings().SetBoardThickness(mm(BOARD_T))
nets = {}
for name in ('GND', '5V', 'DIN', 'N_END'):
    n = pcbnew.NETINFO_ITEM(board, name)
    board.Add(n)
    nets[name] = n
chain = []
for i in range(1, N):
    n = pcbnew.NETINFO_ITEM(board, 'D%d' % i)
    board.Add(n)
    nets['D%d' % i] = n


def add_field(fp, name, text):
    fp.SetField(name, text)
    for f in fp.GetFields():
        if f.GetName() == name:
            f.SetVisible(False)


def line(layer, a, b, w, net=None, shape=pcbnew.SHAPE_T_SEGMENT):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(shape)
    s.SetLayer(layer)
    s.SetStart(P(*a))
    s.SetEnd(P(*b))
    s.SetWidth(mm(w))
    board.Add(s)
    return s


def track(layer, pts, w, net):
    for a, b in zip(pts[:-1], pts[1:]):
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(P(*a))
        t.SetEnd(P(*b))
        t.SetWidth(mm(w))
        t.SetLayer(layer)
        t.SetNet(net)
        board.Add(t)


def via(xy, net, dia=0.5, drill=0.3):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(P(*xy))
    v.SetWidth(mm(dia))
    v.SetDrill(mm(drill))
    v.SetViaType(pcbnew.VIATYPE_THROUGH)
    v.SetNet(net)
    board.Add(v)


# ---- outline ----------------------------------------------------------------------------------
RO, RI = OD / 2, ID / 2
outer_c = pcbnew.PCB_SHAPE(board)
outer_c.SetShape(pcbnew.SHAPE_T_CIRCLE)
outer_c.SetLayer(pcbnew.Edge_Cuts)
outer_c.SetCenter(P(CX, CY))
outer_c.SetEnd(P(CX + RO, CY))
outer_c.SetWidth(mm(0.1))
board.Add(outer_c)
bore = pcbnew.PCB_SHAPE(board)
bore.SetShape(pcbnew.SHAPE_T_CIRCLE)
bore.SetLayer(pcbnew.Edge_Cuts)
bore.SetCenter(P(CX, CY))
bore.SetEnd(P(CX + RI, CY))
bore.SetWidth(mm(0.1))
board.Add(bore)

# ---- LEDs and caps ----------------------------------------------------------------------------
lib_led = os.path.join(FPLIB, 'LED_SMD.pretty')
lib_cap = os.path.join(FPLIB, 'Capacitor_SMD.pretty')
leds, pads = [], {}
for i in range(N):
    a = PAD_ANGLE + 11.25 + i * 22.5                  # LED 0 is just counter clockwise of the pad row
    x, y = polar(LED_R, a)
    fp = pcbnew.FootprintLoad(lib_led, 'LED_WS2812B_PLCC4_5.0x5.0mm_P3.2mm')
    fp.SetReference('LED%d' % (i + 1))
    fp.SetValue('WS2812B')
    fp.SetPosition(P(x, y))
    fp.SetOrientationDegrees(a - 90)
    add_field(fp, 'LCSC', LED_LCSC)
    for g in list(fp.GraphicalItems()):          # the library silk of neighbours would overlap: move it to F.Fab
        if g.GetLayer() == pcbnew.F_SilkS:
            g.SetLayer(pcbnew.F_Fab)
    board.Add(fp)
    leds.append(fp)
    d = {p.GetNumber(): p for p in fp.Pads()}
    pads[i] = d
    d['1'].SetNet(nets['5V'])
    d['3'].SetNet(nets['GND'])
    d['4'].SetNet(nets['DIN'] if i == 0 else nets['D%d' % i])
    d['2'].SetNet(nets['N_END'] if i == N - 1 else nets['D%d' % (i + 1)])

    # cap on B.Cu, pads along the radius: outer pad on the VDD band, inner pad on the GND band
    cap = pcbnew.FootprintLoad(lib_cap, 'C_0603_1608Metric')
    cap.SetReference('C%d' % (i + 1))
    cap.SetValue('100nF')
    cap.SetPosition(P(x, y))
    cap.SetOrientationDegrees(a - 90 + 90)
    add_field(cap, 'LCSC', CAP_LCSC)
    board.Add(cap)
    cap.Flip(P(x, y), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
    cp = {p.GetNumber(): p for p in cap.Pads()}
    # after the flip, turn the cap so that its two pads lie on the radius through the LED centre
    q1, q2 = cp['1'].GetPosition(), cp['2'].GetPosition()
    cur = math.atan2(-(q2.y - q1.y), q2.x - q1.x)         # sheet angle, counter clockwise
    want = math.radians(a)
    delta = math.degrees(want - cur)
    cap.Rotate(P(x, y), pcbnew.EDA_ANGLE(delta, pcbnew.DEGREES_T))
    # pick which pad is outer by distance to the board centre
    c1 = cp['1'].GetPosition()
    r1 = math.hypot(pcbnew.ToMM(c1.x) - CX, pcbnew.ToMM(c1.y) - CY)
    c2 = cp['2'].GetPosition()
    r2 = math.hypot(pcbnew.ToMM(c2.x) - CX, pcbnew.ToMM(c2.y) - CY)
    outer, inner = (cp['1'], cp['2']) if r1 > r2 else (cp['2'], cp['1'])
    for g in cap.GraphicalItems():                # no silk from the caps
        if isinstance(g, pcbnew.PCB_SHAPE) and g.GetLayer() == pcbnew.B_SilkS:
            g.SetLayer(pcbnew.B_Fab)
    outer.SetNet(nets['5V'])
    inner.SetNet(nets['GND'])
    for t in (fp, cap):
        for tx in (t.Reference(), t.Value()):
            tx.SetVisible(False)


def padpos(i, num):
    p = pads[i][num].GetPosition()
    return (pcbnew.ToMM(p.x), pcbnew.ToMM(p.y))


def local_to_board(i, lx, ly):
    """Convert a point in the LED footprint frame to board mm."""
    a = math.radians(PAD_ANGLE + 11.25 + i * 22.5 - 90)
    x0, y0 = polar(LED_R, PAD_ANGLE + 11.25 + i * 22.5)
    return (x0 + lx * math.cos(a) + ly * math.sin(a), y0 - lx * math.sin(a) + ly * math.cos(a))


# vias beside the VDD pad (outside) and the GND pad (inside), short stubs on F.Cu
for i in range(N):
    p1 = padpos(i, '1')
    v1 = local_to_board(i, -2.45, -2.55)
    track(pcbnew.F_Cu, [p1, v1], 0.3, nets['5V'])
    via(v1, nets['5V'])
    p3 = padpos(i, '3')
    v3 = local_to_board(i, 2.45, 2.55)
    track(pcbnew.F_Cu, [p3, v3], 0.3, nets['GND'])
    via(v3, nets['GND'])

# DIN chain: from DOUT of LED i to DIN of LED i+1
for i in range(N - 1):
    a = padpos(i, '2')
    b = padpos(i + 1, '4')
    # leave the DOUT pad towards the gap, stay in the middle band, then enter the DIN pad
    m1 = local_to_board(i, -3.35, 1.65)
    m2 = local_to_board(i + 1, 3.35, -1.65)
    track(pcbnew.F_Cu, [a, m1, m2, b], 0.2, nets['D%d' % (i + 1)])

# ---- wire pads on the back (B.Cu, SMD, no part, no paste) ---------------------------------------
wire = {}
for name, r in WIRE_PAD_R.items():
    x, y = pad_xy(r, 0.0)
    fp = pcbnew.FOOTPRINT(board)
    fp.SetReference('W_' + name)
    fp.SetValue(name)
    fp.SetPosition(P(x, y))
    fp.SetAttributes(pcbnew.FP_EXCLUDE_FROM_BOM | pcbnew.FP_EXCLUDE_FROM_POS_FILES)
    pad = pcbnew.PAD(fp)
    pad.SetNumber('1')
    pad.SetShape(pcbnew.PAD_SHAPE_CIRCLE)
    pad.SetAttribute(pcbnew.PAD_ATTRIB_SMD)
    pad.SetSize(pcbnew.VECTOR2I(mm(WIRE_PAD_D), mm(WIRE_PAD_D)))
    ls = pcbnew.LSET()
    ls.AddLayer(pcbnew.B_Cu)
    ls.AddLayer(pcbnew.B_Mask)
    pad.SetLayerSet(ls)
    pad.SetPosition(P(x, y))
    pad.SetNet(nets[name])
    if name != 'DIN':                                     # the zone touches the pad through spokes, so the iron can heat it
        pad.SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    fp.Add(pad)
    fp.Reference().SetVisible(False)
    fp.Value().SetVisible(False)
    board.Add(fp)
    wire[name] = (x, y)

# DIN: from its back pad to a via beside it (not in the pad: solder would run into the hole), then on F.Cu to LED1 DIN
din_via = pad_xy(WIRE_PAD_R['DIN'], 1.35)
track(pcbnew.B_Cu, [wire['DIN'], din_via], 0.3, nets['DIN'])
via(din_via, nets['DIN'])
track(pcbnew.F_Cu, [din_via, padpos(0, '4')], 0.25, nets['DIN'])

# ---- zones on B.Cu ----------------------------------------------------------------------------
def zone(layer, net, pts, prio=0):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(net)
    z.SetMinThickness(mm(0.2))
    z.SetLocalClearance(mm(0.2))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
    z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    z.SetAssignedPriority(prio)
    ol = z.Outline()
    ol.NewOutline()
    for p in pts:
        ol.Append(mm(p[0]), mm(p[1]))
    board.Add(z)
    return z


def arc_pts(r, a0, a1, step=3.0):
    n = max(2, int(abs(a1 - a0) / step) + 1)
    return [polar(r, a0 + (a1 - a0) * k / (n - 1)) for k in range(n)]


# GND band: full ring, from the bore edge to just under the LED radius
z_gnd = zone(pcbnew.B_Cu, nets['GND'], arc_pts(BAND_GAP[0], 0, 359.9, 3.0) + arc_pts(RI + 0.3, 359.9, 0, 3.0))
# VDD band: full ring, from just over the LED radius to the outer edge
z_vdd = zone(pcbnew.B_Cu, nets['5V'], arc_pts(RO + 0.2, 0, 359.9, 3.0) + arc_pts(BAND_GAP[1], 359.9, 0, 3.0))

# ---- silk -------------------------------------------------------------------------------------
def text(s, xy, size, layer=pcbnew.F_SilkS, angle=0.0):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s)
    t.SetPosition(P(*xy))
    t.SetLayer(layer)
    t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
    t.SetTextThickness(mm(0.16))
    t.SetTextAngleDegrees(angle)
    board.Add(t)


# one letter per pad on the back (G = GND, D = DIN, 5 = 5V), beside the pad row, mirrored because they are on B.SilkS
for name, r in WIRE_PAD_R.items():
    t = pcbnew.PCB_TEXT(board)
    t.SetText(name[0])
    t.SetPosition(P(*pad_xy(r, -1.7)))
    t.SetLayer(pcbnew.B_SilkS)
    t.SetMirrored(True)
    t.SetTextSize(pcbnew.VECTOR2I(mm(1.0), mm(1.0)))
    t.SetTextThickness(mm(0.16))
    t.SetTextAngleDegrees(PAD_ANGLE + 90)
    board.Add(t)

pcbnew.SaveBoard(os.path.join(HERE, 'reins-ring.kicad_pcb'), board)

# matching project file, with the JLCPCB 2 layer limits
src = os.path.join(HERE, '..', 'reins-guitar', 'reins-guitar.kicad_pro')
p = json.load(open(src))
r = p['board']['design_settings']['rules']
r['min_clearance'] = 0.15
r['min_track_width'] = 0.15
r['min_copper_edge_clearance'] = 0.3
r['min_via_diameter'] = 0.5
p['board']['design_settings']['rule_severities']['courtyards_overlap'] = 'ignore'
p['net_settings']['classes'][0]['clearance'] = 0.15
p['net_settings']['classes'][0]['track_width'] = 0.25
json.dump(p, open(os.path.join(HERE, 'reins-ring.kicad_pro'), 'w'), indent=2)
print('wrote reins-ring.kicad_pcb')
