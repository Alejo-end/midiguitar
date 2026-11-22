#!/usr/bin/env python3
"""Reads reins-guitar.kicad_pcb and checks copper connectivity by net, ground pour islands, pad
positions, screw head clearance, board edge margins and silkscreen text. Arg: <kicad_pcb path>."""
import collections, math, re, sys


def parse(t):
    toks = re.findall(r'"(?:\\.|[^"\\])*"|[()]|[^\s()]+', t)
    st = [[]]
    for k in toks:
        if k == '(':
            st.append([])
        elif k == ')':
            x = st.pop()
            st[-1].append(x)
        else:
            st[-1].append(k[1:-1] if k[0] == '"' else k)
    return st[0][0]


def get(n, k):
    return [c for c in n if isinstance(c, list) and c and c[0] == k]


b = parse(open(sys.argv[1]).read())
F = lambda v: float(v)


def rot(x, y, a):
    r = math.radians(a)
    return x * math.cos(r) + y * math.sin(r), -x * math.sin(r) + y * math.cos(r)


pads = []
for f in get(b, 'footprint'):
    at = get(f, 'at')[0]
    fx, fy, fa = F(at[1]), F(at[2]), F(at[3]) if len(at) > 3 else 0.0
    side = get(f, 'layer')[0][1]
    ref = [p[2] for p in get(f, 'property') if p[1] == 'Reference'][0]
    for p in get(f, 'pad'):
        pa = get(p, 'at')[0]
        dx, dy = rot(F(pa[1]), F(pa[2]), fa)
        n = get(p, 'net')
        lay = get(p, 'layers')[0][1:]
        sz = get(p, 'size')[0][1:]
        pads.append(dict(ref=ref, num=p[1], typ=p[2], x=fx + dx, y=fy + dy, net=n[0][-1] if n else None, layers=lay, side=side, size=(F(sz[0]), F(sz[1]))))

parent = {}


def find(a):
    parent.setdefault(a, a)
    while parent[a] != a:
        parent[a] = parent[parent[a]]
        a = parent[a]
    return a


def union(a, b_):
    parent[find(a)] = find(b_)


def key(x, y, l):
    return (round(x, 2), round(y, 2), l)


tracks = []
for s in get(b, 'segment'):
    a, e = get(s, 'start')[0], get(s, 'end')[0]
    tracks.append(dict(x0=F(a[1]), y0=F(a[2]), x1=F(e[1]), y1=F(e[2]), w=F(get(s, 'width')[0][1]), l=get(s, 'layer')[0][1], net=get(s, 'net')[0][-1]))
vias = [dict(x=F(get(v, 'at')[0][1]), y=F(get(v, 'at')[0][2]), net=get(v, 'net')[0][-1]) for v in get(b, 'via')]
polys = []
for z in get(b, 'zone'):
    for fp in get(z, 'filled_polygon'):
        pts = [(F(q[1]), F(q[2])) for q in get(get(fp, 'pts')[0], 'xy')]
        polys.append(dict(l=get(fp, 'layer')[0][1], pts=pts, net='/GND', id=len(polys), area=abs(sum(p[0] * q[1] - q[0] * p[1] for p, q in zip(pts, pts[1:] + pts[:1]))) / 2))
print('filled polygons', [(p['l'], round(p['area'], 1)) for p in polys])


def pip(x, y, pts):
    c = False
    for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1]):
        if (y0 > y) != (y1 > y) and x < x0 + (y - y0) * (x1 - x0) / (y1 - y0):
            c = not c
    return c


def seg_dist(px, py, t):
    dx, dy = t['x1'] - t['x0'], t['y1'] - t['y0']
    L = dx * dx + dy * dy
    u = max(0, min(1, ((px - t['x0']) * dx + (py - t['y0']) * dy) / L)) if L else 0
    return math.hypot(px - t['x0'] - u * dx, py - t['y0'] - u * dy)


