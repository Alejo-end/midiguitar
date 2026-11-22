#!/usr/bin/env python3
"""Fit the forum scan of a Bullet Mustang pickguard to the Sketch001 frame and compare it with
the sketch outline. Run with FreeCAD's Python (numpy, scipy, PIL):
  /Applications/FreeCAD.app/Contents/Resources/bin/python -I trace_pickguard_scan.py <pickguard dir> <out dir>
"""
import json, os, sys
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage as ni

pg, out = sys.argv[1], sys.argv[2]
meas = json.load(open(f'{pg}/ref/measured.json'))
ref = json.load(open(f'{pg}/ref/sketch001.json'))
img = Image.open(f'{pg}/ref/forum-scan/bullet-mustang-hh-scan.jpg').convert('L')
g = np.asarray(img, float)
H, W = g.shape
print('image', W, H, 'gray pct', np.percentile(g, [1, 5, 50, 95]))

# Holes: small round dark blobs against a local background.
bg = ni.median_filter(g, size=41)
d = bg - g
mask = d > 25
lab, n = ni.label(mask)
blobs = []
for i, sl in enumerate(ni.find_objects(lab), 1):
    h, w = sl[0].stop - sl[0].start, sl[1].stop - sl[1].start
    area = (lab[sl] == i).sum()
    if 15 <= area <= 500 and 0.6 < w / max(h, 1) < 1.7 and area / (w * h) > 0.45:
        yy, xx = np.nonzero(lab[sl] == i)
        wt = d[sl][yy, xx]
        blobs.append((round(float((xx * wt).sum() / wt.sum() + sl[1].start), 1), round(float((yy * wt).sum() / wt.sum() + sl[0].start), 1), int(area), w, h))
print('blobs', len(blobs)); [print(' ', b) for b in blobs]
json.dump(blobs, open(f'{out}/scan_blobs.json', 'w'))

# Fit: similarity (no reflection in y-down image coords: the scan is face down) to the 9 measured holes.
EDGE = [b[:2] for b in blobs if b[2] >= 200]
PU = [b[:2] for b in blobs if 150 <= b[2] < 200]
M = np.array([[h['cx'], h['cy']] for h in meas['guard_screws']])
Z = np.array([complex(*p) for p in EDGE]); Wm = np.array([complex(*p) for p in M])
from scipy.optimize import linear_sum_assignment as lsa
from itertools import permutations
best = None
for a, b in permutations(range(len(Z)), 2):
    for c, e in permutations(range(9), 2):
        k = (Wm[c] - Wm[e]) / (Z[a] - Z[b])
        if not .1 < abs(k) < .4: continue
        T = k * Z + Wm[c] - k * Z[a]
        C = np.abs(T[:, None] - Wm[None, :])
        r, co = lsa(C); s = C[r, co].max()
        if best is None or s < best[0]: best = (s, co.copy())
order = best[1]
W2 = Wm[order]
def fit(Zs, Ws):
    zm, wm = Zs.mean(), Ws.mean()
    k = ((Ws - wm) * np.conj(Zs - zm)).sum() / (np.abs(Zs - zm) ** 2).sum()
    return k, wm - k * zm
k, t = fit(Z, W2)
res = np.abs(k * Z + t - W2)
print('fit mm/px', round(abs(k), 4), 'dpi', round(25.4 / abs(k), 1), 'rot deg', round(np.degrees(np.angle(k)), 2))
print('residual mm', np.round(res, 2), 'rms', round(float(np.sqrt((res ** 2).mean())), 2))
for z, w, r in zip(Z, W2, res):
    p = k * z + t
    print(f'  scan->frame ({p.real:7.2f},{p.imag:7.2f}) measured ({w.real:7.2f},{w.imag:7.2f}) d=({p.real-w.real:+.2f},{p.imag-w.imag:+.2f})')
PUm = [k * complex(*p) + t for p in PU]
print('HH screws in frame', [(round(p.real, 1), round(p.imag, 1)) for p in PUm], 'spacing mm', round(abs(PUm[0] - PUm[2]), 2), round(abs(PUm[1] - PUm[3]), 2), round(abs(PUm[0] - PUm[1]), 2))
# Toggle hole: find the bright disc inside the tape patch around screen (562,240) -> image px
tog = complex(562 * 1200 / 735 - (450 * 1200 / 735), 240 * 1200 / 735 - 23 * 1200 / 735)
print('toggle approx image px', tog, 'frame', k * tog + t)
cav = meas['cavity']; ca = np.array(cav)
print('cavity bbox', ca.min(0), ca.max(0))

