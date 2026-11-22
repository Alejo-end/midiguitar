# Where the plate falls apart, and whether the STL meshes are printable.
# Run: CHECK_FILE=... CHECK_OUT=... STL_DIR=... freecadcmd check_pickguard_mesh.py
import glob, os
import FreeCAD as App
import Mesh, Part

out = open(os.environ['CHECK_OUT'], 'w')


def print(*a):
    out.write(' '.join(str(x) for x in a) + '\n')
    out.flush()


doc = App.openDocument(os.environ['CHECK_FILE'])
g = {o.Name: o for o in doc.getObject('REINS_Pickguard').Group}
for n in ('Pickguard', 'PlyBottom', 'PlyMiddle', 'PlyTop', 'FitTest'):
    s = g[n].Shape
    print(n, 'solids', len(s.Solids), 'valid', s.isValid())
    for so in s.Solids:
        bb = so.BoundBox
        print(f'   vol {so.Volume:9.1f} valid {so.isValid()} x {bb.XMin:.1f}..{bb.XMax:.1f} y {bb.YMin:.1f}..{bb.YMax:.1f} z {bb.ZMin:.1f}..{bb.ZMax:.1f}')
    if not s.isValid():
        try:
            s.check(True)
        except Exception as e:
            print('   check:', str(e)[:900])
for f in sorted(glob.glob(os.environ['STL_DIR'] + '/*.stl')):
    m = Mesh.Mesh(f)
    print(os.path.basename(f), 'facets', m.CountFacets, 'solid', m.isSolid(), 'nonmanifold', m.hasNonManifolds(), 'selfint', m.hasSelfIntersections(), 'components', len(m.getSeparateComponents()))
os._exit(0)