def on_layer(p, l):
    return l in p['layers'] or '*.Cu' in p['layers'] or (l == 'F.Cu' and 'F&B.Cu' in p['layers'])


for net in sorted({p['net'] for p in pads if p['net']}):
    nodes = []
    ps = [p for p in pads if p['net'] == net]
    ts = [t for t in tracks if t['net'] == net]
    vs = [v for v in vias if v['net'] == net]
    pz = [z for z in polys if z['net'] == net]
    parent.clear()
    for i, p in enumerate(ps):
        find(('p', i))
        for j, t in enumerate(ts):
            if on_layer(p, t['l']) and seg_dist(p['x'], p['y'], t) < max(p['size']) / 2 + t['w'] / 2:
                union(('p', i), ('t', j))
        for k, z in enumerate(pz):
            if on_layer(p, z['l']) and pip(p['x'], p['y'], z['pts']):
                union(('p', i), ('z', k))
    for j, t in enumerate(ts):
        find(('t', j))
        for j2, t2 in enumerate(ts):
            if j2 > j and t['l'] == t2['l']:
                for x, y in ((t2['x0'], t2['y0']), (t2['x1'], t2['y1'])):
                    if seg_dist(x, y, t) < (t['w'] + t2['w']) / 2 * 0.5 + 0.01:
                        union(('t', j), ('t', j2))
                for x, y in ((t['x0'], t['y0']), (t['x1'], t['y1'])):
                    if seg_dist(x, y, t2) < (t['w'] + t2['w']) / 2 * 0.5 + 0.01:
                        union(('t', j), ('t', j2))
        for k, z in enumerate(pz):
            if t['l'] == z['l'] and (pip(t['x0'], t['y0'], z['pts']) or pip(t['x1'], t['y1'], z['pts'])):
                union(('t', j), ('z', k))
    for i, v in enumerate(vs):
        find(('v', i))
        for j, t in enumerate(ts):
            if seg_dist(v['x'], v['y'], t) < 0.3 + t['w'] / 2:
                union(('v', i), ('t', j))
        for k, z in enumerate(pz):
            if pip(v['x'], v['y'], z['pts']):
                union(('v', i), ('z', k))
    for k in range(len(pz)):
        find(('z', k))
    groups = collections.defaultdict(list)
    for n_ in list(parent):
        groups[find(n_)].append(n_)
    padg = collections.defaultdict(list)
    for i, p in enumerate(ps):
        padg[find(('p', i))].append(f"{p['ref']}.{p['num']}")
    print(f"net {net}: pads {len(ps)} tracks {len(ts)} vias {len(vs)} -> {len(padg)} pad groups" + ('' if len(padg) == 1 else f' OPEN {list(padg.values())}'))
    if pz:
        print('   pour parts not tied to a pad:', [(pz[n_[1]]['l'], round(pz[n_[1]]['area'], 1)) for n_ in parent if n_[0] == 'z' and find(n_) not in padg])

print('pad positions')
for p in pads:
    if p['ref'] in ('J1', 'J2', 'J3', 'J5', 'TP1', 'D1', 'D2', 'U1') or (p['ref'] == 'M1' and p['num'] in ('1', '7', '8', '14')):
        print(f"  {p['ref']}.{p['num']} ({p['x']:.2f},{p['y']:.2f}) {p['net']}")

holes = [(166.47, 114.38), (187.07, 115.76), (180.98, 140.26), (151.53, 140.6)]
print('copper within a 3.8 mm M2 head of each hole, pads only')
for i, (hx, hy) in enumerate(holes, 1):
    near = [(p['ref'], p['num'], p['net'], round(math.hypot(p['x'] - hx, p['y'] - hy) - max(p['size']) / 2, 2), p['layers'][0]) for p in pads if p['net'] and math.hypot(p['x'] - hx, p['y'] - hy) - max(p['size']) / 2 < 2.4]
    tr = [(t['net'], t['l'], round(seg_dist(hx, hy, t) - t['w'] / 2, 2)) for t in tracks if seg_dist(hx, hy, t) - t['w'] / 2 < 2.0]
    print(f'  H{i}', near, 'tracks', tr)