# Inverse map: frame -> image
def f2i(x, y):
    z = (complex(x, y) - t) / k
    return z.real, z.imag
# Outline: from each sketch outline point search along the normal for the darkest valley.
ol = np.array(ref['outline'], float)
# resample at about 1.5 mm
seg = np.hypot(*np.diff(np.vstack([ol, ol[:1]]), axis=0).T); keep = [0]; acc = 0
for i in range(1, len(ol)):
    acc += seg[i - 1]
    if acc >= 1.5: keep.append(i); acc = 0
P = ol[keep]
Ns = np.roll(P, -1, 0) - np.roll(P, 1, 0); Ns = np.c_[Ns[:, 1], -Ns[:, 0]]; Ns /= np.maximum(np.hypot(*Ns.T), 1e-6)[:, None]
area = 0.5 * np.sum(P[:, 0] * np.roll(P[:, 1], -1) - np.roll(P[:, 0], -1) * P[:, 1])
if area < 0: Ns = -Ns  # outward for a CCW polygon has (dy,-dx)
gs = ni.gaussian_filter(g, 0.8)
off = np.arange(-8, 8.01, 0.1)
det = []; conf = []
for p, n in zip(P, Ns):
    q = p[None, :] + off[:, None] * n[None, :]
    ii = np.array([f2i(*x) for x in q])
    v = ni.map_coordinates(gs, [ii[:, 1], ii[:, 0]], order=1, mode='nearest')
    base = ni.median_filter(v, size=41)
    dip = base - v
    j = int(dip.argmax()); det.append(off[j]); conf.append(dip[j])
det = np.array(det); conf = np.array(conf)
print('normal offset scan vs sketch mm: pct5,50,95', np.percentile(det, [5, 50, 95]).round(2), 'conf median', round(float(np.median(conf)), 1), 'low-conf frac', round(float((conf < 8).mean()), 2))
# robust smoothing
good = conf >= 8
det2 = det.copy(); det2[~good] = np.nan
n_ = len(det2)
sm = np.array([np.nanmedian(np.take(det2, range(i - 5, i + 6), mode='wrap')) for i in range(n_)])
sm = np.where(np.isnan(sm), 0, sm)
print('smoothed offset abs: median', round(float(np.median(np.abs(sm))), 2), 'max', round(float(np.abs(sm).max()), 2))
for a, b in [(0, n_ // 8), (n_ // 8, n_ // 4), (n_ // 4, 3 * n_ // 8), (3 * n_ // 8, n_ // 2), (n_ // 2, 5 * n_ // 8), (5 * n_ // 8, 3 * n_ // 4), (3 * n_ // 4, 7 * n_ // 8), (7 * n_ // 8, n_)]:
    print(f'  arc {a}-{b} from ({P[a][0]:.0f},{P[a][1]:.0f}) mean offset {sm[a:b].mean():+.2f} max {np.abs(sm[a:b]).max():.2f}')
Q = P + sm[:, None] * Ns
json.dump({'outline': Q.round(3).tolist(), 'fit_mm_per_px': float(abs(k)), 'k': [k.real, k.imag], 't': [t.real, t.imag], 'edge_holes_frame': [[(k * z + t).real, (k * z + t).imag] for z in Z]}, open(f'{out}/scan_fit.json', 'w'))
# Overlay in image coordinates
vis = Image.open(f'{pg}/ref/forum-scan/bullet-mustang-hh-scan.jpg').convert('RGB'); dr = ImageDraw.Draw(vis)
dr.line([f2i(*x) for x in np.vstack([ol, ol[:1]])], fill=(0, 90, 255), width=2)
dr.line([f2i(*x) for x in np.vstack([Q, Q[:1]])], fill=(255, 40, 40), width=1)
for w in Wm:
    x, y = f2i(w.real, w.imag); dr.ellipse([x - 6, y - 6, x + 6, y + 6], outline=(0, 160, 0), width=2)
for p_ in meas['pickups'].values():
    for s in p_['screws']:
        x, y = f2i(*s); dr.ellipse([x - 6, y - 6, x + 6, y + 6], outline=(255, 140, 0), width=2)
dr.line([f2i(*x) for x in ca.tolist() + [ca[0].tolist()]], fill=(160, 0, 200), width=2)
vis.save(f'{out}/scan_overlay.png')
