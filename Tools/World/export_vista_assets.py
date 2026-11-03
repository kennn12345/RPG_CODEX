"""Blender: RESTYLED world assets (R02) toward the Dreamscape: Tower video (style only, original meshes) -> World_R02/exports.
  blender -b --python export_vista_assets.py [-- name ...]      (names = only those, e.g. trees grass; default all)
Never touches the layout pieces (terrain, far land, vista_layout.json) - those stay in export_vista_meshes.py / World_R01.
Shape rules: the round trees keep R01's trunk, lean, branches and dome (leaf-cluster) positions exactly - the R01 code up to
the leaves is replayed with the same seeds (export_vista_meshes.fluffy_tree); only the canopy look is rebuilt.
Every export also gets a preview render (golden-hour light) in World_R02/previews and an entry in World_R02/exports/manifest.json
(dims m, triangles per LOD, hull count) that Tools/World/build_vista_assets.py checks sizes against."""
import bpy, bmesh, math, json, random, os, sys
from mathutils import Vector, noise

HERE = 'C:/UNREAL PROJECTS/RPG_CODEX/Tools/World'
OUT = 'C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs/World_R02'
EXP, PRE = OUT + '/exports', OUT + '/previews'
TREES2 = 'C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs/Trees_R02/textures'
os.makedirs(EXP, exist_ok=True); os.makedirs(PRE, exist_ok=True)
ARGS = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
WANT = set(ARGS) or None
def want(*k): return WANT is None or any(x in WANT for x in k)

# R01 helpers (tube, trunk_hull, obj, NKEYS/set_n/apply_normals, smoothstep, lin1) - definitions only, no exports
_src = open(HERE + '/export_vista_meshes.py').read()
_src = _src[:_src.index("T = terrain(); export(T, 'SM_Vista_Terrain')")]
R1 = {'__name__': 'vista_r01'}
exec(compile(_src, 'export_vista_meshes', 'exec'), R1)
tube, trunk_hull, obj, smoothstep, lin1 = R1['tube'], R1['trunk_hull'], R1['obj'], R1['smoothstep'], R1['lin1']
UP = Vector((0, 0, 1))
MANIFEST_PATH = EXP + '/manifest.json'
MANIFEST = json.load(open(MANIFEST_PATH)) if os.path.exists(MANIFEST_PATH) else {}

def lin(c): return tuple(lin1(v) for v in c)
def tris(ob): return sum(len(p.vertices) - 2 for p in ob.data.polygons)

def export(ob, name, extra=(), lods=()):
    """ob + UCX extras -> name.fbx; lods -> name_LOD<i>.fbx. Vertex colours LINEAR (as R01 world meshes)."""
    def one(o, fn, ext):
        bpy.ops.object.select_all(action='DESELECT')
        for x in [o] + list(ext): x.select_set(True)
        bpy.context.view_layer.objects.active = o
        bpy.ops.export_scene.fbx(filepath=f'{EXP}/{fn}.fbx', use_selection=True, axis_forward='-Y', axis_up='Z',
                                 mesh_smooth_type='FACE', colors_type='LINEAR')
    one(ob, name, extra)
    for i, l in enumerate(lods): one(l, f'{name}_LOD{i + 1}', ())
    MANIFEST[name] = dict(dims=[round(v, 3) for v in ob.dimensions], triangles=[tris(ob)] + [tris(l) for l in lods], hulls=len(extra))
    json.dump(MANIFEST, open(MANIFEST_PATH, 'w'), indent=1)
    print('EXPORTED', name, MANIFEST[name])

# ---------------------------------------------------------------- mesh accumulator (verts + colour + normal + uv + mat)
class Mesh:
    def __init__(s): s.v = []; s.c = []; s.n = []; s.f = []; s.uv = []; s.m = []
    def vert(s, co, col, n=None): s.v.append(tuple(co)); s.c.append(tuple(col)); s.n.append(n); return len(s.v) - 1
    def face(s, idx, uvs, mat=0): s.f.append(tuple(idx)); s.uv.append(uvs); s.m.append(mat)
    def build(s, name, mats, smooth=True):
        me = bpy.data.meshes.new(name); me.from_pydata(s.v, [], s.f); me.update()
        uvl = me.uv_layers.new(name='UVMap'); uvl.data.foreach_set('uv', [c for f in s.uv for uv in f for c in uv])
        ca = me.color_attributes.new('Col', 'FLOAT_COLOR', 'POINT'); ca.data.foreach_set('color', [c for col in s.c for c in col])
        me.polygons.foreach_set('material_index', s.m)
        sm = smooth if isinstance(smooth, (list, tuple)) else [smooth] * len(mats)
        me.polygons.foreach_set('use_smooth', [bool(sm[m]) for m in s.m])
        for mn in mats: me.materials.append(bpy.data.materials.get(mn) or bpy.data.materials.new(mn))
        me.update()
        if any(n is not None for n in s.n):
            ln = []
            for p in me.polygons:
                for li in p.loop_indices:
                    n = s.n[me.loops[li].vertex_index]; ln.append(n if n is not None else (p.normal.copy() if not sm[s.m[p.index]] else me.vertices[me.loops[li].vertex_index].normal.copy()))
            me.normals_split_custom_set(ln)
        ob = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(ob); return ob