edges = []
for c in b:
    if isinstance(c, list) and c and c[0] in ('gr_line', 'gr_arc') and get(c, 'layer')[0][1] == 'Edge.Cuts':
        for k in ('start', 'mid', 'end'):
            q = get(c, k)
            if q:
                edges.append((F(q[0][1]), F(q[0][2])))
xs, ys = [e[0] for e in edges], [e[1] for e in edges]
print('edge bbox', min(xs), max(xs), min(ys), max(ys))
print('closest pads to the edge', sorted((round(min(p['x'] - min(xs), max(xs) - p['x'], p['y'] - min(ys), max(ys) - p['y']), 2), p['ref'], p['num']) for p in pads if p['net'])[:6])

print('thin connection widths on power nets', {n: min(t['w'] for t in tracks if t['net'] == n) for n in ('/5V', '/VBAT', '/VBAT_SW', '/LED_PWR', '/3V3')})

print('silkscreen text')
for t in get(b, 'gr_text'):
    print('  ', repr(t[1][:40]), get(t, 'layer')[0][1], get(t, 'at')[0][1:3])
for f in get(b, 'footprint'):
    ref = [p[2] for p in get(f, 'property') if p[1] == 'Reference'][0]
    if ref in ('D1', 'D2', 'U1', 'J1', 'J2', 'J3', 'TP1', 'J5'):
        marks = [c[0] for c in f if isinstance(c, list) and c and c[0] in ('fp_line', 'fp_poly', 'fp_circle', 'fp_text', 'fp_rect') and get(c, 'layer') and 'SilkS' in get(c, 'layer')[0][1]]
        txt = [(c[1], c[2][:24]) for c in f if isinstance(c, list) and c and c[0] == 'fp_text' and get(c, 'layer') and 'SilkS' in get(c, 'layer')[0][1]]
        print('  ', ref, collections.Counter(marks), txt)

print('GND through hole pads: how the pour reaches them (directions with copper at the pad edge + 0.05 mm, of 72)')
for p in pads:
    if p['net'] == '/GND' and p['typ'] == 'thru_hole':
        out = {}
        for l in ('F.Cu', 'B.Cu'):
            r0 = max(p['size']) / 2 + 0.05
            hits = sum(any(z['l'] == l and pip(p['x'] + r0 * math.cos(math.radians(a)), p['y'] + r0 * math.sin(math.radians(a)), z['pts']) for z in polys) for a in range(0, 360, 5))
            out[l] = hits
        print(f"  {p['ref']}.{p['num']} ({p['x']:.2f},{p['y']:.2f}) {out}")
sizes = collections.Counter()
for t in get(b, 'gr_text'):
    fnt = get(get(t, 'effects')[0], 'font')[0]
    sizes[(get(t, 'layer')[0][1], get(fnt, 'size')[0][1], (get(fnt, 'thickness') or [[0, '?']])[0][1])] += 1
print('silk text sizes', dict(sizes))
print('F.Cu small pour piece', [(round(min(q[0] for q in z['pts']), 1), round(max(q[0] for q in z['pts']), 1), round(min(q[1] for q in z['pts']), 1), round(max(q[1] for q in z['pts']), 1)) for z in polys if z['area'] < 100])
small = [z for z in polys if z['area'] < 100]
for z in small:
    print('  vias in it', [(v['x'], v['y']) for v in vias if v['net'] == '/GND' and pip(v['x'], v['y'], z['pts'])], 'pads', [f"{p['ref']}.{p['num']}" for p in pads if p['net'] == '/GND' and pip(p['x'], p['y'], z['pts'])])
