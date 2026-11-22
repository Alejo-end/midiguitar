# Photo reference for the REINS pickguard version 2. Writes v2/ref/:
#   homographies.json        photo pixel <-> guitar frame (mm), fitted to the nine pickguard screw holes
#   IMG_2407-rectified.png   the owner's photo (stock pickguard on), flattened to the guitar frame
#   IMG_2408-rectified.png   the body without the pickguard, flattened the same way
#   rectified.json           the window of both rectified images, in mm (for the render overlay)
#   pickup_cavity.json       the pickup cavity ("pool") opening, traced on IMG_2408
#   pickup_covers.json       the two pickup covers measured on IMG_2407
# and v2/web/body_outline.json (the guitar body around the pickguard, for the web site).
#
# Run with the FreeCAD Python (it has numpy, Pillow and contourpy):
#   /Applications/FreeCAD.app/Contents/Resources/bin/python -I photo_ref_v2.py
# The photos are taken out of cad/cad-midiguitar.FCStd (image planes IMG_2407 and IMG_2408).
import json
import math
import os
import sys
import zipfile

import numpy as np
from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
CAD = os.path.normpath(os.path.join(HERE, '..', '..', '..', 'cad', 'cad-midiguitar.FCStd'))
OUT = os.path.join(HERE, 'v2', 'ref')
WEB = os.path.join(HERE, 'v2', 'web')
os.makedirs(OUT, exist_ok=True)
os.makedirs(WEB, exist_ok=True)
MEAS = json.load(open(os.path.join(HERE, 'ref', 'measured.json')))
HOLES = [(h['cx'], h['cy']) for h in MEAS['guard_screws']]
PU = [tuple(s) for p in MEAS['pickups'].values() for s in p['screws']]

# Pixel centres of the nine screw holes (and, on IMG_2407, the four pickup screws), found on
# crops around the positions predicted by the image planes, then refined to the centroid of the
# dark hole. Same order as measured.json.
PIX = {
    'IMG_2407 Background Removed.png': [(2300.1, 2626.9), (344.3, 1958.2), (630.0, 3038.2), (1261.0, 2883.4), (1449.1, 1138.1),
                                        (1898.8, 2912.5), (2415.1, 1708.1), (2419.7, 800.3), (628.6, 863.8),
                                        (734.0, 2537.0), (1722.0, 2595.0), (637.0, 1506.0), (1618.0, 1571.0)],
    'IMG_24082.jpeg': [(2859.7, 880.9), (2058.0, 2802.3), (3170.9, 2604.8), (3057.7, 1950.5), (1322.7, 1638.4),
                       (3128.5, 1301.0), (1947.8, 718.6), (1067.3, 670.4), (1002.1, 2427.5)],
}
NAMES = {'IMG_2407 Background Removed.png': 'IMG_2407', 'IMG_24082.jpeg': 'IMG_2408'}
PPM = 4.0
X0, X1, Y0, Y1 = -120.0, 300.0, -110.0, 290.0


def fit(src, dst):
    A = []
    for (x, y), (u, v) in zip(src, dst):
        A += [[x, y, 1, 0, 0, 0, -u * x, -u * y, -u], [0, 0, 0, x, y, 1, -v * x, -v * y, -v]]
    H = np.linalg.svd(np.array(A))[2][-1].reshape(3, 3)
    return H / H[2, 2]


def apply(H, p):
    q = H @ np.array([p[0], p[1], 1.0])
    return q[:2] / q[2]


zf = zipfile.ZipFile(CAD)
homs, rect = {}, {}
for fname, pix in PIX.items():
    im = Image.open(zf.open(fname))
    im.load()
    im = im.convert('RGBA')
    src = (HOLES + PU)[:len(pix)]
    H = fit(src, pix)
    Hi = np.linalg.inv(H)
    res = [float(np.hypot(*(apply(Hi, p) - np.array(s)))) for s, p in zip(src, pix)]
    nm = NAMES[fname]
    homs[nm] = {'file_in_FCStd': fname, 'size_px': list(im.size), 'H_mm_to_px': H.tolist(),
                'residual_mm': [round(r, 3) for r in res], 'rms_mm': round(math.sqrt(sum(r * r for r in res) / len(res)), 3)}
    W, Hh = int((X1 - X0) * PPM), int((Y1 - Y0) * PPM)
    A = np.array([[1 / PPM, 0, X0 + 0.5 / PPM], [0, -1 / PPM, Y1 - 0.5 / PPM], [0, 0, 1]])
    Tm = H @ A
    Tm = Tm / Tm[2, 2]
    r = im.transform((W, Hh), Image.PERSPECTIVE, tuple(Tm.flatten()[:8]), Image.BICUBIC)
    r.save(os.path.join(OUT, f'{nm}-rectified.png'))
    rect[nm] = np.asarray(r).astype(float)
    print(nm, 'rms', homs[nm]['rms_mm'], 'mm, worst', max(res))
