#!/usr/bin/env python3
"""Layout checks for the REINS pickguard, without FreeCAD geometry: edge margins of the holes and
openings against ref/scan_outline.json, and the pickup cavity and switch slots in the Sketch001
frame, from a homography fitted to the nine screw holes in a body photo overlay.
Run with FreeCAD's Python: bin/python -I check_pickguard_layout.py <pickguard dir> <body overlay png>
"""
import json, math, sys
import numpy as np
from PIL import Image

pg, overlay = sys.argv[1], sys.argv[2]
meas = json.load(open(f'{pg}/ref/measured.json'))
ref = json.load(open(f'{pg}/ref/sketch001.json'))
P = np.array(json.load(open(f'{pg}/ref/scan_outline.json'))['outline'])


def edge_dist(q):
    a, b = P, np.roll(P, -1, axis=0)
    ab = b - a
    t = np.clip(((q - a) * ab).sum(1) / np.maximum((ab * ab).sum(1), 1e-9), 0, 1)
    return np.hypot(*(a + t[:, None] * ab - q).T).min()


def inside(q):
    x, y = q
    c = False
    for (x0, y0), (x1, y1) in zip(P, np.roll(P, -1, axis=0)):
        if (y0 > y) != (y1 > y) and x < x0 + (y - y0) * (x1 - x0) / (y1 - y0):
            c = not c
    return c


print('outline bbox', P.min(0).round(1), P.max(0).round(1))
print('screw hole: distance to the outline edge (hole radius 1.75, countersink radius 3.5)')
for h in meas['guard_screws']:
    q = np.array([h['cx'], h['cy']])
    print(f'  ({q[0]:6.1f},{q[1]:6.1f}) inside {inside(q)}  edge {edge_dist(q):5.2f}')

print('pickup covers against the openings (cover 70 x 18, from the measured screws)')
for name, op in zip(('bridge', 'neck'), ref['pickups']):
    m = meas['pickups'][name]
    a = math.radians(op['angle'])
    u, v = np.array([math.cos(a), math.sin(a)]), np.array([-math.sin(a), math.cos(a)])
    o = np.array([op['cx'], op['cy']])
    worst_w = worst_l = 0
    for su in (-1, 1):
        for sv in (-1, 1):
            b = math.radians(m['angle'])
            cu, cv = np.array([math.cos(b), math.sin(b)]), np.array([-math.sin(b), math.cos(b)])
            c = np.array([m['cx'], m['cy']]) + su * 35 * cu + sv * 9 * cv
            worst_l = max(worst_l, abs((c - o) @ u) - (op['length'] / 2 + 0.1))
            worst_w = max(worst_w, abs((c - o) @ v) - (op['width'] / 2 + 0.1))
    sc = [np.array(s) for s in m['screws']]
    gap = [abs((s - o) @ u) - (op['length'] / 2 + 0.1) for s in sc]
    print(f'  {name}: opening angle {op["angle"]:.2f}, screws angle {m["angle"]:.2f}; centre shift '
          f'{np.round(np.array([m["cx"], m["cy"]]) - o, 2)}; cover beyond opening: width {worst_w:+.2f}, length {worst_l:+.2f}; '
          f'screw centre past the opening end: {gap[0]:.2f}, {gap[1]:.2f}')

# Homography from overlay pixels to the frame. Pixels read by hand, about 3 px (0.5 mm).
px = {(176.162, 12.076): (125, 105), (181.619, 152.08): (92, 995), (154.84, 90.102): (254, 597),
      (102.576, 17.628): (568, 130), (95.547, 181.193): (623, 1186), (28.214, 32.419): (1028, 213),
      (14.22, 116.736): (1127, 755), (6.258, 166.824): (1187, 1087), (7.189, 66.371): (1165, 427)}
print('overlay size', Image.open(overlay).size)
A = []
for (X, Y), (x, y) in px.items():
    A.append([x, y, 1, 0, 0, 0, -X * x, -X * y, -X])
    A.append([0, 0, 0, x, y, 1, -Y * x, -Y * y, -Y])
H = np.linalg.svd(np.array(A, float))[2][-1].reshape(3, 3)


def to(p):
    w = H @ np.array([p[0], p[1], 1.0])
    return w[:2] / w[2]


res = [np.hypot(*(to(v) - np.array(k))) for k, v in px.items()]
print('homography residual mm', np.round(res, 2))
print('pickup cavity corners in the frame (hand read, foil edge):')
for name, p in (('bridge side, Y low', (1110, 490)), ('bridge side, Y high', (1105, 1040)), ('neck side, Y low', (328, 512)), ('neck side, Y high', (335, 1040))):
    print(f'  {name}: {np.round(to(p), 1)}')
edge_y = np.mean([to(p)[1] for p in ((1105, 1040), (335, 1040))])
for r in ref['rects']:
    if r['cy'] > 150:
        pts = np.array(r['pts'])
        print(f"slot centre ({r['cx']:.1f},{r['cy']:.1f}) size {np.hypot(*(pts[1]-pts[0])):.1f} x {np.hypot(*(pts[2]-pts[1])):.1f}; "
              f"its near edge is {pts[:,1].min() - edge_y:+.1f} mm from the cavity edge in Y (positive = over wood)")


def stadium(q, c, L, W, ang):
    """Signed distance from q to an obround opening (negative = inside)."""
    a = math.radians(ang)
    d = np.array(q) - np.array(c)
    x, y = d @ np.array([math.cos(a), math.sin(a)]), d @ np.array([-math.sin(a), math.cos(a)])
    r = W / 2
    return math.hypot(max(abs(x) - (L / 2 - r), 0), y) - r


print('exact clearances, opening = sketch size + 0.1 per side')
for name, op in zip(('bridge', 'neck'), ref['pickups']):
    m = meas['pickups'][name]
    for s in m['screws']:
        d = stadium(s, (op['cx'], op['cy']), op['length'] + .2, op['width'] + .2, op['angle'])
        print(f'  {name} screw {s}: {d:.2f} mm from the opening; wall to the 3.5 mm hole {d - 1.75:+.2f}, to the 7 mm countersink {d - 3.5:+.2f}, to the 1 mm chamfer and countersink {d - 4.5:+.2f}')
    b = math.radians(m['angle'])
    cu, cv = np.array([math.cos(b), math.sin(b)]), np.array([-math.sin(b), math.cos(b)])
    for w in (18.0, 19.0):
        worst = -9
        for t in np.linspace(0, 2 * math.pi, 720):
            r = w / 2
            sx = math.cos(t)
            q = np.array([m['cx'], m['cy']]) + (r * math.cos(t) + math.copysign(35 - r, sx)) * cu + r * math.sin(t) * cv
            worst = max(worst, stadium(q, (op['cx'], op['cy']), op['length'] + .2, op['width'] + .2, op['angle']))
        print(f'  {name} cover 70 x {w:.0f} on the measured screws: {"hits the opening by" if worst > 0 else "clears the opening by"} {abs(worst):.2f} mm')