# ---------------------------------------------------------------- canopy pieces shared by round trees, bushes, ivy
# leaf colour ramp (video palette PALETTE.md): deep teal-green shade -> saturated yellow-green lit (same as Trees_R02)
RAMP = [(0, lin((26, 58, 36))), (.28, lin((50, 98, 32))), (.5, lin((96, 144, 24))), (.72, lin((160, 196, 28))), (1, lin((222, 232, 66)))]
def ramp(t):
    t = max(0.0, min(1.0, t))
    for (a, ca), (b, cb) in zip(RAMP, RAMP[1:]):
        if t <= b:
            k = (t - a) / (b - a); return tuple(ca[c] + (cb[c] - ca[c]) * k for c in range(3)) + (1,)
    return RAMP[-1][1] + (1,)

def bunch_card(M, rnd, p, facing, size, col, shade_n, mat=1):
    """folded leaf-bunch card (Trees_R02 Leaf_Cluster_Atlas 2x2), centred near p, facing ~facing."""
    tv = Vector((rnd.gauss(0, 1), rnd.gauss(0, 1), rnd.gauss(0, 1))) * .45 - UP * .35 + facing.cross(UP) * .5
    tv = tv - facing * tv.dot(facing)
    if tv.length < 1e-4: tv = facing.orthogonal()
    tip = tv.normalized(); side = tip.cross(facing).normalized(); fold = math.radians(rnd.uniform(14, 28))
    sl = -side * math.cos(fold) + facing * math.sin(fold); sr = side * math.cos(fold) + facing * math.sin(fold)
    b0 = p - tip * .45 * size; t0 = p + tip * .55 * size; w = .5 * size
    i = [M.vert(c, col, shade_n) for c in (b0, t0, t0 + sl * w, b0 + sl * w, b0 + sr * w, t0 + sr * w)]
    cell = rnd.randrange(4); u0 = (cell % 2) * .5; v0 = .5 if cell < 2 else 0.0
    um, ul, ur, vb, vt = u0 + .25, u0, u0 + .5, v0, v0 + .5
    M.face((i[0], i[1], i[2], i[3]), [(um, vb), (um, vt), (ul, vt), (ul, vb)], mat)
    M.face((i[0], i[4], i[5], i[1]), [(um, vb), (ur, vb), (ur, vt), (um, vt)], mat)

def icosphere(sub):
    bm = bmesh.new(); bmesh.ops.create_icosphere(bm, subdivisions=sub, radius=1.0)
    good = [v for v in bm.verts if v.is_valid]; idx = {v: i for i, v in enumerate(good)}
    vs = [v.co.copy() for v in good]; fs = [[idx[v] for v in f.verts] for f in bm.faces if f.is_valid]
    bm.free(); return vs, fs

def blob(M, centre, r, light_fn, tint, sub=1, squash=.75, seed=0.0, mat=2):
    """opaque lumpy canopy blob (M_Vista_Canopy: vertex G = light, B = tint)."""
    vs, fs = icosphere(sub); base = len(M.v)
    for u in vs:
        u = u.normalized()
        k = 1 + .14 * math.sin(u.x * 5.1 + seed) * math.cos(u.y * 4.3 + seed * 1.7) + .06 * math.sin(u.z * 9 + seed)
        p = centre + Vector((u.x, u.y, u.z * (squash if u.z < 0 else 1.0))) * r * k
        M.vert(p, (1.0, light_fn(p, u), tint, 1), u)
    for f in fs: M.face([base + i for i in f], [(0, 0)] * len(f), mat)

def dome_canopy(M, rnd, domes, C, zlo, zhi, leaf, density, lod):
    """leaf-bunch cards over every dome + dark cores (LOD0/1), or lit blobs only (LOD2)."""
    keep = (1.0, .42, 0)[lod]; grow = (1.0, 1.45, 0)[lod]
    for di, (c, r) in enumerate(domes):
        tint = .5 + rnd.uniform(-.08, .08)
        if lod == 2:
            blob(M, c, r * .97, lambda p, u: max(.15, min(1, .42 + .45 * u.z + .3 * (smoothstep(zlo, zhi, p.z) - .5))), tint, 2 if r > 1.0 else 1, .75, di * 2.3)
            continue
        blob(M, c, r * .6, lambda p, u: .10 + .14 * max(0, u.z), tint, 1, .75, di * 2.3)
        for k in range(int(density * r * r * keep)):
            u = Vector((rnd.gauss(0, 1), rnd.gauss(0, 1), rnd.gauss(0, 1))).normalized()
            if u.z < -.5 and rnd.random() < .6: continue
            f = rnd.uniform(.84, 1.04); p = c + u * r * f
            if any((p - c2).length < r2 * .78 for c2, r2 in domes if c2 is not c): continue
            up = smoothstep(zlo, zhi, p.z)
            t = .5 + .42 * u.z + .25 * (up - .5) + 1.0 * (f - .94) + (tint - .5) + rnd.gauss(0, .07)
            facing = (u + Vector((rnd.uniform(-.4, .4), rnd.uniform(-.4, .4), rnd.uniform(-.1, .5)))).normalized()
            shade_n = ((p - C).normalized() * .5 + u * .4 + UP * .15).normalized()
            bunch_card(M, rnd, p, facing, leaf * grow * rnd.uniform(.85, 1.15), ramp(t), shade_n)