json.dump(homs, open(os.path.join(OUT, 'homographies.json'), 'w'), indent=1)
json.dump({'x0': X0, 'x1': X1, 'y0': Y0, 'y1': Y1, 'px_per_mm': PPM,
           'note': 'Pixel (0, 0) is the top left corner at (x0, y1). Image up = +Y (bass side), right = +X (neck).'},
          open(os.path.join(OUT, 'rectified.json'), 'w'), indent=1)

# Pickup cavity: read on IMG_2408-rectified at the body surface edge (copper lined walls): a
# rounded rectangle, good to about 1 mm.
cx0, cx1, cy0, cy1, cr = 19.5, 142.5, 75.5, 159.5, 7.0
pts = []
for (ccx, ccy, a0) in ((cx1 - cr, cy0 + cr, -90), (cx1 - cr, cy1 - cr, 0), (cx0 + cr, cy1 - cr, 90), (cx0 + cr, cy0 + cr, 180)):
    for k in range(0, 91, 10):
        a = math.radians(a0 + k)
        pts.append((round(ccx + cr * math.cos(a), 2), round(ccy + cr * math.sin(a), 2)))
json.dump({'outline': pts, 'xmin': cx0, 'xmax': cx1, 'ymin': cy0, 'ymax': cy1, 'corner_r': cr,
           'source': 'IMG_2408 rectified with the nine screw holes; edge of the copper lined opening', 'accuracy_mm': 1.0,
           'depth': 'not measured: measure it'}, open(os.path.join(OUT, 'pickup_cavity.json'), 'w'), indent=1)

# Pickup covers on IMG_2407: dark pixels around each pickup, principal axis.
a7 = rect['IMG_2407']
xs = X0 + (np.arange(a7.shape[1]) + 0.5) / PPM
ys = Y1 - (np.arange(a7.shape[0]) + 0.5) / PPM
covers = {}
for name, p in MEAS['pickups'].items():
    i0, i1 = int((p['cx'] - 25 - X0) * PPM), int((p['cx'] + 25 - X0) * PPM)
    j0, j1 = int((Y1 - p['cy'] - 45) * PPM), int((Y1 - p['cy'] + 45) * PPM)
    sub = a7[j0:j1, i0:i1, :3].mean(2)
    jj, ii = np.nonzero((sub < 45) & (a7[j0:j1, i0:i1, 3] > 128))
    X = np.c_[xs[i0 + ii], ys[j0 + jj]]
    c = X.mean(0)
    w, v = np.linalg.eigh(np.cov((X - c).T))
    ang = math.degrees(math.atan2(v[1, 1], v[0, 1])) % 180
    (x0, y0), (x1, y1) = p['screws']
    covers[name] = {'dark_centre': [round(float(c[0]), 2), round(float(c[1]), 2)], 'dark_angle_deg': round(ang, 2),
                    'screw_mid': [round((x0 + x1) / 2, 2), round((y0 + y1) / 2, 2)],
                    'screw_angle_deg': round(math.degrees(math.atan2(y0 - y1, x0 - x1)), 2)}
json.dump({'covers': covers, 'sketch_openings_angle_deg': 78.13,
           'note': 'The dark cover (top face plus the side seen in perspective) lies parallel to its screws within 0.3 deg. '
                   'A 70 x 18 mm obround centred between the screws matches the visible top face.'},
          open(os.path.join(OUT, 'pickup_covers.json'), 'w'), indent=1)
print('covers', covers)

