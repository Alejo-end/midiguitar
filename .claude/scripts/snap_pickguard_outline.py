#!/usr/bin/env python3
"""Outline of the forum scan from hand seeds, snapped to the strongest edge, mapped to the Sketch001
frame with the affine fit from trace_pickguard_affine.py. Args: <pickguard dir> <out dir>."""
import json, sys
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage as ni
pg, out = sys.argv[1], sys.argv[2]
X = np.array(json.load(open(f'{out}/scan_affine.json'))['X'])
g = np.asarray(Image.open(f'{pg}/ref/forum-scan/bullet-mustang-hh-scan.jpg').convert('L'), float)
# Seeds in image px, going around: neck side concave, neck tab, treble curve, bridge cut, bass horn, bass curve.
# (y small = neck, y large = bridge in the frame X direction)
SEEDS = [(62,38),(80,52),(100,85),(125,122),(170,172),(222,212),(280,234),(340,238),(400,230),(460,207),(515,163),(530,166),(570,178),(650,186),(720,186),(780,181),(803,168),(812,140),(812,95),(814,28),(845,27),(880,40),(930,100),(980,205),(1020,320),(1046,440),(1059,560),(1060,680),(1051,800),(1032,900),(1000,980),(960,1030),(915,1062),(874,1076),(874,1030),(862,1010),(840,1005),(650,1000),(470,995),(446,998),(437,1012),(436,1081),(412,1075),(388,1060),(338,1008),(286,958),(210,945),(150,915),(148,870),(135,750),(120,680),(85,570),(50,430),(30,300),(27,200),(35,100),(48,48)]
S = np.array(SEEDS, float)
# resample polygon at 2 px
pts = []
for a, b in zip(S, np.roll(S, -1, 0)):
    n = max(int(np.hypot(*(b - a)) / 2), 1)
    pts += [a + (b - a) * i / n for i in range(n)]
P = np.array(pts); N = len(P)
sm = ni.gaussian_filter(P, sigma=(1.5, 0), mode='wrap')
T = np.roll(sm, -1, 0) - np.roll(sm, 1, 0); Nn = np.c_[T[:, 1], -T[:, 0]]; Nn /= np.maximum(np.hypot(*Nn.T), 1e-6)[:, None]
gs = ni.gaussian_filter(g, 1.0)
offs = np.arange(-9, 9.01, 0.5); M = len(offs)
cost = np.zeros((N, M))
for i in range(N):
    q = P[i] + offs[:, None] * Nn[i]
    v = ni.map_coordinates(gs, [q[:, 1], q[:, 0]], order=1, mode='nearest')
    d = np.abs(np.gradient(v, 0.5))
    cost[i] = -d + 0.02 * np.abs(offs)
# circular DP (Viterbi, one wrap fix by running from best start)
pen = 1.2
D = cost[0].copy(); back = np.zeros((N, M), int)
for i in range(1, N):
    C = D[None, :] + pen * np.abs(offs[:, None] - offs[None, :])
    back[i] = C.argmin(1); D = C.min(1) + cost[i]
j = int(D.argmin()); sel = np.zeros(N, int)
for i in range(N - 1, -1, -1):
    sel[i] = j; j = back[i, j] if i else j
Q = P + offs[sel][:, None] * Nn
# light smoothing that keeps corners: median then small gaussian on the offsets
of2 = ni.median_filter(offs[sel], size=7, mode='wrap'); of2 = ni.gaussian_filter1d(of2, 1.5, mode='wrap')
Q = P + of2[:, None] * Nn
F = np.c_[Q, np.ones(N)] @ X
strength = (-cost[np.arange(N), sel])
print('points', N, 'snap offset px pct', np.percentile(np.abs(offs[sel]), [50, 90, 99]).round(1), 'edge strength pct10/50', np.percentile(strength, [10, 50]).round(1))
A = 0.5 * np.sum(F[:, 0] * np.roll(F[:, 1], -1) - np.roll(F[:, 0], -1) * F[:, 1]); print('area mm2', round(abs(A)), 'bbox', F.min(0).round(1), F.max(0).round(1))
json.dump({'outline': F[::2].round(3).tolist(), 'source': 'Forum scan of a Bullet Mustang guard, squier-talk.com thread 201274, snapped to edges, affine fit to the nine measured holes'}, open(f'{out}/scan_outline.json', 'w'))
vis = Image.open(f'{pg}/ref/forum-scan/bullet-mustang-hh-scan.jpg').convert('RGB').resize((2182, 2400), Image.BICUBIC); dr = ImageDraw.Draw(vis)
dr.line([tuple(x * 2) for x in np.vstack([Q, Q[:1]])], fill=(255, 0, 0), width=1)
vis.crop((0, 0, 1100, 1200)).save(f'{out}/snap_a.png'); vis.crop((1082, 0, 2182, 1200)).save(f'{out}/snap_b.png'); vis.crop((0, 1200, 1100, 2400)).save(f'{out}/snap_c.png'); vis.crop((1082, 1200, 2182, 2400)).save(f'{out}/snap_d.png')