# ---------------------------------------------------------------- round world trees (R01 shape replayed)
def round_tree(name, seed, trunk_h, canopy_r, clumps, lean=0.4, leaf_size=.5, density=210):
    """R01 fluffy_tree up to the canopy, verbatim (same rnd draws): trunk, lean, domes, branches. R02 canopy + LODs."""
    rnd = random.Random(seed); R1['rnd'] = rnd
    bm = bmesh.new(); col = bm.verts.layers.float_color.new('Col'); bm.loops.layers.uv.new('UVMap')
    for k in R1['NKEYS']: bm.verts.layers.float.new(k)
    top = Vector((rnd.uniform(-lean, lean), rnd.uniform(-lean, lean), trunk_h))
    mid = Vector((top.x * .35 + rnd.uniform(-.2, .2), top.y * .35 + rnd.uniform(-.2, .2), trunk_h * .5))
    tube(bm, col, Vector((0, 0, -.4)), mid, .42 * trunk_h / 6, .3 * trunk_h / 6, 10, flare=.7)
    tube(bm, col, mid, top, .3 * trunk_h / 6, .2 * trunk_h / 6, 10)
    C = top + Vector((0, 0, canopy_r * .4))
    domes = [(C + Vector((0, 0, canopy_r * .1)), canopy_r * .6)]
    for k in range(clumps):
        a = 2 * math.pi * k / clumps + rnd.uniform(-.35, .35); el = rnd.uniform(-.25, .45)
        d = Vector((math.cos(a) * math.cos(el), math.sin(a) * math.cos(el), math.sin(el) * .8))
        domes.append((C + d * canopy_r * rnd.uniform(.68, .92), canopy_r * rnd.uniform(.44, .6)))
    for k in range(clumps // 2 + 1):
        a = rnd.uniform(0, 2 * math.pi); el = rnd.uniform(-.1, .7)
        d = Vector((math.cos(a) * math.cos(el), math.sin(a) * math.cos(el), math.sin(el)))
        domes.append((C + d * canopy_r * rnd.uniform(1.0, 1.15), canopy_r * rnd.uniform(.28, .38)))
    for c, r in domes[1:]:
        tube(bm, col, top.lerp(mid, rnd.uniform(0, .4)), c, .14 * trunk_h / 6, .06, 6)
    zlo = min(c.z - r for c, r in domes); zhi = max(c.z + r for c, r in domes)
    # bark -> Mesh (faceted), with R01 bark UVs (around x height); moss weight 0 (material adds the ground collar)
    bark = Mesh()
    bm.verts.ensure_lookup_table(); vid = {}
    for v in bm.verts: vid[v] = bark.vert(v.co, (0, 1, .5, 1))
    for f in bm.faces:
        bark.face([vid[v] for v in f.verts], [((math.atan2(v.co.y, v.co.x) / math.pi + 1), v.co.z / 2.0) for v in f.verts], 0)
    bm.free()
    hull = trunk_hull(name, Vector((0, 0, -.3)), mid.lerp(top, .5), .42 * trunk_h / 6)
    lods = []
    for lod in range(3):
        M = Mesh(); M.v, M.c, M.n, M.f, M.uv, M.m = list(bark.v), list(bark.c), list(bark.n), list(bark.f), list(bark.uv), list(bark.m)
        dome_canopy(M, random.Random(seed * 31 + lod), domes, C, zlo, zhi, leaf_size * 1.25, density * .8, lod)
        ob = M.build(name if lod == 0 else f'{name}_LOD{lod}', ('Bark', 'Leaves', 'Fill'), (False, True, True))
        lods.append(ob)
    hull.parent = None
    return lods[0], [hull], lods[1:]

def decimate_mat(ob, mat_index, ratio):
    """decimate only the faces of one material (the bark) - keeps the canopy cards intact."""
    bpy.context.view_layer.objects.active = ob; bpy.ops.object.select_all(action='DESELECT'); ob.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='DESELECT')
    ob.active_material_index = mat_index; bpy.ops.object.material_slot_select()
    bpy.ops.mesh.decimate(ratio=ratio)
    bpy.ops.object.mode_set(mode='OBJECT')

