# REINS pod: one printed part that holds the 16 LED NeoPixel ring and the SparkFun 1 in ToF board.
#
#   /Applications/FreeCAD.app/Contents/Resources/bin/freecadcmd pod.py
#
# It is the "eye" of build_pickguard.py with these changes:
#   1. The sensor cone follows the real sensor, the SparkFun VL53L4CD (field of view 18 degrees, sensor
#      on the middle of the 1 in board, holes 0.8 in apart: SparkFun board drawing). Half angle 14
#      degrees, 5 degrees more than the sensor needs, because the package (4.4 x 2.4 mm) has its
#      emitter and receiver off centre.
#   2. The ring seat has a groove, so the solder pads and wires on the back of the ring fit. It is
#      open to the outer edge, because the Adafruit ring 1463 has its five big plated pads at 20 to
#      21 mm radius. The ring rests on an inner rim (1.5 mm wide) and on three outer posts.
#   3. The wire slot is wider (radial 16.6 to 22.2 mm, 6 mm tangential) and goes through the base so
#      the wires drop below it. Same angle as before, towards the guitar board.
# Same collar interface as before (three M2 screws at 90, 210, 330 degrees), same diffuser.
# Output: stl/reins-pod.stl
import math, os
import FreeCAD as App
import Part
import Mesh, MeshPart

HERE = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(HERE, 'build_pickguard.py')).read().split('# ---------------------------------------------------------------- document')[0]
ns = {'__file__': os.path.join(HERE, 'build_pickguard.py'), '__name__': 'params'}
exec(src, ns)    # parameters and helper functions only, no plate building
V, cyl, cone, rect, prism, union = (ns[k] for k in ('V', 'cyl', 'cone', 'rect', 'prism', 'union'))
T, COLLAR, Z_RING, Z_TOF_TOP = ns['T'], ns['COLLAR'], ns['Z_RING'], ns['Z_TOF_TOP']
TOF_HOLE, SCREW_R, RING_ID, RING_OD = ns['TOF_HOLE'], ns['SCREW_R'], ns['RING_ID'], ns['RING_OD']
POD_C, POD_ROT = ns['POD_C'], ns['POD_ROT']
cx, cy = POD_C
bx, by = ns['bpt'](155.2, 127.3)
wire_ang = math.degrees(math.atan2(by - cy, bx - cx)) - POD_ROT
print('wire angle in pod frame: %.2f deg' % wire_ang)

SLOT_R0, SLOT_R1 = 16.6, 22.2   # radial extent of the wire slot
GROOVE = (17.4, 22.4, 1.2)      # ring seat groove: inner radius, outer radius (open), depth
POSTS = (90, 210, 330)          # outer ring posts, in the pod frame, 6 degrees wide, 1.1 mm radial
FOV_HALF = 14.0       # half angle of the sensor cone. VL53L4CD field of view is 18 deg, so 9 deg half angle
SENSOR_Z = Z_TOF_TOP + 1.0   # top of the sensor window
R_WIN = 2.6           # radius of the cone where it leaves the sensor window

z_base = COLLAR[2] - 1.0
eye = cyl(0, 0, COLLAR[1], z_base, COLLAR[2])                                       # base
eye = eye.fuse(cyl(0, 0, 14.9, COLLAR[2] - 0.01, T))                                 # iris
eye = eye.fuse(cyl(0, 0, 22.2, COLLAR[2] - 0.01, Z_RING).cut(cyl(0, 0, 15.9, COLLAR[2] - 1, 1)))  # ring seat
eye = eye.fuse(cyl(0, 0, 26.1, COLLAR[2] - 0.01, Z_RING - 0.2).cut(cyl(0, 0, 22.95, COLLAR[2] - 1, 1)))  # flange clamp
for sx in (-1, 1):
    for sy in (-1, 1):
        eye = eye.fuse(cyl(sx * TOF_HOLE, sy * TOF_HOLE, 2.7, Z_TOF_TOP, z_base + 0.01))   # ToF bosses
eye = eye.fuse(cyl(0, 0, 3.9, -6.4, z_base + 0.01))                                   # light skirt

# Groove in the ring seat for the parts and wire pads on the back of the ring board.
groove = cyl(0, 0, GROOVE[1], Z_RING - GROOVE[2], Z_RING + 0.001).cut(cyl(0, 0, GROOVE[0], Z_RING - 2, Z_RING + 1))
for a in POSTS:    # leave three outer posts standing
    post = Part.makeBox(1.1, 2.2, GROOVE[2] + 0.2, V(GROOVE[1] - 1.1, -1.1, Z_RING - GROOVE[2] - 0.1))
    post.rotate(V(0, 0, 0), V(0, 0, 1), a)
    groove = groove.cut(post)
eye = eye.cut(groove)

# Sensor cone: from the window up through the iris.
z0 = SENSOR_Z
t = math.tan(math.radians(FOV_HALF))
r_top = R_WIN + (T + 0.001 - z0) * t
eye = eye.cut(cone(0, 0, R_WIN, r_top, z0, T + 0.001))
eye = eye.cut(cone(0, 0, r_top, r_top + 0.6, T - 0.6, T + 0.001))                     # rim chamfer
for sx in (-1, 1):
    for sy in (-1, 1):
        eye = eye.cut(cyl(sx * TOF_HOLE, sy * TOF_HOLE, 1.25, Z_TOF_TOP - 0.1, z_base - 0.5))   # M2 pilots
for a in (90, 210, 330):
    x, y = SCREW_R * math.cos(math.radians(a)), SCREW_R * math.sin(math.radians(a))
    eye = eye.cut(cyl(x, y, 1.15, z_base - 1, COLLAR[2] + 0.1)).cut(cyl(x, y, 2.1, z_base - 1, z_base))
slot = rect((SLOT_R0 + SLOT_R1) / 2, 0, SLOT_R1 - SLOT_R0, 6.0)
slot.rotate(V(0, 0, 0), V(0, 0, 1), wire_ang)
eye = eye.cut(prism(slot, z_base - 1, Z_RING + 0.01))
eye = eye.removeSplitter()

print('cone radius at the rim: %.2f mm, iris radius 14.9 mm, wall %.2f mm' % (r_top, 14.9 - r_top))
print('valid', eye.isValid(), 'solids', len(eye.Solids), 'volume %.0f mm3' % eye.Volume)
bb = eye.BoundBox
print('bbox %.1f x %.1f x %.1f' % (bb.XLength, bb.YLength, bb.ZLength))

# Fit checks against simple models of the bought parts.
ring = cyl(0, 0, RING_OD / 2, Z_RING, Z_RING + 0.9).cut(cyl(0, 0, RING_ID / 2, Z_RING - 1, 1))
tofb = Part.makeBox(25.4, 25.4, 1.6, V(-12.7, -12.7, Z_TOF_TOP - 1.6))
sensor = Part.makeBox(4.4, 2.4, 1.0, V(-2.2, -1.2, Z_TOF_TOP))
for n, p in (('ring', ring), ('ToF board', tofb), ('sensor', sensor)):
    print('overlap with %-9s %.2f mm3' % (n, eye.common(p).Volume))

m = MeshPart.meshFromShape(Shape=eye, LinearDeflection=0.02, AngularDeflection=0.2, Relative=False)
m.removeDuplicatedFacets(); m.removeDuplicatedPoints(); m.harmonizeNormals()
out = os.path.join(HERE, 'stl', 'reins-pod.stl')
m.write(out)
print('wrote', out, 'solid' if m.isSolid() else 'NOT SOLID', m.CountFacets, 'facets')
