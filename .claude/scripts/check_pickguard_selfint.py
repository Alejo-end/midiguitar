# Locates self intersections in the pickguard STL files. Env: CHECK_OUT, STL_DIRS (colon separated).
import os
import FreeCAD as App
import Mesh

out = open(os.environ['CHECK_OUT'], 'w')


def print(*a):
    out.write(' '.join(str(x) for x in a) + '\n')
    out.flush()


for d in os.environ['STL_DIRS'].split(':'):
    for n in ('reins-pickguard.stl', 'reins-pickguard-ply3-top.stl'):
        m = Mesh.Mesh(os.path.join(d, n))
        pairs = m.getSelfIntersections()
        print(d[-24:], n, 'self intersecting pairs', len(pairs))
        spots = {}
        F = m.Facets
        for p in pairs[:400]:
            f = F[p[0]]
            c = [sum(q[i] for q in f.Points) / 3 for i in range(3)]
            k = (round(c[0] / 5) * 5, round(c[1] / 5) * 5, round(c[2], 1))
            spots[k] = spots.get(k, 0) + 1
        print('   ', sorted(spots.items(), key=lambda x: -x[1])[:14])
os._exit(0)