# ---------------------------------------------------------------- grass, wheat, flowers
def blade(M, rnd, base, ang, lean, h, w, bend, segs=4, col_extra=(0, 0), mat=0):
    """curved tapered blade; vertex R = height 0..1 (wind + root->tip colour), G/B = col_extra."""
    d = Vector((math.cos(ang), math.sin(ang), 0)); side = Vector((-d.y, d.x, 0)) * rnd.uniform(.8, 1.0) + d * rnd.uniform(-.2, .2)
    rows = []; prev = None
    for s in range(segs + 1):
        t = s / segs; a = math.radians(lean + bend * t * t)
        p = base + d * (math.sin(a) * h * t) + Vector((0, 0, math.cos(a) * h * t))
        ww = w * (1 - t) ** .7 * (.75 + .5 * math.sin(min(1, t * 2.2) * math.pi / 2))   # widest a little above the root
        cn = (UP * .7 + d * .3).normalized()                                                  # soft upward normals (no dark faces)
        if s == segs: rows.append([M.vert(p, (t, *col_extra, 1), cn)]); break
        rows.append([M.vert(p - side * ww / 2, (t, *col_extra, 1), cn), M.vert(p + side * ww / 2, (t, *col_extra, 1), cn)])
    for s in range(segs - 1):
        M.face((rows[s][0], rows[s][1], rows[s + 1][1], rows[s + 1][0]), [(0, s / segs), (1, s / segs), (1, (s + 1) / segs), (0, (s + 1) / segs)], mat)
    M.face((rows[segs - 1][0], rows[segs - 1][1], rows[segs][0]), [(0, (segs - 1) / segs), (1, (segs - 1) / segs), (.5, 1)], mat)

def grass_clump(name, seed, blades, hmin, hmax, wid, spread, segs=4, lean=(6, 30), bend=(15, 55)):
    rnd = random.Random(seed); M = Mesh()
    for k in range(blades):
        a = rnd.uniform(0, 2 * math.pi); rr = spread * math.sqrt(rnd.random())
        out = a + rnd.uniform(-.5, .5)
        blade(M, rnd, Vector((math.cos(a) * rr, math.sin(a) * rr, -0.03)), out, rnd.uniform(*lean) * (.5 + rr / spread),
              rnd.uniform(hmin, hmax) * (1.1 - .3 * rr / spread), wid * rnd.uniform(.8, 1.2), rnd.uniform(*bend), segs)
    return M.build(name, ('Grass',))

def grass_lods(name, seed, blades, hmin, hmax, wid, spread, segs=4, **kw):
    ob = grass_clump(name, seed, blades, hmin, hmax, wid, spread, segs, **kw)
    l1 = grass_clump(name + '_LOD1', seed, max(4, int(blades * .45)), hmin, hmax, wid * 1.45, spread, 2, **kw)
    l2 = grass_clump(name + '_LOD2', seed, max(3, int(blades * .2)), hmin, hmax, wid * 2.0, spread, 1, **kw)
    return ob, [], [l1, l2]

def wheat(name, seed, stalks=26, lod=0):
    """golden wheat tuft: thin stalks + seed ears (vertex G = 1 on ears) + a few leaves."""
    rnd = random.Random(seed); M = Mesh(); spread = .3
    for k in range(stalks):
        a = rnd.uniform(0, 2 * math.pi); rr = spread * math.sqrt(rnd.random())
        base = Vector((math.cos(a) * rr, math.sin(a) * rr, -.03)); h = rnd.uniform(.85, 1.15)
        lean = Vector((rnd.uniform(-.08, .08), rnd.uniform(-.08, .08), 0)) + Vector((math.cos(a), math.sin(a), 0)) * rr * .3
        top = base + Vector((0, 0, h)) + lean * h
        # stalk: thin ribbon
        sd = (top - base).cross(UP).normalized() * .006 if lean.length > 1e-3 else Vector((.006, 0, 0))
        i = [M.vert(base - sd, (0, 0, 0, 1), UP), M.vert(base + sd, (0, 0, 0, 1), UP), M.vert(top + sd, (1, 0, 0, 1), UP), M.vert(top - sd, (1, 0, 0, 1), UP)]
        M.face(i, [(0, 0), (1, 0), (1, 1), (0, 1)])
        # ear: two crossed tapered quads, 11-14 cm, nodding a little
        ax = (lean * 2 + UP).normalized(); L = rnd.uniform(.11, .15)
        for side_ang in ((0, math.pi / 2) if lod == 0 else (0,)):
            sx = Vector((math.cos(a + side_ang), math.sin(a + side_ang), 0)) * .022
            e0 = top - ax * L * .1; e1 = top + ax * L
            j = [M.vert(e0 - sx * .6, (1, 1, 0, 1), UP), M.vert(e0 + sx * .6, (1, 1, 0, 1), UP),
                 M.vert(top + ax * L * .5 + sx, (1, 1, .5, 1), UP), M.vert(e1, (1, 1, 1, 1), UP), M.vert(top + ax * L * .5 - sx, (1, 1, .5, 1), UP)]
            M.face((j[0], j[1], j[2], j[3], j[4]), [(0, 0), (1, 0), (1, .5), (.5, 1), (0, .5)])
    for k in range(5 if lod == 0 else 2):                     # leaves near the base
        a = rnd.uniform(0, 2 * math.pi)
        blade(M, rnd, Vector((math.cos(a) * .1, math.sin(a) * .1, -.03)), a, rnd.uniform(20, 40), rnd.uniform(.35, .55), .03, rnd.uniform(30, 60), 3)
    return M.build(name, ('Wheat',))