# Body outline for the web site. IMG_2407 is a background removed photo: its alpha mask is the
# guitar. The neck is cut off along its edges. Points next to the photo frame are not body
# edges; they are dropped and the gaps are closed with an estimate.
import contourpy
alpha = a7[..., 3] > 128
XX, YY = np.meshgrid(xs, ys)
Hm = np.array(homs['IMG_2407']['H_mm_to_px'])
P = np.stack([XX, YY, np.ones_like(XX)], -1) @ Hm.T
u, v = P[..., 0] / P[..., 2], P[..., 1] / P[..., 2]
w7, h7 = homs['IMG_2407']['size_px']
inside = (u > 10) & (u < w7 - 10) & (v > 10) & (v < h7 - 10)
body = alpha & ~((XX > 166) & (YY > 93.5) & (YY < 146.5))
b = np.asarray(Image.fromarray((body * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(4))).astype(float)[::4, ::4] / 255
ins = inside[::4, ::4]
gx, gy = xs[::4], ys[::4]
line = sorted(contourpy.contour_generator(gx, gy, b).lines(0.5), key=len, reverse=True)[0]
meas = []
for x, y in line[::2]:
    i, j = int(np.argmin(abs(gx - x))), int(np.argmin(abs(gy - y)))
    ok = ins[max(j - 5, 0):j + 6, max(i - 5, 0):i + 6].all()
    # IMG_2407 was cut at the pickguard on the bass side (IMG_2408 shows red body up to its frame
    # at Y 198 there), and around the bridge plate: keep only the treble side and the cutaway.
    ok = ok and ((y < 12.0 and x > -100) or (x > 150 and y < 147))
    meas.append((round(float(x), 2), round(float(y), 2), bool(ok)))
# the longest run of good points along the contour (it may wrap round the start)
n = len(meas)
best, cur = (0, 0), None
for i in range(2 * n):
    if meas[i % n][2]:
        cur = i if cur is None else cur
        if i - cur + 1 > best[1] - best[0]:
            best = (cur, i + 1)
    else:
        cur = None
treble = [meas[i % n][:2] for i in range(best[0], min(best[1], best[0] + n))]
if treble[-1][0] < treble[0][0]:
    treble = treble[::-1]
# Estimate for the rest (not seen in either photo): bass edge at least 198 mm (IMG_2408 frame),
# the bass horn lower edge seen on IMG_2408 at X 210..234, the butt end placed from the Sonic
# Mustang overall length (about 120 mm behind the bridge).
est_bass = [(235.0, 196.0), (241.0, 201.0), (238.0, 205.5), (225.0, 205.0), (200.0, 203.0), (160.0, 203.5), (120.0, 205.0),
            (80.0, 205.5), (40.0, 202.0), (10.0, 193.0), (0.0, 186.0)]
est_butt = [(-40.0, 183.0), (-80.0, 176.0), (-118.0, 158.0), (-142.0, 120.0), (-150.0, 75.0), (-143.0, 30.0), (-125.0, 2.0),
            (-105.0, -16.0)]
horn_seen = [(210.0, 147.0), (216.0, 169.0), (222.0, 182.5), (228.0, 190.7), (234.0, 195.1)]
closed = treble + [(172.0, 146.5)] + horn_seen + est_bass + est_butt
json.dump({
    'units': 'mm', 'frame': 'guitar frame of v2/web/parts.json (X to the neck, Y to the bass side), on the body top, Z = 0',
    'segments': [
        {'name': 'treble_edge_and_cutaway', 'source': 'IMG_2407 alpha mask, rectified with the nine screw holes', 'accuracy_mm': 3.0,
         'points': treble},
        {'name': 'bass_horn_lower_edge', 'source': 'IMG_2408, red body against the wall, rectified', 'accuracy_mm': 4.0, 'points': horn_seen},
        {'name': 'bass_edge', 'source': 'estimate: not in either photo; IMG_2408 shows body up to its frame at Y 198 for X 40..228',
         'accuracy_mm': 10.0, 'points': est_bass},
        {'name': 'butt_end', 'source': 'estimate from the overall length; not in either photo', 'accuracy_mm': 20.0, 'points': est_butt}],
    'closed_polyline': closed,
    'neck_heel': 'The neck covers the body between Y 93.5 and 146.5 from X 166 on. The closed polyline jumps across it.',
}, open(os.path.join(WEB, 'body_outline.json'), 'w'), indent=1)
print('body outline: treble edge', len(treble), 'points, closed', len(closed))
