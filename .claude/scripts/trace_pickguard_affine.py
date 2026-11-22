#!/usr/bin/env python3
"""Affine fit of the forum scan to the 9 measured holes, then outline trace and overlay.
Args: <pickguard dir> <out dir>. Run with FreeCAD's Python, -I."""
import json, sys
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage as ni
pg, out = sys.argv[1], sys.argv[2]
meas = json.load(open(f'{pg}/ref/measured.json')); ref = json.load(open(f'{pg}/ref/sketch001.json'))
blobs = json.load(open(f'{out}/scan_blobs.json'))
edge = np.array([b[:2] for b in blobs if b[2] >= 200]); pu = np.array([b[:2] for b in blobs if 150 <= b[2] < 200])
# order from the similarity fit: (848,74)->(181.6,152.1) etc. Use nearest after a coarse map.
M = np.array([[h['cx'], h['cy']] for h in meas['guard_screws']])
sf = json.load(open(f'{out}/scan_fit.json')); k = complex(*sf['k']); t = complex(*sf['t'])
co = np.array([[(k * complex(*p) + t).real, (k * complex(*p) + t).imag] for p in edge])
idx = [int(np.argmin(np.hypot(*(M - c).T))) for c in co]; Mo = M[idx]
A = np.c_[edge, np.ones(len(edge))]
X, *_ = np.linalg.lstsq(A, Mo, rcond=None)   # frame = A @ X
pred = A @ X; r = np.hypot(*(pred - Mo).T)
print('affine residual mm', r.round(2), 'rms', round(float(np.sqrt((r ** 2).mean())), 2), 'max', round(float(r.max()), 2))
u, s, vt = np.linalg.svd(X[:2, :2]); print('scales mm/px', s.round(4), 'anisotropy %', round(float((s[0] / s[1] - 1) * 100), 2), 'det sign', np.sign(np.linalg.det(X[:2, :2])))
Pu = np.c_[pu, np.ones(len(pu))] @ X
print('HH screws', Pu.round(1).tolist(), 'spacing', round(float(np.hypot(*(Pu[0] - Pu[2]))), 2), round(float(np.hypot(*(Pu[1] - Pu[3]))), 2), 'width', round(float(np.hypot(*(Pu[0] - Pu[1]))), 2))
Inv = np.linalg.inv(np.c_[X, [0, 0, 1]])
def f2i(p):
    v = np.c_[np.atleast_2d(p), np.ones(len(np.atleast_2d(p)))] @ Inv; return v[:, :2]
g = np.asarray(Image.open(f'{pg}/ref/forum-scan/bullet-mustang-hh-scan.jpg').convert('L'), float); gs = ni.gaussian_filter(g, 0.8)
ol = np.array(ref['outline'], float); seg = np.hypot(*np.diff(np.vstack([ol, ol[:1]]), axis=0).T); keep = [0]; acc = 0
for i in range(1, len(ol)):
    acc += seg[i - 1]
    if acc >= 1.2: keep.append(i); acc = 0
P = ol[keep]; T_ = np.roll(P, -1, 0) - np.roll(P, 1, 0); Ns = np.c_[T_[:, 1], -T_[:, 0]]; Ns /= np.maximum(np.hypot(*Ns.T), 1e-6)[:, None]
if 0.5 * np.sum(P[:, 0] * np.roll(P[:, 1], -1) - np.roll(P[:, 0], -1) * P[:, 1]) < 0: Ns = -Ns
off = np.arange(-4, 4.01, 0.05); det = []; conf = []
for p, n in zip(P, Ns):
    ii = f2i(p + off[:, None] * n); v = ni.map_coordinates(gs, [ii[:, 1], ii[:, 0]], order=1, mode='nearest')
    dip = ni.median_filter(v, size=61) - v; j = int(dip.argmax()); det.append(off[j]); conf.append(dip[j])
det = np.array(det); conf = np.array(conf); good = conf >= 10
print('points', len(P), 'good frac', round(float(good.mean()), 2), 'offset pct 5/50/95', np.percentile(det[good], [5, 50, 95]).round(2))
d2 = det.copy(); d2[~good] = np.nan; n_ = len(d2)
sm = np.array([np.nanmedian(np.take(d2, range(i - 6, i + 7), mode='wrap')) if np.isfinite(np.take(d2, range(i - 6, i + 7), mode='wrap')).any() else np.nan for i in range(n_)])
for a in range(0, n_, n_ // 10):
    b = min(a + n_ // 10, n_); print(f'  from ({P[a][0]:6.1f},{P[a][1]:6.1f}) offset {np.nanmean(sm[a:b]):+.2f} max|{np.nanmax(np.abs(sm[a:b])):.2f}| good {good[a:b].mean():.2f}')
tog = np.c_[[[183.0, 354.0]], [1.0]] @ X; print('toggle in frame', tog.round(1))
Q = P + np.nan_to_num(sm)[:, None] * Ns
json.dump({'X': X.tolist(), 'outline_scan': Q.round(3).tolist(), 'offset': np.nan_to_num(sm).round(3).tolist(), 'conf': conf.round(1).tolist()}, open(f'{out}/scan_affine.json', 'w'))
vis = Image.open(f'{pg}/ref/forum-scan/bullet-mustang-hh-scan.jpg').convert('RGB'); dr = ImageDraw.Draw(vis)
dr.line([tuple(x) for x in f2i(np.vstack([ol, ol[:1]]))], fill=(0, 90, 255), width=2)
for w in M:
    x, y = f2i(w)[0]; dr.ellipse([x - 7, y - 7, x + 7, y + 7], outline=(0, 170, 0), width=2)
for p_ in meas['pickups'].values():
    for s_ in p_['screws']:
        x, y = f2i(s_)[0]; dr.ellipse([x - 7, y - 7, x + 7, y + 7], outline=(255, 140, 0), width=2)
for q in ref['pickups']:
    a = np.radians(q['angle']); d = np.array([np.cos(a), np.sin(a)]); e = np.array([-d[1], d[0]]); c = np.array([q['cx'], q['cy']]); L, Wd = q['length'] / 2, q['width'] / 2
    dr.line([tuple(x) for x in f2i(np.array([c + L * d + Wd * e, c + L * d - Wd * e, c - L * d - Wd * e, c - L * d + Wd * e, c + L * d + Wd * e]))], fill=(220, 0, 200), width=2)
dr.line([tuple(x) for x in f2i(np.array(meas['cavity'] + [meas['cavity'][0]]))], fill=(120, 60, 200), width=1)
vis.save(f'{out}/scan_overlay.png')
