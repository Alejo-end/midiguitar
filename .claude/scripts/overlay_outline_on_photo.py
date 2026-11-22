#!/usr/bin/env python3
"""Draw outlines on the guitar photo stored in cad-midiguitar.FCStd. Args: <pickguard dir> <out dir> <photo>."""
import json, math, sys
import numpy as np
from PIL import Image, ImageDraw
pg, out, ph = sys.argv[1:4]
ref = json.load(open(f'{pg}/ref/sketch001.json')); meas = json.load(open(f'{pg}/ref/measured.json')); sc = json.load(open(f'{out}/scan_outline.json'))
im = Image.open(ph).convert('RGB'); W, H = im.size; print(W, H)
Px, Py, A, XS, YS = 86.25, 88.11, -1.6929693744344996, 243.42, 324.56
c, s = math.cos(A), math.sin(A)
def toimg(p):
    dx, dy = p[0] - Px, p[1] - Py
    u = c * dx + s * dy; v = -s * dx + c * dy
    return ((u / XS + 0.5) * W, (0.5 - v / YS) * H)
d = ImageDraw.Draw(im); lw = max(W // 600, 3)
for name, pts, col in (('sketch', ref['outline'], (0, 120, 255)), ('scan', sc['outline'], (255, 30, 30))):
    q = [toimg(p) for p in pts]; d.line(q + q[:1], fill=col, width=lw)
for h in meas['guard_screws']:
    x, y = toimg((h['cx'], h['cy'])); r = W // 110; d.ellipse([x - r, y - r, x + r, y + r], outline=(0, 255, 0), width=lw)
for p in meas['pickups'].values():
    for sc_ in p['screws']:
        x, y = toimg(sc_); r = W // 110; d.ellipse([x - r, y - r, x + r, y + r], outline=(255, 160, 0), width=lw)
for q in ref['pickups']:
    a = math.radians(q['angle']); da = np.array([math.cos(a), math.sin(a)]); e = np.array([-da[1], da[0]]); c0 = np.array([q['cx'], q['cy']]); L, Wd = q['length'] / 2, q['width'] / 2
    r_ = [toimg(c0 + L * da + Wd * e), toimg(c0 + L * da - Wd * e), toimg(c0 - L * da - Wd * e), toimg(c0 - L * da + Wd * e)]; d.line(r_ + r_[:1], fill=(255, 0, 255), width=lw)
xs, ys = zip(*[toimg(p) for p in ref['outline']]); m = W // 25
box = (max(min(xs) - m, 0), max(min(ys) - m, 0), min(max(xs) + m, W), min(max(ys) + m, H)); c2 = im.crop(tuple(map(int, box))); c2.thumbnail((1500, 1500)); c2.save(f'{out}/photo_overlay.png'); print(box, c2.size)
