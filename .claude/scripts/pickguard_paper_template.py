#!/usr/bin/env python3
"""A4 paper template of the REINS pickguard at 1:1, to test the outline, the nine screw holes and
the two candidate pickup positions on the real guitar.
Run with FreeCAD's Python: bin/python -I pickguard_paper_template.py <pickguard dir>
"""
import json, math, sys
import matplotlib
matplotlib.use('pdf')
import matplotlib.pyplot as plt
import numpy as np

pg = sys.argv[1]
meas = json.load(open(f'{pg}/ref/measured.json'))
ref = json.load(open(f'{pg}/ref/sketch001.json'))
P = np.array(json.load(open(f'{pg}/ref/scan_outline.json'))['outline'])
A_COL, B_COL, INK, FAINT = '#1457c8', '#c81e1e', '#000000', '#8a8a8a'


def stadium(c, L, W, ang, n=48):
    r, a = W / 2, math.radians(ang)
    t = np.linspace(-math.pi / 2, math.pi / 2, n)
    right = np.c_[L / 2 - r + r * np.cos(t), r * np.sin(t)]
    pts = np.r_[right, -right, right[:1]]
    R = np.array([[math.cos(a), -math.sin(a)], [math.sin(a), math.cos(a)]])
    return pts @ R.T + np.array(c)


def cross(ax, x, y, col, size=3.0, lw=0.5):
    ax.plot([x - size, x + size], [y, y], color=col, lw=lw)
    ax.plot([x, x], [y - size, y + size], color=col, lw=lw)


fig = plt.figure(figsize=(210 / 25.4, 297 / 25.4))
ax = fig.add_axes([0, 0, 1, 1])
cx = (P[:, 0].min() + P[:, 0].max()) / 2
top = P[:, 1].max() + 6
ax.set_xlim(cx - 105, cx + 105)
ax.set_ylim(top - 297, top)
ax.set_aspect('equal')
ax.axis('off')

ax.plot(*np.r_[P, P[:1]].T, color=INK, lw=0.7)
for h in meas['guard_screws']:
    ax.add_patch(plt.Circle((h['cx'], h['cy']), 1.75, fill=False, color=INK, lw=0.5))
    cross(ax, h['cx'], h['cy'], INK)
for op, m, name in zip(ref['pickups'], (meas['pickups']['bridge'], meas['pickups']['neck']), ('bridge', 'neck')):
    ax.plot(*stadium((op['cx'], op['cy']), op['length'] + .2, op['width'] + .2, op['angle']).T, color=A_COL, lw=0.7)
    ax.plot(*stadium((m['cx'], m['cy']), 70.0, 18.0, m['angle']).T, color=B_COL, lw=0.7, ls=(0, (4, 2)))
    for s in m['screws']:
        cross(ax, s[0], s[1], B_COL)
    ax.text(op['cx'], op['cy'], f'{name} pickup', ha='center', va='center', fontsize=7, rotation=op['angle'] - 90)
near = [c for c in ref['circles'] if c['n'] == 24]
for s in near:
    if any(math.dist((s['cx'], s['cy']), q) < 5 for m in meas['pickups'].values() for q in m['screws']):
        ax.plot(s['cx'], s['cy'], marker='x', ms=5, mew=0.6, color=A_COL)
for r in ref['rects']:
    if r['cy'] > 150:
        ax.plot(*np.r_[r['pts'], r['pts'][:1]].T, color=FAINT, lw=0.5)
ax.plot(*np.r_[meas['cavity'], meas['cavity'][:1]].T, color=FAINT, lw=0.5, ls=':')

# Notes and scale bar under the drawing.
x0, y = P[:, 0].min() + 2, P[:, 1].min() - 12
ax.plot([x0, x0 + 100], [y, y], color=INK, lw=1.0)
for i in range(0, 101, 10):
    ax.plot([x0 + i, x0 + i], [y, y + (3 if i % 50 == 0 else 1.5)], color=INK, lw=0.5)
ax.text(x0 + 104, y, 'This bar must measure 100 mm. Print at 100 %, with no "fit to page".', va='center', fontsize=7)
notes = [
    (INK, 'Black: outline and the nine pickguard screw holes.'),
    (A_COL, 'Blue line and blue x: A, the pickup openings and pickup screws from your sketch (Sketch001).'),
    (B_COL, 'Red dashed line and red +: B, a 70 x 18 mm cover centred on the pickup screws measured from the photos.'),
    (FAINT, 'Gray: the two switch slots. Dotted: the board cavity.'),
    (INK, '1. Hold the sheet on the stock pickguard, and line up the nine black holes with the nine real holes.'),
    (INK, '2. Look at where the real pickup screws and covers are: on A (blue) or on B (red). Push a pin through to check.'),
    (INK, '3. Check that the black outline follows the edge of the stock pickguard, and measure any gap.'),
]
y -= 9
for col, line in notes:
    ax.text(x0, y, line, color=col, fontsize=8, va='center')
    y -= 6
fig.savefig(f'{pg}/reins-pickguard-paper-template.pdf')
print('written', f'{pg}/reins-pickguard-paper-template.pdf', 'lowest text y', round(y, 1), 'page bottom', round(top - 297, 1))