def flower_clump(name, seed, kind, heads=5, lod=0):
    """poppies (kind 'poppy': 4 cupped red petals, dark centre, buds) or daisies (kind 'daisy': 12-14 thin white
    petals, yellow centre). Vertex: R = height (wind), G = 0 stem/leaf .. .5 centre .. 1 petal, B = petal shade 0 base .. 1 rim."""
    rnd = random.Random(seed); M = Mesh()
    for k in range(heads):
        base = Vector((rnd.uniform(-.18, .18), rnd.uniform(-.18, .18), -.02))
        h = rnd.uniform(.32, .55) if kind == 'poppy' else rnd.uniform(.18, .32)
        top = base + Vector((rnd.uniform(-.07, .07), rnd.uniform(-.07, .07), h))
        st = .006 if kind == 'poppy' else .004
        sd = Vector((st, 0, 0)); sd2 = Vector((0, st, 0))
        for s_ in (sd, sd2):                                    # stem: two crossed ribbons
            i = [M.vert(base - s_, (0, 0, 0, 1), UP), M.vert(base + s_, (0, 0, 0, 1), UP), M.vert(top + s_ * .7, (1, 0, 0, 1), UP), M.vert(top - s_ * .7, (1, 0, 0, 1), UP)]
            M.face(i, [(0, 0), (1, 0), (1, 1), (0, 1)])
        bud = kind == 'poppy' and rnd.random() < .2
        face_up = (UP + Vector((rnd.uniform(-.35, .35), rnd.uniform(-.35, .35), 0))).normalized()
        tx = face_up.orthogonal().normalized(); ty = face_up.cross(tx)
        c = M.vert(top + face_up * .012, (1, .5, 0, 1), face_up)
        if bud:
            for p_ in range(3):
                a0 = 2 * math.pi * p_ / 3
                e = [M.vert(top + (tx * math.cos(a0 + o) + ty * math.sin(a0 + o)) * .018 + face_up * .005, (1, 1, .2, 1), face_up) for o in (0, 2.1)]
                tipv = M.vert(top + face_up * .05, (1, 1, .6, 1), face_up)
                M.face((e[0], e[1], tipv), [(0, 0), (1, 0), (.5, 1)])
            continue
        n_pet = 4 if kind == 'poppy' else (rnd.randint(12, 14) if lod == 0 else 7)
        R = rnd.uniform(.05, .07) if kind == 'poppy' else rnd.uniform(.03, .038)
        wid = (2 * math.pi / n_pet) * (.62 if kind == 'poppy' else .3)
        for p_ in range(n_pet):
            a0 = 2 * math.pi * p_ / n_pet + k
            dirp = tx * math.cos(a0) + ty * math.sin(a0); sidep = tx * math.cos(a0 + math.pi / 2) + ty * math.sin(a0 + math.pi / 2)
            cup = .55 if kind == 'poppy' else .12                   # poppy petals cup upward
            mid = top + dirp * R * .55 + face_up * R * cup * .4
            tip = top + dirp * R + face_up * R * cup
            pw = R * wid * (1.4 if kind == 'poppy' else 1.0)
            vs = [M.vert(top + sidep * pw * .25, (1, 1, 0, 1), face_up), M.vert(mid + sidep * pw, (1, 1, .6, 1), face_up),
                  M.vert(tip + sidep * pw * .6, (1, 1, 1, 1), face_up), M.vert(tip - sidep * pw * .6, (1, 1, 1, 1), face_up),
                  M.vert(mid - sidep * pw, (1, 1, .6, 1), face_up), M.vert(top - sidep * pw * .25, (1, 1, 0, 1), face_up)]
            M.face((vs[0], vs[1], vs[4], vs[5]), [(0, 0)] * 4); M.face((vs[1], vs[2], vs[3], vs[4]), [(0, 0)] * 4)
        if lod == 0:                                              # centre disc
            ring = [M.vert(top + (tx * math.cos(2 * math.pi * q / 6) + ty * math.sin(2 * math.pi * q / 6)) * R * (.28 if kind == 'poppy' else .35) + face_up * .01,
                           (1, .5, .5, 1), face_up) for q in range(6)]
            for q in range(6): M.face((c, ring[q], ring[(q + 1) % 6]), [(0, 0)] * 3)
    for k in range(4 if lod == 0 else 2):                          # basal leaves (green, G = 0)
        a = rnd.uniform(0, 2 * math.pi)
        blade(M, rnd, Vector((math.cos(a) * .05, math.sin(a) * .05, -.02)), a, rnd.uniform(35, 55), rnd.uniform(.12, .2), .035, 40, 2, (0, 0))
    return M.build(name, ('Flowers',))

def flower_lods(name, seed, kind):
    return flower_clump(name, seed, kind), [], [flower_clump(name + '_LOD1', seed, kind, lod=1)]

# ---------------------------------------------------------------- detail pieces
def bush(name, seed, r=0.9, lobes=5, lod_count=3):
    """round leafy bush (video: dense clumps along paths, at tree feet): dome cluster of leaf-bunch cards + dark core."""
    rnd = random.Random(seed); domes = [(Vector((0, 0, r * .6)), r * .8)]
    for k in range(lobes):
        a = 2 * math.pi * k / lobes + rnd.uniform(-.4, .4)
        domes.append((Vector((math.cos(a) * r * .6, math.sin(a) * r * .6, r * rnd.uniform(.35, .7))), r * rnd.uniform(.45, .62)))
    zlo = 0.0; zhi = max(c.z + rr for c, rr in domes); C = Vector((0, 0, r * .5))
    out = []
    for lod in range(lod_count):
        M = Mesh()
        dome_canopy(M, random.Random(seed * 7 + lod), domes, C, zlo, zhi, .55, 150, lod)
        M.m = [m - 1 for m in M.m]                              # no bark: cards -> slot 0 (Leaves), blobs -> slot 1 (Fill)
        out.append(M.build(name if lod == 0 else f'{name}_LOD{lod}', ('Leaves', 'Fill'), (True, True)))
    return out[0], [], out[1:]

def reeds(name, seed):
    """water-edge reeds: long thin blades + a few cattail heads (G = 1 on heads)."""
    rnd = random.Random(seed); M = Mesh()
    for k in range(22):
        a = rnd.uniform(0, 2 * math.pi); rr = .25 * math.sqrt(rnd.random())
        blade(M, rnd, Vector((math.cos(a) * rr, math.sin(a) * rr, -.05)), a, rnd.uniform(2, 14), rnd.uniform(.9, 1.6), .035, rnd.uniform(5, 30), 4)
    for k in range(4):
        a = rnd.uniform(0, 2 * math.pi); base = Vector((math.cos(a) * .12, math.sin(a) * .12, -.05)); h = rnd.uniform(1.3, 1.7)
        top = base + Vector((rnd.uniform(-.06, .06), rnd.uniform(-.06, .06), h))
        for s_ in (Vector((.005, 0, 0)), Vector((0, .005, 0))):
            i = [M.vert(base - s_, (0, 0, 0, 1), UP), M.vert(base + s_, (0, 0, 0, 1), UP), M.vert(top + s_, (1, 0, 0, 1), UP), M.vert(top - s_, (1, 0, 0, 1), UP)]
            M.face(i, [(0, 0)] * 4)
            hb = top - Vector((0, 0, .22))
            j = [M.vert(hb - s_ * 3.5, (1, 1, 0, 1), UP), M.vert(hb + s_ * 3.5, (1, 1, 0, 1), UP), M.vert(top - Vector((0, 0, .03)) + s_ * 3.5, (1, 1, 1, 1), UP),
                 M.vert(top - Vector((0, 0, .03)) - s_ * 3.5, (1, 1, 1, 1), UP)]
            M.face(j, [(0, 0)] * 4)
    return M.build(name, ('Grass',)), [], []

def path_stones(name, seed):
    """a few flat embedded pebbles for path edges (rock material; world-projected texture in Unreal)."""
    rnd = random.Random(seed); bm = bmesh.new()
    for k in range(7):
        r = rnd.uniform(.06, .16); c = Vector((rnd.uniform(-.5, .5), rnd.uniform(-.35, .35), 0))
        res = bmesh.ops.create_icosphere(bm, subdivisions=2, radius=r)
        for v in [v for v in res['verts'] if v.is_valid]:
            u = v.co.normalized(); n_ = 1 + .18 * noise.noise(u * 2.2 + Vector((k, seed, 0)))
            v.co = c + Vector((u.x * r * n_ * rnd.uniform(1.1, 1.4), u.y * r * n_, u.z * r * .45 * n_ - r * .12))
    uv = bm.loops.layers.uv.new('UVMap'); col = bm.verts.layers.float_color.new('Col')
    for v in bm.verts: v[col] = (1, 1, 1, 1)
    for f in bm.faces:
        f.smooth = True
        for l in f.loops: l[uv].uv = (l.vert.co.x, l.vert.co.y)
    return obj(name, bm, ('Rock',)), [], []

def fallen_leaves(name, seed):
    """scattered fallen leaf-bunch cards lying on the ground (a soft patch, tints from green to autumn gold)."""
    rnd = random.Random(seed); M = Mesh()
    gold = [lin((200, 150, 40)), lin((170, 110, 30)), lin((120, 140, 30))]
    for k in range(26):
        p = Vector((rnd.gauss(0, .45), rnd.gauss(0, .45), .01 + rnd.uniform(0, .015)))
        fc = (UP + Vector((rnd.uniform(-.15, .15), rnd.uniform(-.15, .15), 0))).normalized()
        col = gold[rnd.randrange(3)] + (1,)
        bunch_card(M, rnd, p, fc, rnd.uniform(.16, .26), col, UP, mat=0)
    for i in range(len(M.v)):                                   # flatten onto the ground
        x, y, z = M.v[i]; M.v[i] = (x, y, max(.005, min(z, .04)))
    return M.build(name, ('Leaves',), (True,)), [], []

def ground_swatch(name):
    """30 x 30 m review patch for the terrain material: vertex R = path (a curving strip), G = wet shore (one corner pond
    edge), a rock slope bank on one side (steep normals)."""
    bm = bmesh.new(); N = 60; S = 30.0
    col = bm.verts.layers.float_color.new('Col'); grid = []
    for j in range(N + 1):
        row = []
        for i in range(N + 1):
            x = -S / 2 + S * i / N; y = -S / 2 + S * j / N
            z = .08 * noise.noise(Vector((x * .3, y * .3, 0)))
            bank = smoothstep(7, 12, x); z += bank * 3.5 * smoothstep(-15, 15, y) + bank * 1.2   # rising rock bank (east)
            px = 1.6 * math.sin(y * .22); path = 1 - smoothstep(.9, 2.0, abs(x - px) + .3 * noise.noise(Vector((x, y, 1))))
            wet = 1 - smoothstep(4.0, 7.0, math.hypot(x + 15, y + 15))
            z -= wet * .6
            v = bm.verts.new((x, y, z)); v[col] = (path * (1 - bank), wet, 0, 1); row.append(v)
        grid.append(row)
    uv = bm.loops.layers.uv.new('UVMap')
    for j in range(N):
        for i in range(N):
            f = bm.faces.new((grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i])); f.smooth = True
            for l in f.loops: l[uv].uv = (l.vert.co.x / 4, l.vert.co.y / 4)
    return obj(name, bm, ('Terrain',)), [], []

# ---------------------------------------------------------------- preview renders (golden hour)
def preview_scene():
    sc = bpy.context.scene; sc.render.engine = 'CYCLES'; sc.cycles.samples = 32; sc.cycles.use_denoising = True
    sc.cycles.transparent_max_bounces = 48; sc.view_settings.view_transform = 'AgX'; sc.view_settings.look = 'AgX - Punchy'
    w = bpy.data.worlds.new('W'); sc.world = w; w.node_tree.nodes['Background'].inputs[0].default_value = (.42, .58, .85, 1)
    sun = bpy.data.objects.new('Sun', bpy.data.lights.new('Sun', 'SUN')); sc.collection.objects.link(sun)
    sun.data.energy = 4.5; sun.data.color = (1, .8, .58); sun.data.angle = math.radians(6)
    sun.rotation_euler = Vector((-.62, .55, -.42)).normalized().to_track_quat('-Z', 'Y').to_euler()
    cam = bpy.data.objects.new('Cam', bpy.data.cameras.new('Cam')); sc.collection.objects.link(cam); sc.camera = cam
    gp = bpy.data.meshes.new('PG'); gp.from_pydata([(-200, -200, 0), (200, -200, 0), (200, 200, 0), (-200, 200, 0)], [], [(0, 1, 2, 3)])
    go = bpy.data.objects.new('PreviewGround', gp); sc.collection.objects.link(go)
    gm = bpy.data.materials.new('PG'); gm.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = lin((112, 140, 32)) + (1,)
    go.data.materials.append(gm)
    return sc, cam

def preview_materials():
    """Blender look-alikes of the Unreal materials, keyed by slot name."""
    def img(p): return bpy.data.images.get(os.path.basename(p)) or bpy.data.images.load(p)
    for mn, base in (('Grass', (60, 120, 20)), ('Wheat', (220, 165, 50)), ('Flowers', (210, 40, 30)), ('Rock', (150, 130, 100)),
                     ('Terrain', (112, 140, 32)), ('Bark', (110, 55, 32))):
        m = bpy.data.materials.get(mn) or bpy.data.materials.new(mn)
        m.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = lin(base) + (1,)
    for mn in ('Grass', 'Wheat', 'Flowers'):                      # height gradient root -> tip
        m = bpy.data.materials[mn]; nd = m.node_tree.nodes; ln = m.node_tree.links
        va = nd.new('ShaderNodeVertexColor'); va.layer_name = 'Col'; sep = nd.new('ShaderNodeSeparateColor'); ln.new(va.outputs[0], sep.inputs[0])
        cr = nd.new('ShaderNodeValToRGB')
        lo, hi = {'Grass': ((40, 92, 20), (175, 200, 30)), 'Wheat': ((140, 100, 30), (235, 185, 60)), 'Flowers': ((40, 90, 20), (60, 120, 25))}[mn]
        cr.color_ramp.elements[0].color = lin(lo) + (1,); cr.color_ramp.elements[1].color = lin(hi) + (1,)
        ln.new(sep.outputs[0], cr.inputs[0])
        if mn == 'Flowers':
            mx = nd.new('ShaderNodeMix'); mx.data_type = 'RGBA'; ln.new(sep.outputs[1], mx.inputs[0]); ln.new(cr.outputs[0], mx.inputs[6])
            mx.inputs[7].default_value = lin((215, 35, 25)) + (1,); ln.new(mx.outputs[2], nd['Principled BSDF'].inputs['Base Color'])
        else:
            ln.new(cr.outputs[0], nd['Principled BSDF'].inputs['Base Color'])
    lm = bpy.data.materials.get('Leaves') or bpy.data.materials.new('Leaves'); nd = lm.node_tree.nodes; ln = lm.node_tree.links
    ti = nd.new('ShaderNodeTexImage'); ti.image = img(TREES2 + '/Leaf_Cluster_Atlas_BaseColor.png')
    va = nd.new('ShaderNodeVertexColor'); va.layer_name = 'Col'
    mul = nd.new('ShaderNodeMix'); mul.data_type = 'RGBA'; mul.blend_type = 'MULTIPLY'; mul.inputs[0].default_value = 1
    ln.new(ti.outputs['Color'], mul.inputs[6]); ln.new(va.outputs['Color'], mul.inputs[7]); ln.new(mul.outputs[2], nd['Principled BSDF'].inputs['Base Color'])
    tp = nd.new('ShaderNodeBsdfTransparent'); m2 = nd.new('ShaderNodeMixShader')
    ln.new(ti.outputs['Alpha'], m2.inputs[0]); ln.new(tp.outputs[0], m2.inputs[1]); ln.new(nd['Principled BSDF'].outputs[0], m2.inputs[2])
    ln.new(m2.outputs[0], nd['Material Output'].inputs['Surface'])
    fm = bpy.data.materials.get('Fill') or bpy.data.materials.new('Fill'); nd = fm.node_tree.nodes; ln = fm.node_tree.links
    va = nd.new('ShaderNodeVertexColor'); va.layer_name = 'Col'; sep = nd.new('ShaderNodeSeparateColor'); ln.new(va.outputs[0], sep.inputs[0])
    cr = nd.new('ShaderNodeValToRGB'); cr.color_ramp.elements[0].color = lin((26, 58, 36)) + (1,); cr.color_ramp.elements[1].color = lin((160, 196, 28)) + (1,)
    ln.new(sep.outputs[1], cr.inputs[0]); ln.new(cr.outputs[0], nd['Principled BSDF'].inputs['Base Color'])

def preview(sc, cam, obs, name, dist, height, az=30, el=8, lens=40):
    for o in bpy.context.scene.objects:
        if o.type == 'MESH' and o.name != 'PreviewGround': o.hide_render = o not in obs
    sc.render.resolution_x, sc.render.resolution_y = 800, 450
    cam.data.lens = lens; t = Vector((0, 0, height))
    d = Vector((math.sin(math.radians(az)) * math.cos(math.radians(el)), -math.cos(math.radians(az)) * math.cos(math.radians(el)), math.sin(math.radians(el))))
    cam.location = t + d * dist; cam.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()
    sc.render.filepath = f'{PRE}/{name}.png'; bpy.ops.render.render(write_still=True)

# ---------------------------------------------------------------- build
bpy.ops.wm.read_factory_settings(use_empty=True)
sc, cam = preview_scene()
JOBS = [
    ('trees', 'SM_Vista_Tree_Round', lambda: round_tree('SM_Vista_Tree_Round', 11, 5.5, 4.4, 6, leaf_size=.72, density=120), (16, 6)),
    ('trees', 'SM_Vista_Tree_Tall', lambda: round_tree('SM_Vista_Tree_Tall', 12, 7.5, 3.6, 5, lean=.3, leaf_size=.72, density=120), (17, 6)),
    ('trees', 'SM_Vista_Tree_Wide', lambda: round_tree('SM_Vista_Tree_Wide', 13, 4.6, 5.4, 7, lean=.6, leaf_size=.78, density=100), (17, 5.5)),
    ('grass', 'SM_Vista_Grass', lambda: grass_lods('SM_Vista_Grass', 21, 52, .28, .55, .048, .42, segs=3), (2.2, .25)),
    ('grass', 'SM_Vista_GrassTall', lambda: grass_lods('SM_Vista_GrassTall', 22, 40, .55, .95, .055, .32, segs=3, lean=(4, 22), bend=(20, 70)), (2.6, .45)),
    ('grass', 'SM_Vista_Tuft', lambda: grass_lods('SM_Vista_Tuft', 27, 40, .35, .7, .06, .18, segs=3, lean=(10, 35), bend=(25, 70)), (2.0, .3)),
    ('wheat', 'SM_Vista_Wheat', lambda: (wheat('SM_Vista_Wheat', 23), [], [wheat('SM_Vista_Wheat_LOD1', 23, 12, 1)]), (2.6, .6)),
    ('flowers', 'SM_Vista_Poppies', lambda: flower_lods('SM_Vista_Poppies', 24, 'poppy'), (1.4, .3)),
    ('flowers', 'SM_Vista_Daisies', lambda: flower_lods('SM_Vista_Daisies', 25, 'daisy'), (1.1, .2)),
    ('details', 'SM_Vista_Bush_A', lambda: bush('SM_Vista_Bush_A', 41, .9, 5), (4.5, .7)),
    ('details', 'SM_Vista_Bush_B', lambda: bush('SM_Vista_Bush_B', 42, .6, 4), (3.2, .5)),
    ('details', 'SM_Vista_Reeds', lambda: reeds('SM_Vista_Reeds', 43), (3.2, .8)),
    ('details', 'SM_Vista_PathStones', lambda: path_stones('SM_Vista_PathStones', 44), (2.0, .05)),
    ('details', 'SM_Vista_FallenLeaves', lambda: fallen_leaves('SM_Vista_FallenLeaves', 45), (2.2, 0.0)),
    ('ground', 'SM_Review_GroundSwatch', lambda: ground_swatch('SM_Review_GroundSwatch'), None),
]
preview_materials()
for group, name, fn, view in JOBS:
    if not want(group, name): continue
    ob, extra, lods = fn()
    export(ob, name, extra, lods)
    if view:
        for o in [ob] + lods: o.location = (0, 0, 0)
        preview(sc, cam, [ob], name, *view)
        if lods: preview(sc, cam, [lods[-1]], name + f'_LOD{len(lods)}', *view)
    for o in [ob] + list(extra) + list(lods): o.hide_render = True; o.hide_set(True)
print('VISTA_ASSETS_EXPORTED')
