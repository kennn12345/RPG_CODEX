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

LOD_KEEP, LOD_GROW = (1.0, .5, .22), (1.0, 1.3, 1.75)      # canopy cards kept / enlarged per LOD (subsets of LOD0)
def dome_canopy(M, rnd, domes, C, zlo, zhi, leaf, density, lod):
    """leaf-bunch cards over every dome + dark cores. R02b: every LOD uses the SAME cards (positions, angles, atlas cells
    from per-card seeds) - a lower LOD only keeps a subset (per-card key) and grows them a little, so the canopy thins
    instead of rearranging when the LOD switches (the R02 LODs re-rolled every card -> leaves jumped on approach)."""
    keep, grow = LOD_KEEP[lod], LOD_GROW[lod]
    for di, (c, r) in enumerate(domes):
        tint = .5 + rnd.uniform(-.08, .08)
        if lod < 2 or r > .8:
            blob(M, c, r * .6, lambda p, u: .10 + .14 * max(0, u.z), tint, 2 if r > 1.0 else 1, .75, di * 2.3)
        for k in range(int(density * r * r)):
            u = Vector((rnd.gauss(0, 1), rnd.gauss(0, 1), rnd.gauss(0, 1))).normalized()
            skip_under = u.z < -.5 and rnd.random() < .6
            f = rnd.uniform(.84, 1.04); p = c + u * r * f
            key, card_seed = rnd.random(), rnd.randrange(1 << 30)
            tj, fj = rnd.gauss(0, .07), (rnd.uniform(-.4, .4), rnd.uniform(-.4, .4), rnd.uniform(-.1, .5))
            sz = rnd.uniform(.85, 1.15)
            if skip_under or key > keep: continue
            if any((p - c2).length < r2 * .78 for c2, r2 in domes if c2 is not c): continue
            up = smoothstep(zlo, zhi, p.z)
            t = .5 + .42 * u.z + .25 * (up - .5) + 1.0 * (f - .94) + (tint - .5) + tj
            facing = (u + Vector(fj)).normalized()
            shade_n = ((p - C).normalized() * .5 + u * .4 + UP * .15).normalized()
            bunch_card(M, random.Random(card_seed), p, facing, leaf * grow * sz, ramp(t), shade_n)

def sweep(bm, col, pts, radii, sides, flare_rings=0):
    """ONE continuous tube through pts (Catmull-Rom, parallel-transported frames - no twist, no seams), closed at the top,
    root-flare lumps on the first flare_rings rings. Material 0 (bark)."""
    dense = []
    P = [pts[0] * 2 - pts[1]] + list(pts) + [pts[-1] * 2 - pts[-2]]; R = [radii[0]] + list(radii) + [radii[-1]]
    for i in range(1, len(P) - 2):
        for k in range(4):
            t = k / 4; t2, t3 = t * t, t * t * t
            p = .5 * ((2 * P[i]) + (-P[i - 1] + P[i + 1]) * t + (2 * P[i - 1] - 5 * P[i] + 4 * P[i + 1] - P[i + 2]) * t2 + (-P[i - 1] + 3 * P[i] - 3 * P[i + 1] + P[i + 2]) * t3)
            dense.append((p, R[i] + (R[i + 1] - R[i]) * t, i - 1 < flare_rings))
    dense.append((pts[-1], radii[-1], False))
    T = [(dense[min(i + 1, len(dense) - 1)][0] - dense[max(i - 1, 0)][0]).normalized() for i in range(len(dense))]
    n = T[0].orthogonal().normalized(); rings = []
    for i, (p, r, fl) in enumerate(dense):
        if i: n = T[i - 1].rotation_difference(T[i]) @ n; n = (n - T[i] * n.dot(T[i])).normalized()
        b = T[i].cross(n); ring = []
        for j in range(sides):
            a = 2 * math.pi * j / sides
            k = 1 + (.22 * noise.noise(Vector((math.cos(a) * 1.7, math.sin(a) * 1.7, i * .3))) + .12 * math.cos(a * 5) if fl else 0)
            v = bm.verts.new(p + (n * math.cos(a) + b * math.sin(a)) * r * k); v[col] = (0, 1, .5, 1); ring.append(v)
        rings.append(ring)
    for i in range(len(rings) - 1):
        for j in range(sides):
            f = bm.faces.new((rings[i][j], rings[i][(j + 1) % sides], rings[i + 1][(j + 1) % sides], rings[i + 1][j])); f.material_index = 0
    tip = bm.verts.new(dense[-1][0] + T[-1] * radii[-1] * .5); tip[col] = (0, 1, .5, 1)
    for j in range(sides): bm.faces.new((rings[-1][j], rings[-1][(j + 1) % sides], tip)).material_index = 0

# ---------------------------------------------------------------- round world trees (R01 shape replayed)
def round_tree(name, seed, trunk_h, canopy_r, clumps, lean=0.4, leaf_size=.5, density=210):
    """R01 fluffy_tree up to the canopy, verbatim (same rnd draws): trunk, lean, domes, branches. R02 canopy + LODs."""
    rnd = random.Random(seed); R1['rnd'] = rnd
    bm = bmesh.new(); col = bm.verts.layers.float_color.new('Col'); bm.loops.layers.uv.new('UVMap')
    for k in R1['NKEYS']: bm.verts.layers.float.new(k)
    top = Vector((rnd.uniform(-lean, lean), rnd.uniform(-lean, lean), trunk_h))
    mid = Vector((top.x * .35 + rnd.uniform(-.2, .2), top.y * .35 + rnd.uniform(-.2, .2), trunk_h * .5))
    sweep(bm, col, [Vector((0, 0, -.4)), Vector((0, 0, .05)), mid * .5, mid, mid.lerp(top, .5), top, top + (top - mid).normalized() * .4],
          [.42 * trunk_h / 6 * 1.55, .42 * trunk_h / 6 * 1.2, .36 * trunk_h / 6, .3 * trunk_h / 6, .25 * trunk_h / 6, .2 * trunk_h / 6, .12 * trunk_h / 6], 16, flare_rings=2)
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
        tube(bm, col, top.lerp(mid, rnd.uniform(0, .4)), c, .14 * trunk_h / 6, .06, 9)
    zlo = min(c.z - r for c, r in domes); zhi = max(c.z + r for c, r in domes)
    # bark -> Mesh (faceted), with R01 bark UVs (around x height); moss weight 0 (material adds the ground collar)
    bark = Mesh()
    bm.verts.ensure_lookup_table(); vid = {}
    for v in bm.verts: vid[v] = bark.vert(v.co, (0, 1, .5, 1))
    for f in bm.faces:
        us = [(math.atan2(v.co.y, v.co.x) / math.pi + 1) for v in f.verts]
        if max(us) - min(us) > 1.0: us = [u + 2.0 if u < 1.0 else u for u in us]    # no texture smear across the wrap seam
        bark.face([vid[v] for v in f.verts], [(u, v.co.z / 2.0) for u, v in zip(us, f.verts)], 0)
    bm.free()
    hull = trunk_hull(name, Vector((0, 0, -.3)), mid.lerp(top, .5), .42 * trunk_h / 6)
    lods = []
    for lod in range(3):
        M = Mesh(); M.v, M.c, M.n, M.f, M.uv, M.m = list(bark.v), list(bark.c), list(bark.n), list(bark.f), list(bark.uv), list(bark.m)
        dome_canopy(M, random.Random(seed * 31), domes, C, zlo, zhi, leaf_size * 1.25, density * .8, lod)
        ob = M.build(name if lod == 0 else f'{name}_LOD{lod}', ('Bark', 'Leaves', 'Fill'), (True, True, True))   # smooth round bark
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
              rnd.uniform(hmin, hmax) * (1.1 - .3 * rr / spread), wid * rnd.uniform(.8, 1.2), rnd.uniform(*bend), segs, (0, rnd.random() ** 2))   # B = per-blade hue
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
        base = Vector((math.cos(a) * rr, math.sin(a) * rr, -.03)); h = rnd.uniform(.6, .82)          # ~0.7-1.0 m in the world (scale 0.9-1.3)
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
        h = rnd.uniform(.32, .55) if kind == 'poppy' else rnd.uniform(.24, .4)
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
        R = rnd.uniform(.05, .07) if kind == 'poppy' else rnd.uniform(.042, .055)
        wid = (2 * math.pi / n_pet) * (.62 if kind == 'poppy' else .3)
        for p_ in range(n_pet):
            a0 = 2 * math.pi * p_ / n_pet + k
            dirp = tx * math.cos(a0) + ty * math.sin(a0); sidep = tx * math.cos(a0 + math.pi / 2) + ty * math.sin(a0 + math.pi / 2)
            cup = 1.0 if kind == 'poppy' else .12                   # poppy petals cup upward (open bowl)
            mid = top + dirp * R * .55 + face_up * R * cup * .4
            tip = top + dirp * R + face_up * R * cup
            pw = R * wid * (1.4 if kind == 'poppy' else 1.0)
            vs = [M.vert(top + sidep * pw * .25, (1, 1, 0, 1), face_up), M.vert(mid + sidep * pw, (1, 1, .6, 1), face_up),
                  M.vert(tip + sidep * pw * .6, (1, 1, 1, 1), face_up), M.vert(tip - sidep * pw * .6, (1, 1, 1, 1), face_up),
                  M.vert(mid - sidep * pw, (1, 1, .6, 1), face_up), M.vert(top - sidep * pw * .25, (1, 1, 0, 1), face_up)]
            M.face((vs[0], vs[1], vs[4], vs[5]), [(.35, 0), (0, .5), (1, .5), (.65, 0)]); M.face((vs[1], vs[2], vs[3], vs[4]), [(0, .5), (.2, 1), (.8, 1), (1, .5)])
        if lod == 0:                                              # centre disc
            ring = [M.vert(top + (tx * math.cos(2 * math.pi * q / 6) + ty * math.sin(2 * math.pi * q / 6)) * R * (.28 if kind == 'poppy' else .35) + face_up * .01,
                           (1, .5, .5, 1), face_up) for q in range(6)]
            for q in range(6): M.face((c, ring[q], ring[(q + 1) % 6]), [(0, 0)] * 3)
    for k in range(4 if lod == 0 else 2):                          # basal leaves (green, G = 0)
        a = rnd.uniform(0, 2 * math.pi)
        blade(M, rnd, Vector((math.cos(a) * .05, math.sin(a) * .05, -.02)), a, rnd.uniform(35, 55), rnd.uniform(.12, .2), .035, 40, 2, (0, 0))
    return M.build(name, ('Daisy',) if kind == 'daisy' else ('Flowers',))

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
        dome_canopy(M, random.Random(seed * 7), domes, C, zlo, zhi, .55, 150, lod)
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

# ---------------------------------------------------------------- cliffs + ivy (R01 slabs replayed)
def edge_light(me):
    """vertex colour for the stone materials: R = edge light (convex brighter, creases darker), G = height, B = random."""
    zs = [v.co.z for v in me.vertices]; z0, z1 = min(zs), max(zs)
    conv = [0.0] * len(me.vertices); cnt = [0] * len(me.vertices)
    for e in me.edges:
        a, b = e.vertices; d = me.vertices[b].co - me.vertices[a].co
        if d.length < 1e-6: continue
        c = (me.vertices[b].normal - me.vertices[a].normal).dot(d.normalized())
        conv[a] += c; conv[b] += c; cnt[a] += 1; cnt[b] += 1
    col = me.color_attributes.get('Col') or me.color_attributes.new('Col', 'FLOAT_COLOR', 'POINT')
    for i, v in enumerate(me.vertices):
        col.data[i].color = (max(0, min(1, .5 + conv[i] / max(1, cnt[i]) * 1.2)), (v.co.z - z0) / max(1e-3, z1 - z0), (i * 0.6180339) % 1.0, 1)

def cliff_r02(name, seed, layers=4):
    """R01 stacked-ledge cliff (same slabs: R01 code run with its ivy leaves only recorded), restyled: soft shading split
    at 40 deg, edge-light vertex colour, ivy = thick leaf-bunch mats spilling over each ledge top + denser curtains."""
    rec = []
    def record(bm, col, p, n, size, light, tint, normal):
        rnd_ = R1['rnd']; rnd_.gauss(0, 1); rnd_.gauss(0, 1); rnd_.gauss(0, 1); rnd_.uniform(14, 30); rnd_.randrange(4)
        rec.append((p.copy(), n.copy(), size, light, normal.copy())); return []
    R1['leaf'] = record
    rock = R1['cliff'](name + '_rock', seed, layers)
    me = rock.data                                                       # R01 flat slab normals kept (big planes, like the reference)
    edge_light(me)
    rnd = random.Random(seed * 13); M = Mesh()
    # curtains: the R01 strand positions, as leaf bunches (fewer, bigger, overlapping)
    for (p, n, size, light, normal) in rec:
        bunch_card(M, rnd, p, n, size * 2.1, ramp(.25 + light * .75 + rnd.gauss(0, .06)), (normal * .6 + UP * .3).normalized(), mat=0)
    # mats on ledge tops: cards lying on up-facing rock faces near their outer edge, spilling over it
    tops = [p for p in me.polygons if p.normal.z > .9 and p.area > 1.0]
    for f in tops:
        cen = f.center; verts = [me.vertices[i].co for i in f.vertices]
        for k in range(int(f.area * 6.0)):
            a, b = rnd.sample(range(len(verts)), 2); t = rnd.random()
            e = verts[a].lerp(verts[b], t)                                      # a point on the rim polygon
            q = e.lerp(cen, rnd.uniform(0, .5) ** 1.5) + UP * .04
            out = (e - cen); out.z = 0
            if out.length < 1e-3 or rnd.random() < .25: continue
            out.normalize()
            fc = (UP * .8 + out * .45 + Vector((rnd.uniform(-.2, .2), rnd.uniform(-.2, .2), 0))).normalized()
            bunch_card(M, rnd, q + out * .15, fc, rnd.uniform(.9, 1.35), ramp(.62 + rnd.gauss(0, .08)), (UP * .7 + out * .3).normalized(), mat=0)
            if rnd.random() < .55:                                              # a short spill over the edge
                bunch_card(M, rnd, e + out * .25 - UP * .25, (out + UP * .2).normalized(), rnd.uniform(.5, .75), ramp(.45 + rnd.gauss(0, .08)), out, mat=0)
    ivy = M.build(name + '_ivy', ('Leaves',), (True,))
    # join: slot 0 Rock (cliff), slot 1 Leaves
    rock.data.materials.clear(); rock.data.materials.append(bpy.data.materials.get('Rock')); rock.data.materials.append(bpy.data.materials.get('Leaves'))
    ivy.data.materials.clear(); ivy.data.materials.append(bpy.data.materials.get('Rock')); ivy.data.materials.append(bpy.data.materials.get('Leaves'))
    for p in ivy.data.polygons: p.material_index = 1
    bpy.ops.object.select_all(action='DESELECT'); rock.select_set(True); ivy.select_set(True)
    bpy.context.view_layer.objects.active = rock; bpy.ops.object.join(); rock.name = name; rock.data.name = name
    return rock, [], []

# ---------------------------------------------------------------- hero tree (step 2, user screenshots User_01..07)
def hero_tree(name, seed, H=12.0, n_branch=10, clump_r=(1.5, 2.2), leaf=1.0, density=34.0):
    """Video-style tree (reference/USER_REFS.md): tall slim red-brown trunk visible for ~45% of the height, a few long
    branches forking upward at 30-50 deg, and an OPEN canopy of separate flattened-dome clumps (dappled small-leaf cards,
    slot 'Dapple') at the branch ends with sky gaps between them. Each clump is shaded like a soft sphere (vertex colour +
    bent normals from the clump centre). LOD1/2 = subsets of the same cards (no popping). Returns (LOD0, [UCX], [LOD1, LOD2])."""
    rnd = random.Random(seed)
    bm = bmesh.new(); col = bm.verts.layers.float_color.new('Col'); bm.loops.layers.uv.new('UVMap')
    lean = Vector((rnd.uniform(-.25, .25), rnd.uniform(-.25, .25), 0))
    tp = [Vector((0, 0, -.35)), Vector((0, 0, .35))]
    for f in (.3, .55, .78, .95):
        tp.append(Vector((0, 0, H * f)) + lean * f * f * H * .06 + Vector((rnd.uniform(-.12, .12), rnd.uniform(-.12, .12), 0)) * f)
    r0 = .034 * H                                                           # ~41 cm at 12 m
    sweep(bm, col, tp, [r0 * 1.5, r0 * 1.12, r0 * .9, r0 * .72, r0 * .52, r0 * .3], 14, flare_rings=2)
    def trunk_at(z):
        for a, b in zip(tp, tp[1:]):
            if a.z <= z <= b.z: return a.lerp(b, (z - a.z) / max(b.z - a.z, 1e-3))
        return tp[-1]
    clumps = []                                                             # (centre, radius)
    gold = 2.39996
    az0 = rnd.uniform(0, 6.28)
    for k in range(n_branch):
        t = k / max(1, n_branch - 1)
        z = H * (.3 + .42 * t + rnd.uniform(-.03, .03))
        az = az0 + k * gold + rnd.uniform(-.3, .3)
        up = math.radians(rnd.uniform(48, 66) - 16 * t)                      # angle from vertical: wide spread low, steeper up top
        L = H * rnd.uniform(.3, .4) * (1.15 - .4 * t)
        d = Vector((math.cos(az) * math.sin(up), math.sin(az) * math.sin(up), math.cos(up)))
        p0 = trunk_at(z); p1 = p0 + d * L * .5 + Vector((0, 0, L * .05)); p2 = p0 + d * L + Vector((0, 0, L * .12))
        rb = r0 * rnd.uniform(.42, .55) * (1.05 - .4 * t)
        sweep(bm, col, [p0 - d * .1, p0, p1, p2], [rb, rb, rb * .62, rb * .3], 8)
        cr = rnd.uniform(*clump_r) * (1.05 - .25 * t)
        clumps.append((p2 + Vector((0, 0, cr * .25)), cr))
        cm = cr * rnd.uniform(.55, .75); clumps.append((p1.lerp(p2, .35) + Vector((0, 0, cm * .45)) + Vector((-d.y, d.x, 0)) * rnd.uniform(-.5, .5), cm))   # mid-branch clump
        if rnd.random() < .9:                                               # a fork: side clump
            q0 = p0.lerp(p1, rnd.uniform(.9, 1.3)); sd = (d + Vector((math.cos(az + 1.6), math.sin(az + 1.6), .6)) * rnd.choice((-1, 1)) * .8).normalized()
            if sd.z < .3: sd.z = .3; sd.normalize()
            q1 = q0 + sd * L * rnd.uniform(.35, .5)
            sweep(bm, col, [q0 - sd * .05, q0, q1], [rb * .55, rb * .5, rb * .2], 6)
            cr2 = cr * rnd.uniform(.7, .85); clumps.append((q1 + Vector((0, 0, cr2 * .2)), cr2))
    top = tp[-1]                                                            # crown: 2-3 clumps around the top
    for k in range(rnd.randint(4, 6)):
        a = rnd.uniform(0, 6.28); o = Vector((math.cos(a), math.sin(a), 0)) * rnd.uniform(.6, 1.9)
        clumps.append((top + o + Vector((0, 0, rnd.uniform(.3, .9))), rnd.uniform(*clump_r) * .9))
    zlo = min(c.z - r for c, r in clumps); zhi = max(c.z + r for c, r in clumps)
    # bark -> Mesh (smooth), wrapped UVs
    bark = Mesh(); bm.verts.ensure_lookup_table(); vid = {v: bark.vert(v.co, (0, 1, .5, 1)) for v in bm.verts}
    for f in bm.faces:
        us = [(math.atan2(v.co.y, v.co.x) / math.pi + 1) for v in f.verts]
        if max(us) - min(us) > 1.0: us = [u + 2.0 if u < 1.0 else u for u in us]
        bark.face([vid[v] for v in f.verts], [(u, v.co.z / 2.5) for u, v in zip(us, f.verts)], 0)
    bm.free()
    # one card list for all LODs (per-card seeds)
    cards = []; crnd = random.Random(seed * 101)
    for ci, (c, r) in enumerate(clumps):
        tint = crnd.uniform(-.07, .07)
        for k in range(int(density * r * r)):
            u = Vector((crnd.gauss(0, 1), crnd.gauss(0, 1), crnd.gauss(0, 1) * .8 + .35)).normalized()   # more on top
            p = c + Vector((u.x, u.y, u.z * (.75 if u.z > 0 else .5))) * r * (crnd.uniform(.55, 1.0) if crnd.random() < .7 else crnd.uniform(1.0, 1.3))   # soft dome + stray leaves past the rim
            key, cs = crnd.random(), crnd.randrange(1 << 30)
            h = smoothstep(zlo, zhi, p.z)
            t = .52 + .45 * u.z + .12 * (h - .5) + tint + crnd.gauss(0, .07)                              # soft sphere per clump
            facing = (u * .8 + UP * .35 + Vector((crnd.uniform(-.3, .3), crnd.uniform(-.3, .3), 0))).normalized()
            shade_n = (u * .75 + UP * .25).normalized()
            cards.append((key, cs, p, facing, leaf * crnd.uniform(.85, 1.15), t, shade_n))
    lods = []
    for lod in range(3):
        M = Mesh(); M.v, M.c, M.n, M.f, M.uv, M.m = list(bark.v), list(bark.c), list(bark.n), list(bark.f), list(bark.uv), list(bark.m)
        for ci, (c, r) in enumerate(clumps):                               # dark core: closes the see-through INSIDE a clump only
            blob(M, c - Vector((0, 0, r * .12)), r * (.36 if lod < 2 else .26), lambda p, u: .14 + .14 * max(0, u.z), .5, 2 if lod < 2 else 1, .6, ci * 1.7)
        for key, cs, p, facing, sz, t, sn in cards:
            if key > LOD_KEEP[lod]: continue
            bunch_card(M, random.Random(cs), p, facing, sz * LOD_GROW[lod], ramp(t), sn)
        ob = M.build(name if lod == 0 else f'{name}_LOD{lod}', ('Bark', 'Dapple', 'Fill'), (True, True, True))
        lods.append(ob)
    hull = trunk_hull(name, Vector((0, 0, -.3)), trunk_at(H * .45), r0 * 1.1)
    return lods[0], [hull], lods[1:]

# ---------------------------------------------------------------- R02b detail pieces (moss, ferns, wildflowers, mushrooms, wood)
def moss_patch(name, seed, r=0.7, lod=0):
    """soft moss cushion: low lumpy mound (slot 'Moss', vertex G = light: top lit, edges darker) + a fringe of tiny
    leaf bunches at the rim so it does not end in a hard edge."""
    rnd = random.Random(seed); M = Mesh()
    vs, fs = icosphere(3 if lod == 0 else 2); base = len(M.v)
    for u in vs:
        u = u.normalized(); k = 1 + .2 * noise.noise(u * 2.5 + Vector((seed, 0, 0))) + .07 * noise.noise(u * 7 + Vector((0, seed, 0)))
        p = Vector((u.x * r * k * 1.15, u.y * r * k, max(-.04, u.z) * r * .5 * k))
        M.vert(p, (0, max(0, min(1, .35 + .65 * u.z)), rnd.random(), 1), (UP * .7 + u * .3).normalized())
    for f in fs: M.face([base + i for i in f], [(M.v[base + i][0] / .6, M.v[base + i][1] / .6) for i in f], 0)
    if lod == 0:
        for k in range(34):
            a = rnd.uniform(0, 2 * math.pi); p = Vector((math.cos(a) * r * 1.05, math.sin(a) * r * .92, .03))
            fc = (UP * .7 + Vector((math.cos(a), math.sin(a), 0)) * .5).normalized()
            bunch_card(M, rnd, p, fc, rnd.uniform(.18, .28), ramp(.45 + rnd.gauss(0, .08)), fc, mat=1)
    return M.build(name if lod == 0 else f'{name}_LOD{lod}', ('Moss', 'Leaves'), (True, True))

def fern(name, seed, fronds=14, lod=0):
    """fern: arching fronds (curved spine) with paired leaflets tapering to the tip (slot 'Grass': vertex R = height
    for wind/colour, B = per-frond hue)."""
    rnd = random.Random(seed); M = Mesh()
    for k in range(fronds):
        a = 2 * math.pi * k / fronds + rnd.uniform(-.25, .25); L = rnd.uniform(.6, .9); arch = rnd.uniform(28, 45)
        d = Vector((math.cos(a), math.sin(a), 0)); side = Vector((-d.y, d.x, 0)); hue = rnd.random()
        n_seg = 16 if lod == 0 else 7; prev = None
        for s in range(n_seg + 1):
            t = s / n_seg; ang = math.radians(8 + arch * t * t * 2.0)
            p = d * (math.sin(ang) * L * t) + UP * (math.cos(ang) * L * t * .9)
            if s == 0: prev = p; continue
            wl = .28 * L * math.sin(min(1, t * 1.05) * math.pi) * (1.1 - t * .45)        # leaflet length
            if wl > .01:
                tng = (p - prev).normalized(); nrm = tng.cross(side).normalized()
                if nrm.z < 0: nrm = -nrm
                for sg in (-1, 1):
                    tip = p + side * sg * wl + tng * wl * .45 - UP * wl * .12
                    mid = prev.lerp(tip, .55) + tng * wl * .15
                    i = [M.vert(prev, (t, 0, hue, 1), nrm), M.vert(mid, (t, 0, hue, 1), nrm), M.vert(tip, (t, 0, hue, 1), nrm), M.vert(p, (t, 0, hue, 1), nrm)]
                    M.face(i, [(.5, 0), (sg * .3 + .5, .4), (sg * .5 + .5, 1), (.5, 1)], 0)   # wider leaflet (quad)
            # spine
            i = [M.vert(prev - side * .006, (t, 0, hue, 1), UP), M.vert(prev + side * .006, (t, 0, hue, 1), UP), M.vert(p + side * .004, (t, 0, hue, 1), UP), M.vert(p - side * .004, (t, 0, hue, 1), UP)]
            M.face(i, [(.48, 0), (.52, 0), (.52, 1), (.48, 1)], 0)
            prev = p
    return M.build(name if lod == 0 else f'{name}_LOD{lod}', ('Grass',))

def wildflowers(name, seed, kind, heads=12, lod=0):
    """small wildflower clump: 'bell' = nodding purple bellflowers (video t075 purple clumps), 'blue' = tiny 5-petal blue
    stars (t021). Same vertex convention as the poppies (G kind, B petal shade, UV radial on petals)."""
    rnd = random.Random(seed); M = Mesh()
    for k in range(heads if lod == 0 else heads // 2):
        base = Vector((rnd.gauss(0, .1), rnd.gauss(0, .1), -.02)); h = rnd.uniform(.14, .3)
        top = base + Vector((rnd.uniform(-.05, .05), rnd.uniform(-.05, .05), h))
        s_ = Vector((.003, 0, 0))
        i = [M.vert(base - s_, (0, 0, 0, 1), UP), M.vert(base + s_, (0, 0, 0, 1), UP), M.vert(top + s_, (1, 0, 0, 1), UP), M.vert(top - s_, (1, 0, 0, 1), UP)]
        M.face(i, [(0, 0)] * 4)
        if kind == 'bell':
            ax = (Vector((rnd.uniform(-1, 1), rnd.uniform(-1, 1), -1.2))).normalized()      # nodding
            tx = ax.orthogonal().normalized(); ty = ax.cross(tx); R = rnd.uniform(.032, .044)
            # rounded bell: 6-sided cup (narrow waist ring, flared rim) - reads as a soft blossom, not a spike
            waist = [M.vert(top + ax * R * .7 + (tx * math.cos(math.pi * q / 3) + ty * math.sin(math.pi * q / 3)) * R * .75, (1, 1, .5, 1), -ax) for q in range(6)]
            ring = [M.vert(top + ax * R * 1.25 + (tx * math.cos(math.pi * q / 3 + .5) + ty * math.sin(math.pi * q / 3 + .5)) * R * 1.05, (1, 1, 1, 1), -ax) for q in range(6)]
            c = M.vert(top, (1, 1, 0, 1), -ax)
            for q in range(6):
                q2 = (q + 1) % 6
                M.face((c, waist[q], waist[q2]), [(.5, 0), (q / 6, .5), ((q + 1) / 6, .5)])
                M.face((waist[q], ring[q], ring[q2], waist[q2]), [(q / 6, .5), (q / 6, 1), ((q + 1) / 6, 1), ((q + 1) / 6, .5)])
        else:
            fu = (UP + Vector((rnd.uniform(-.3, .3), rnd.uniform(-.3, .3), 0))).normalized(); tx = fu.orthogonal().normalized(); ty = fu.cross(tx)
            R = rnd.uniform(.032, .042); c = M.vert(top + fu * .004, (1, .5, .5, 1), fu)
            for q in range(5):
                a0 = 2 * math.pi * q / 5
                e0 = M.vert(top + (tx * math.cos(a0 - .3) + ty * math.sin(a0 - .3)) * R * .4, (1, 1, .2, 1), fu)
                tp = M.vert(top + (tx * math.cos(a0) + ty * math.sin(a0)) * R, (1, 1, 1, 1), fu)
                e1 = M.vert(top + (tx * math.cos(a0 + .3) + ty * math.sin(a0 + .3)) * R * .4, (1, 1, .2, 1), fu)
                M.face((c, e0, tp, e1), [(.5, 0), (.3, .5), (.5, 1), (.7, .5)])
    for k in range(5 if lod == 0 else 2):
        a = rnd.uniform(0, 2 * math.pi)
        blade(M, rnd, Vector((math.cos(a) * .05, math.sin(a) * .05, -.02)), a, rnd.uniform(30, 55), rnd.uniform(.1, .18), .03, 40, 2, (0, 0))
    return M.build(name if lod == 0 else f'{name}_LOD{lod}', ('Bellflower',) if kind == 'bell' else ('BlueFlower',))

def clover(name, seed, lod=0):
    """low leafy ground cover (clover-like round leaflets) - fills the ground between grass clumps and at path edges."""
    rnd = random.Random(seed); M = Mesh()
    for k in range(44 if lod == 0 else 18):
        a = rnd.uniform(0, 2 * math.pi); rr = .35 * math.sqrt(rnd.random())
        p = Vector((math.cos(a) * rr, math.sin(a) * rr, rnd.uniform(.04, .12)))
        fc = (UP + Vector((rnd.uniform(-.4, .4), rnd.uniform(-.4, .4), 0))).normalized()
        bunch_card(M, rnd, p, fc, rnd.uniform(.14, .22), ramp(.42 + .25 * p.z / .12 + rnd.gauss(0, .06)), (UP * .8 + fc * .2).normalized(), mat=0)
    return M.build(name if lod == 0 else f'{name}_LOD{lod}', ('Leaves',), (True,))

def mushrooms(name, seed):
    """a small cluster of stylized mushrooms (slot 'Mushroom': vertex G = 0 stem .. 1 cap, B = cap underside)."""
    rnd = random.Random(seed); M = Mesh()
    for k in range(rnd.randint(3, 5)):
        base = Vector((rnd.gauss(0, .07), rnd.gauss(0, .07), -.01)); h = rnd.uniform(.05, .14); r = h * rnd.uniform(.55, .85)
        tilt = Vector((rnd.uniform(-.15, .15), rnd.uniform(-.15, .15), 1)).normalized()
        top = base + tilt * h; sides = 10
        tx = tilt.orthogonal().normalized(); ty = tilt.cross(tx)
        ring = lambda c, rad, z, col: [M.vert(c + (tx * math.cos(2 * math.pi * i / sides) + ty * math.sin(2 * math.pi * i / sides)) * rad + tilt * z, col,
                                              ((tx * math.cos(2 * math.pi * i / sides) + ty * math.sin(2 * math.pi * i / sides)) * .7 + tilt * .3).normalized()) for i in range(sides)]
        s0 = ring(base, r * .22, 0, (0, 0, 0, 1)); s1 = ring(top, r * .17, 0, (0, 0, 0, 1))
        for i in range(sides): M.face((s0[i], s0[(i + 1) % sides], s1[(i + 1) % sides], s1[i]), [(0, 0)] * 4)
        under = ring(top, r, -r * .05, (0, 1, 1, 1)); mid = ring(top, r * .8, r * .32, (0, 1, 0, 1))
        apex = M.vert(top + tilt * r * .5, (0, 1, 0, 1), tilt); hub = M.vert(top - tilt * r * .02, (0, 1, 1, 1), -tilt)
        for i in range(sides):
            j = (i + 1) % sides
            M.face((under[i], under[j], mid[j], mid[i]), [(0, 0)] * 4); M.face((mid[i], mid[j], apex), [(0, 0)] * 3)
            M.face((hub, under[j], under[i]), [(0, 0)] * 3)
    return M.build(name, ('Mushroom',))

def wood_tube(M, pts, radii, sides, moss_up=.55, uv_scale=1.0):
    """bark tube along pts (slot 0 'Bark'); vertex R = moss on up-facing bark (M_Vista_Bark)."""
    rings = []; acc_len = 0.0
    for i, (p, r) in enumerate(zip(pts, radii)):
        t = (pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized()
        a = t.orthogonal().normalized(); b = t.cross(a)
        if i: acc_len += (p - pts[i - 1]).length
        ring = []
        for j in range(sides):
            th = 2 * math.pi * j / sides; d = a * math.cos(th) + b * math.sin(th)
            rr = r * (1 + .08 * math.sin(th * 3 + i))
            moss = max(0, min(1, (d.z - (1 - moss_up)) / moss_up)) * (.6 + .4 * noise.noise(p * 2 + d))
            ring.append((M.vert(p + d * rr, (max(0, moss), 1, .5, 1), d), j / sides, acc_len / uv_scale))
        rings.append(ring)
    for i in range(len(rings) - 1):
        for j in range(sides):
            j2 = (j + 1) % sides
            a_, b_, c_, d_ = rings[i][j], rings[i][j2], rings[i + 1][j2], rings[i + 1][j]
            M.face((a_[0], b_[0], c_[0], d_[0]), [(a_[1], a_[2]), (b_[1] if j2 else 1.0, b_[2]), (c_[1] if j2 else 1.0, c_[2]), (d_[1], d_[2])], 0)
    return rings

def cap(M, ring, centre, n):
    c = M.vert(centre, (0, 1, .5, 1), n)
    for j in range(len(ring)):
        M.face((c, ring[j][0], ring[(j + 1) % len(ring)][0]), [(.5, .5), (.5 + .4 * math.cos(2 * math.pi * j / len(ring)), .5), (.5, .9)], 1)

def log(name, seed):
    """fallen mossy log (3.2 m) with a broken stub branch; end caps show the wood rings (slot 'Wood'); UCX hull."""
    rnd = random.Random(seed); M = Mesh(); L = 3.2; R0 = .32
    pts = [Vector((-L / 2 + L * t, .06 * math.sin(t * 4 + seed), R0 * .85 + .05 * math.sin(t * 3))) for t in [i / 10 for i in range(11)]]
    rad = [R0 * (1.08 - .2 * (i / 10)) for i in range(11)]
    rings = wood_tube(M, pts, rad, 12, .5, .8)
    cap(M, rings[0], pts[0], (pts[0] - pts[1]).normalized()); cap(M, rings[-1], pts[-1], (pts[-1] - pts[-2]).normalized())
    s0 = pts[4] + Vector((0, 0, rad[4] * .6)); sp = [s0 + Vector((.12 * t, .25 * t, .35 * t)) for t in (0, .5, 1)]
    br = wood_tube(M, sp, [.09, .07, .05], 7, .5, .8); cap(M, br[-1], sp[-1], (sp[-1] - sp[-2]).normalized())
    ob = M.build(name, ('Bark', 'Wood'), (True, False))
    hull = bmesh.new(); hv = [hull.verts.new(p + Vector((0, math.cos(a), math.sin(a))) * r) for p, r in zip(pts[::2], rad[::2]) for a in [2 * math.pi * i / 8 for i in range(8)]]
    bmesh.ops.convex_hull(hull, input=hv); h = obj(f'UCX_{name}_00', hull)
    return ob, [h], []

def stump(name, seed):
    """mossy tree stump with root flare and a ragged top (rings on the cut, slot 'Wood'); UCX hull."""
    rnd = random.Random(seed); M = Mesh(); R = .45; H = .7
    pts = [Vector((0, 0, -.1 + H * t)) for t in (0, .15, .4, .7, 1.0)]
    rad = [R * 1.5, R * 1.15, R * 1.0, R * .96, R * .94]
    rings = wood_tube(M, pts, rad, 14, .5, .9)
    top = [rp for rp in rings[-1]]
    c = M.vert(pts[-1] + Vector((0, 0, -.04)), (0, 1, .5, 1), UP)
    for j in range(len(top)):
        p0 = M.v[top[j][0]]
        M.v[top[j][0]] = (p0[0], p0[1], p0[2] + rnd.uniform(-.06, .1))            # ragged broken top
        M.face((c, top[(j + 1) % len(top)][0], top[j][0]), [(.5, .5), (.5 + .45 * math.cos(2 * math.pi * j / 14), .5 + .45 * math.sin(2 * math.pi * j / 14)),
                                                          (.5 + .45 * math.cos(2 * math.pi * (j + 1) / 14), .5 + .45 * math.sin(2 * math.pi * (j + 1) / 14))], 1)
    for k in range(5):                                                              # roots
        a = 2 * math.pi * k / 5 + rnd.uniform(-.3, .3); d = Vector((math.cos(a), math.sin(a), 0))
        rp = [d * R * .7 + UP * .3, d * (R + .12) + UP * .1, d * (R + .3) - UP * .02, d * (R + .45) - UP * .12]
        wood_tube(M, rp, [.2, .13, .07, .025], 8, .5, .9)
    ob = M.build(name, ('Bark', 'Wood'), (True, False))
    hull = bmesh.new(); hv = [hull.verts.new(Vector((math.cos(a) * r, math.sin(a) * r, z))) for z, r in ((-.1, R * 1.4), (H, R * .95)) for a in [2 * math.pi * i / 8 for i in range(8)]]
    bmesh.ops.convex_hull(hull, input=hv); h = obj(f'UCX_{name}_00', hull)
    return ob, [h], []

def branches(name, seed):
    """a few fallen twigs / small branches lying in the grass (no collision)."""
    rnd = random.Random(seed); M = Mesh()
    for k in range(4):
        a = rnd.uniform(0, math.pi); d = Vector((math.cos(a), math.sin(a), 0)); L = rnd.uniform(.6, 1.2)
        c = Vector((rnd.gauss(0, .3), rnd.gauss(0, .3), .025))
        pts = [c - d * L / 2 + d * L * t + Vector((0, 0, .01 * math.sin(t * 5))) for t in (0, .33, .66, 1)]
        wood_tube(M, pts, [.03, .025, .02, .012], 5, .5, .5)
        if rnd.random() < .7:
            q = pts[1]; e = q + (d.cross(UP) * rnd.choice((-1, 1)) + d * .6).normalized() * L * .35
            wood_tube(M, [q, q.lerp(e, .5), e], [.015, .012, .006], 4, .5, .5)
    return M.build(name, ('Bark',), (True,)), [], []

def ivy_curtain(name, seed, width=2.0, length=2.4):
    """hanging ivy curtain for cliff faces / walls: dense leaf-bunch strands from a top line, thinning downward, with a
    mat lying over the top edge. Pivot at the top edge centre (snap to a ledge)."""
    rnd = random.Random(seed); M = Mesh()
    for s in range(int(width * 6)):
        x = -width / 2 + width * (s + rnd.random()) / int(width * 6); L = length * rnd.uniform(.45, 1.0); z = 0.0
        while z < L:
            t = z / L
            p = Vector((x + .06 * math.sin(z * 3 + s), -.08 - .05 * rnd.random(), -z))
            fc = Vector((rnd.uniform(-.4, .4), -1, rnd.uniform(-.1, .4))).normalized()
            bunch_card(M, rnd, p, fc, rnd.uniform(.3, .42) * (1 - .35 * t), ramp(.62 - .35 * t + rnd.gauss(0, .07)), Vector((0, -.85, .5)).normalized(), mat=0)
            z += rnd.uniform(.1, .18) * (1 + t)
    for k in range(int(width * 10)):
        p = Vector((rnd.uniform(-width / 2, width / 2), rnd.uniform(-.1, .5), .04))
        bunch_card(M, rnd, p, (UP * .8 + Vector((0, -.4, 0))).normalized(), rnd.uniform(.35, .5), ramp(.7 + rnd.gauss(0, .07)), UP, mat=0)
    return M.build(name, ('Leaves',), (True,)), [], []

# ---------------------------------------------------------------- preview renders (golden hour)
def preview_scene():
    sc = bpy.context.scene; sc.render.engine = 'CYCLES'; sc.cycles.samples = 32; sc.cycles.use_denoising = True
    sc.cycles.transparent_max_bounces = 48; sc.view_settings.view_transform = 'AgX'; sc.view_settings.look = 'AgX - Punchy'; sc.view_settings.exposure = .6
    w = bpy.data.worlds.new('W'); sc.world = w; w.node_tree.nodes['Background'].inputs[0].default_value = (.42, .58, .85, 1)
    sun = bpy.data.objects.new('Sun', bpy.data.lights.new('Sun', 'SUN')); sc.collection.objects.link(sun)
    sun.data.energy = 5.0; sun.data.color = (1, .82, .6); sun.data.angle = math.radians(6)
    sun.rotation_euler = Vector((-.45, .7, -.55)).normalized().to_track_quat('-Z', 'Y').to_euler()
    cam = bpy.data.objects.new('Cam', bpy.data.cameras.new('Cam')); sc.collection.objects.link(cam); sc.camera = cam
    gp = bpy.data.meshes.new('PG'); gp.from_pydata([(-200, -200, 0), (200, -200, 0), (200, 200, 0), (-200, 200, 0)], [], [(0, 1, 2, 3)])
    go = bpy.data.objects.new('PreviewGround', gp); sc.collection.objects.link(go)
    gm = bpy.data.materials.new('PG'); gm.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = lin((112, 140, 32)) + (1,)
    go.data.materials.append(gm)
    return sc, cam

TEX2 = OUT + '/textures'
def preview_materials():
    """Blender look-alikes of the Unreal materials, keyed by slot name - with the SAME painted textures + relief
    (World_R02/textures, Trees_R02/textures) so the previews show the real surface detail. Foliage gets some
    translucency (Unreal two-sided foliage transmits light; plain Cycles back faces would read black)."""
    def img(p, data=False):
        im = bpy.data.images.get(os.path.basename(p)) or bpy.data.images.load(p)
        if data: im.colorspace_settings.name = 'Non-Color'
        return im
    def base(mn):
        m = bpy.data.materials.get(mn) or bpy.data.materials.new(mn)
        nd = m.node_tree.nodes
        for n_ in [n_ for n_ in nd if n_.type not in ('BSDF_PRINCIPLED', 'OUTPUT_MATERIAL')]: nd.remove(n_)
        return m, nd, m.node_tree.links, nd['Principled BSDF']
    def mult(nd, ln, a, b):
        x = nd.new('ShaderNodeMix'); x.data_type = 'RGBA'; x.blend_type = 'MULTIPLY'; x.inputs[0].default_value = 1
        ln.new(a, x.inputs[6])
        if isinstance(b, tuple): x.inputs[7].default_value = b
        else: ln.new(b, x.inputs[7])
        return x.outputs[2]
    def vcol(nd, ln):
        va = nd.new('ShaderNodeVertexColor'); va.layer_name = 'Col'; sep = nd.new('ShaderNodeSeparateColor'); ln.new(va.outputs[0], sep.inputs[0]); return va, sep
    def ramp_node(nd, ln, inp, stops):
        cr = nd.new('ShaderNodeValToRGB'); el = cr.color_ramp.elements
        el[0].position, el[0].color = stops[0][0], lin(stops[0][1]) + (1,); el[1].position, el[1].color = stops[-1][0], lin(stops[-1][1]) + (1,)
        for p_, c_ in stops[1:-1]: e = el.new(p_); e.color = lin(c_) + (1,)
        ln.new(inp, cr.inputs[0]); return cr.outputs[0]
    def box(nd, ln, path, scale, data=False):
        tc = nd.new('ShaderNodeTexCoord'); mp = nd.new('ShaderNodeMapping'); mp.inputs['Scale'].default_value = (1 / scale,) * 3
        ln.new(tc.outputs['Object'], mp.inputs['Vector'])
        t = nd.new('ShaderNodeTexImage'); t.image = img(path, data); t.projection = 'BOX'; t.projection_blend = .25
        ln.new(mp.outputs['Vector'], t.inputs['Vector']); return t
    def bump(nd, ln, bsdf, h, strength, dist):
        bp = nd.new('ShaderNodeBump'); bp.inputs['Strength'].default_value = strength; bp.inputs['Distance'].default_value = dist
        ln.new(h, bp.inputs['Height']); ln.new(bp.outputs['Normal'], bsdf.inputs['Normal'])
    def translucent(m, nd, ln, bsdf, col, amount, alpha=None):
        tr = nd.new('ShaderNodeBsdfTranslucent'); ln.new(col, tr.inputs['Color'])
        mx = nd.new('ShaderNodeMixShader'); mx.inputs[0].default_value = amount; ln.new(bsdf.outputs[0], mx.inputs[1]); ln.new(tr.outputs[0], mx.inputs[2])
        out = mx.outputs[0]
        if alpha is not None:
            tp = nd.new('ShaderNodeBsdfTransparent'); m2 = nd.new('ShaderNodeMixShader'); ln.new(alpha, m2.inputs[0]); ln.new(tp.outputs[0], m2.inputs[1])
            ln.new(out, m2.inputs[2]); out = m2.outputs[0]
        ln.new(out, nd['Material Output'].inputs['Surface']); m.use_backface_culling = False
    def mad(nd, ln, inp, a, b):
        n_ = nd.new('ShaderNodeMath'); n_.operation = 'MULTIPLY_ADD'; ln.new(inp, n_.inputs[0]); n_.inputs[1].default_value = a; n_.inputs[2].default_value = b; return n_.outputs[0]
    def mulf(nd, ln, a, b):
        n_ = nd.new('ShaderNodeMath'); n_.operation = 'MULTIPLY'; ln.new(a, n_.inputs[0]); ln.new(b, n_.inputs[1]); return n_.outputs[0]
    # stone: rocks, cliffs, path stones (M_Rock_Stone / M_Vista_Rock look-alike)
    m, nd, ln, bsdf = base('Rock')
    alb = box(nd, ln, TEX2 + '/Stone_R02_BaseColor.png', 2.2); hg = box(nd, ln, TEX2 + '/Stone_R02_Height.png', 2.2, True)
    va, sep = vcol(nd, ln)
    f = mulf(nd, ln, mulf(nd, ln, mad(nd, ln, sep.outputs[0], .5, .72), mad(nd, ln, sep.outputs[1], .28, .78)), mad(nd, ln, sep.outputs[2], .24, .88))
    ln.new(mult(nd, ln, mult(nd, ln, alb.outputs['Color'], f), (1.0, .95, .88, 1)), bsdf.inputs['Base Color'])
    bsdf.inputs['Roughness'].default_value = .85; bump(nd, ln, bsdf, hg.outputs['Color'], .6, .03)
    # bark: Trees_R02 bark + relief (UVs from the trunk tubes)
    m, nd, ln, bsdf = base('Bark')
    tb = nd.new('ShaderNodeTexImage'); tb.image = img(TREES2 + '/Bark_Painted_R02_BaseColor.png')
    th = nd.new('ShaderNodeTexImage'); th.image = img(TEX2 + '/Bark_R02_Height.png', True)
    ln.new(tb.outputs['Color'], bsdf.inputs['Base Color']); bump(nd, ln, bsdf, th.outputs['Color'], .7, .02)
    # grass-type: blade texture x root->tip ramp, per-blade hue (vertex B)
    for mn, stops in (('Grass', [(0, (52, 96, 12)), (.5, (130, 166, 18)), (1, (204, 210, 34))]),
                      ('Wheat', [(0, (170, 128, 40)), (1, (244, 200, 70))])):
        m, nd, ln, bsdf = base(mn); va, sep = vcol(nd, ln)
        uvn = nd.new('ShaderNodeUVMap'); bt = nd.new('ShaderNodeTexImage'); bt.image = img(TEX2 + '/GrassBlade_R02_BaseColor.png'); ln.new(uvn.outputs[0], bt.inputs['Vector'])
        col = ramp_node(nd, ln, sep.outputs[0], stops)
        hue = nd.new('ShaderNodeMix'); hue.data_type = 'RGBA'; ln.new(sep.outputs[2], hue.inputs[0])
        hue.inputs[6].default_value = (1.0, 1.0, 1.0, 1); hue.inputs[7].default_value = (1.18, 1.05, .6, 1)
        c = mult(nd, ln, mult(nd, ln, col, bt.outputs['Color']), hue.outputs[2])
        if mn == 'Wheat':
            ear = nd.new('ShaderNodeMix'); ear.data_type = 'RGBA'; ln.new(sep.outputs[1], ear.inputs[0]); ln.new(c, ear.inputs[6])
            ear.inputs[7].default_value = lin((246, 196, 58)) + (1,); c = ear.outputs[2]
        ln.new(c, bsdf.inputs['Base Color']); bsdf.inputs['Roughness'].default_value = .8
        translucent(m, nd, ln, bsdf, c, .3)
    # flowers: G = kind (0 stem, .5 centre, 1 petal), B = petal shade; petal texture by UV
    for mn, pc, cc in (('Flowers', (232, 52, 24), (5, 4, 3)), ('Daisy', (250, 246, 232), (240, 176, 24))):
        m, nd, ln, bsdf = base(mn); va, sep = vcol(nd, ln)
        uvn = nd.new('ShaderNodeUVMap'); pt = nd.new('ShaderNodeTexImage'); pt.image = img(TEX2 + '/Petal_R02_BaseColor.png'); ln.new(uvn.outputs[0], pt.inputs['Vector'])
        petal = mult(nd, ln, pt.outputs['Color'], lin(pc) + (1,))
        stem = ramp_node(nd, ln, sep.outputs[0], [(0, (40, 80, 14)), (1, (80, 130, 22))])
        k = nd.new('ShaderNodeMath'); k.operation = 'GREATER_THAN'; ln.new(sep.outputs[1], k.inputs[0]); k.inputs[1].default_value = .75
        kc = nd.new('ShaderNodeMath'); kc.operation = 'GREATER_THAN'; ln.new(sep.outputs[1], kc.inputs[0]); kc.inputs[1].default_value = .25
        c1 = nd.new('ShaderNodeMix'); c1.data_type = 'RGBA'; ln.new(kc.outputs[0], c1.inputs[0]); ln.new(stem, c1.inputs[6]); c1.inputs[7].default_value = lin(cc) + (1,)
        c2 = nd.new('ShaderNodeMix'); c2.data_type = 'RGBA'; ln.new(k.outputs[0], c2.inputs[0]); ln.new(c1.outputs[2], c2.inputs[6]); ln.new(petal, c2.inputs[7])
        ln.new(c2.outputs[2], bsdf.inputs['Base Color']); translucent(m, nd, ln, bsdf, c2.outputs[2], .35)
    for mn, pc, cc in (('Bellflower', (186, 130, 236), (90, 60, 130)), ('BlueFlower', (110, 150, 245), (245, 220, 90))):
        m, nd, ln, bsdf = base(mn); va, sep = vcol(nd, ln)
        petal = ramp_node(nd, ln, sep.outputs[2], [(0, tuple(int(x * .55) for x in pc)), (1, pc)])
        stem = ramp_node(nd, ln, sep.outputs[0], [(0, (40, 80, 14)), (1, (80, 130, 22))])
        k = nd.new('ShaderNodeMath'); k.operation = 'GREATER_THAN'; ln.new(sep.outputs[1], k.inputs[0]); k.inputs[1].default_value = .75
        kc = nd.new('ShaderNodeMath'); kc.operation = 'GREATER_THAN'; ln.new(sep.outputs[1], kc.inputs[0]); kc.inputs[1].default_value = .25
        c1 = nd.new('ShaderNodeMix'); c1.data_type = 'RGBA'; ln.new(kc.outputs[0], c1.inputs[0]); ln.new(stem, c1.inputs[6]); c1.inputs[7].default_value = lin(cc) + (1,)
        c2 = nd.new('ShaderNodeMix'); c2.data_type = 'RGBA'; ln.new(k.outputs[0], c2.inputs[0]); ln.new(c1.outputs[2], c2.inputs[6]); ln.new(petal, c2.inputs[7])
        ln.new(c2.outputs[2], bsdf.inputs['Base Color']); translucent(m, nd, ln, bsdf, c2.outputs[2], .35)
    # moss cushion: moss texture (object box) x vertex G light
    m, nd, ln, bsdf = base('Moss'); va, sep = vcol(nd, ln)
    mt = box(nd, ln, TREES2 + '/Moss_Painted_BaseColor.png', .8)
    ln.new(mult(nd, ln, mt.outputs['Color'], ramp_node(nd, ln, sep.outputs[1], [(0, (120, 130, 90)), (1, (255, 255, 220))])), bsdf.inputs['Base Color'])
    bsdf.inputs['Roughness'].default_value = .95
    # mushroom: warm red-brown cap with a lighter rim, cream stem + gills (G cap, B underside)
    m, nd, ln, bsdf = base('Mushroom'); va, sep = vcol(nd, ln)
    cap_c = ramp_node(nd, ln, sep.outputs[2], [(0, (176, 70, 38)), (1, (232, 214, 180))])
    mx = nd.new('ShaderNodeMix'); mx.data_type = 'RGBA'; ln.new(sep.outputs[1], mx.inputs[0]); mx.inputs[6].default_value = lin((236, 224, 196)) + (1,); ln.new(cap_c, mx.inputs[7])
    ln.new(mx.outputs[2], bsdf.inputs['Base Color']); bsdf.inputs['Roughness'].default_value = .6
    # cut wood (log / stump ends): rings
    m, nd, ln, bsdf = base('Wood')
    tc = nd.new('ShaderNodeTexCoord'); wv = nd.new('ShaderNodeTexWave'); wv.wave_type = 'RINGS'; wv.inputs['Scale'].default_value = 6; wv.inputs['Distortion'].default_value = 3
    ln.new(tc.outputs['UV'], wv.inputs['Vector'])
    ln.new(ramp_node(nd, ln, wv.outputs['Fac'], [(0, (150, 96, 50)), (1, (214, 160, 96))]), bsdf.inputs['Base Color'])
    # bark moss (vertex R) for logs / stumps / branches in the preview
    m = bpy.data.materials['Bark']; nd = m.node_tree.nodes; ln = m.node_tree.links; bsdf = nd['Principled BSDF']
    tb = [n_ for n_ in nd if n_.type == 'TEX_IMAGE' and 'Bark_Painted' in n_.image.name][0]
    va, sep = vcol(nd, ln); mt = nd.new('ShaderNodeTexImage'); mt.image = img(TREES2 + '/Moss_Painted_BaseColor.png')
    mm = nd.new('ShaderNodeMix'); mm.data_type = 'RGBA'; ln.new(sep.outputs[0], mm.inputs[0]); ln.new(tb.outputs['Color'], mm.inputs[6]); ln.new(mt.outputs['Color'], mm.inputs[7])
    ln.new(mm.outputs[2], bsdf.inputs['Base Color'])
    # leaves (leaf-bunch atlas x vertex colour) + fill blobs
    m, nd, ln, bsdf = base('Leaves'); va, sep = vcol(nd, ln)
    ti = nd.new('ShaderNodeTexImage'); ti.image = img(TREES2 + '/Leaf_Cluster_Atlas_BaseColor.png')
    c = mult(nd, ln, mult(nd, ln, ti.outputs['Color'], va.outputs['Color']), (1.3, 1.3, 1.3, 1))
    ln.new(c, bsdf.inputs['Base Color']); bsdf.inputs['Roughness'].default_value = .85
    translucent(m, nd, ln, bsdf, c, .3, ti.outputs['Alpha'])
    m.surface_render_method = 'DITHERED'
    m, nd, ln, bsdf = base('Dapple'); va, sep = vcol(nd, ln)
    ti = nd.new('ShaderNodeTexImage'); ti.image = img(TEX2 + '/Leaf_Dapple_Atlas_BaseColor.png')
    c = mult(nd, ln, mult(nd, ln, ti.outputs['Color'], va.outputs['Color']), (1.3, 1.3, 1.3, 1))
    ln.new(c, bsdf.inputs['Base Color']); bsdf.inputs['Roughness'].default_value = .85
    translucent(m, nd, ln, bsdf, c, .3, ti.outputs['Alpha']); m.surface_render_method = 'DITHERED'
    m, nd, ln, bsdf = base('Fill'); va, sep = vcol(nd, ln)
    ln.new(ramp_node(nd, ln, sep.outputs[1], [(0, (26, 58, 36)), (.5, (70, 120, 30)), (1, (160, 196, 28))]), bsdf.inputs['Base Color'])
    # terrain swatch: painted ground grass / dirt by vertex R (path), sand -> wet by G, stone on slopes
    m, nd, ln, bsdf = base('Terrain'); va, sep = vcol(nd, ln)
    gg = box(nd, ln, TEX2 + '/GroundGrass_R02_BaseColor.png', 4.0); gd = box(nd, ln, TEX2 + '/GroundDirt_R02_BaseColor.png', 3.0)
    st = box(nd, ln, TEX2 + '/Stone_R02_BaseColor.png', 4.0)
    nz = nd.new('ShaderNodeTexNoise'); nz.inputs['Scale'].default_value = 1.4
    pe = nd.new('ShaderNodeMath'); pe.operation = 'MULTIPLY_ADD'; ln.new(nz.outputs['Fac'], pe.inputs[0]); pe.inputs[1].default_value = .5; ln.new(sep.outputs[0], pe.inputs[2])
    def mix_by(fac, lo, hi, a, b):
        r = nd.new('ShaderNodeMapRange'); ln.new(fac, r.inputs[0]); r.inputs[1].default_value = lo; r.inputs[2].default_value = hi
        x = nd.new('ShaderNodeMix'); x.data_type = 'RGBA'; ln.new(r.outputs[0], x.inputs[0])
        for i, v in ((6, a), (7, b)):
            if isinstance(v, tuple): x.inputs[i].default_value = v
            else: ln.new(v, x.inputs[i])
        return x.outputs[2]
    c = mix_by(pe.outputs[0], .62, .82, gg.outputs['Color'], gd.outputs['Color'])
    c = mix_by(sep.outputs[1], .12, .4, c, lin((196, 160, 70)) + (1,))
    c = mix_by(sep.outputs[1], .5, .75, c, lin((118, 92, 52)) + (1,))
    gn = nd.new('ShaderNodeNewGeometry'); sz = nd.new('ShaderNodeSeparateXYZ'); ln.new(gn.outputs['Normal'], sz.inputs[0])
    c = mix_by(sz.outputs[2], .82, .62, c, st.outputs['Color'])
    ln.new(c, bsdf.inputs['Base Color']); bsdf.inputs['Roughness'].default_value = .95

def patch(ob, n, radius, seed, extra=()):
    """linked duplicates spread over a disc (how ground cover is actually seen) + optional companion meshes."""
    rnd = random.Random(seed); out = []
    for src, cnt in [(ob, n)] + list(extra):
        for k in range(cnt):
            d = src.copy(); bpy.context.scene.collection.objects.link(d)
            a = rnd.uniform(0, 2 * math.pi); r = radius * math.sqrt(rnd.random())
            d.location = (math.cos(a) * r, math.sin(a) * r, 0); d.rotation_euler = (0, 0, rnd.uniform(0, 6.3))
            s = rnd.uniform(.8, 1.25); d.scale = (s, s, s); d.hide_render = False; out.append(d)
    return out

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
    ('hero', 'SM_Vista_Tree_Hero01', lambda: hero_tree('SM_Vista_Tree_Hero01', 501), (24, 6.5, 30, 4, 40)),
    ('trees', 'SM_Vista_Tree_Round', lambda: round_tree('SM_Vista_Tree_Round', 11, 5.5, 4.4, 6, leaf_size=.72, density=120), (16, 6)),
    ('trees', 'SM_Vista_Tree_Tall', lambda: round_tree('SM_Vista_Tree_Tall', 12, 7.5, 3.6, 5, lean=.3, leaf_size=.72, density=120), (17, 6)),
    ('trees', 'SM_Vista_Tree_Wide', lambda: round_tree('SM_Vista_Tree_Wide', 13, 4.6, 5.4, 7, lean=.6, leaf_size=.78, density=100), (17, 5.5)),
    ('grass', 'SM_Vista_Grass', lambda: grass_lods('SM_Vista_Grass', 21, 76, .2, .44, .032, .45, segs=2, lean=(2, 16), bend=(10, 40)), (5.0, .2, 'patch', 60, 2.4)),
    ('grass', 'SM_Vista_GrassTall', lambda: grass_lods('SM_Vista_GrassTall', 22, 56, .5, .9, .04, .34, segs=3, lean=(4, 22), bend=(20, 70)), (5.0, .3, 'patch', 30, 2.0)),
    ('grass', 'SM_Vista_Tuft', lambda: grass_lods('SM_Vista_Tuft', 27, 54, .32, .66, .04, .2, segs=3, lean=(10, 35), bend=(25, 70)), (4.0, .25, 'patch', 14, 1.4)),
    ('wheat', 'SM_Vista_Wheat', lambda: (wheat('SM_Vista_Wheat', 23), [], [wheat('SM_Vista_Wheat_LOD1', 23, 12, 1)]), (5.0, .7, 'patch', 40, 2.0)),
    ('flowers', 'SM_Vista_Poppies', lambda: flower_lods('SM_Vista_Poppies', 24, 'poppy'), (3.0, .25, 'patch', 10, 1.2, 'SM_Vista_Grass', 18)),
    ('flowers', 'SM_Vista_Daisies', lambda: flower_lods('SM_Vista_Daisies', 25, 'daisy'), (2.6, .2, 'patch', 9, 1.0, 'SM_Vista_Grass', 16)),
    ('details', 'SM_Vista_Bush_A', lambda: bush('SM_Vista_Bush_A', 41, .9, 5), (4.5, .7)),
    ('details', 'SM_Vista_Bush_B', lambda: bush('SM_Vista_Bush_B', 42, .6, 4), (3.2, .5)),
    ('details', 'SM_Vista_Reeds', lambda: reeds('SM_Vista_Reeds', 43), (3.2, .8)),
    ('details', 'SM_Vista_PathStones', lambda: path_stones('SM_Vista_PathStones', 44), (2.0, .05)),
    ('details', 'SM_Vista_FallenLeaves', lambda: fallen_leaves('SM_Vista_FallenLeaves', 45), (2.2, 0.0)),
    ('ground', 'SM_Review_GroundSwatch', lambda: ground_swatch('SM_Review_GroundSwatch'), None),
    ('details', 'SM_Vista_MossPatch', lambda: (moss_patch('SM_Vista_MossPatch', 51), [], [moss_patch('SM_Vista_MossPatch', 51, lod=1)]), (2.6, .1)),
    ('details', 'SM_Vista_Fern', lambda: (fern('SM_Vista_Fern', 52), [], [fern('SM_Vista_Fern', 52, 5, 1)]), (2.4, .3)),
    ('details', 'SM_Vista_Bellflowers', lambda: (wildflowers('SM_Vista_Bellflowers', 53, 'bell', 20), [], [wildflowers('SM_Vista_Bellflowers', 53, 'bell', 20, 1)]), (2.2, .15, 'patch', 6, .8, 'SM_Vista_Grass', 12)),
    ('details', 'SM_Vista_BlueFlowers', lambda: (wildflowers('SM_Vista_BlueFlowers', 54, 'blue', 16), [], [wildflowers('SM_Vista_BlueFlowers', 54, 'blue', 16, 1)]), (2.2, .15, 'patch', 6, .8, 'SM_Vista_Grass', 12)),
    ('details', 'SM_Vista_Clover', lambda: (clover('SM_Vista_Clover', 55), [], [clover('SM_Vista_Clover', 55, 1)]), (1.8, .05)),
    ('details', 'SM_Vista_Mushrooms', lambda: (mushrooms('SM_Vista_Mushrooms', 56), [], []), (.8, .05)),
    ('details', 'SM_Vista_Log', lambda: log('SM_Vista_Log', 57), (5.0, .3)),
    ('details', 'SM_Vista_Stump', lambda: stump('SM_Vista_Stump', 58), (3.0, .3)),
    ('details', 'SM_Vista_Branches', lambda: branches('SM_Vista_Branches', 59), (2.6, .05)),
    ('details', 'SM_Vista_IvyCurtain', lambda: ivy_curtain('SM_Vista_IvyCurtain', 60), (5.0, -1.0)),
    ('cliffs', 'SM_Vista_Cliff_A', lambda: cliff_r02('SM_Vista_Cliff_A', 31), (30, 5)),
    ('cliffs', 'SM_Vista_Cliff_B', lambda: cliff_r02('SM_Vista_Cliff_B', 47, layers=3), (26, 4)),
]
preview_materials()
BUILT = {}
for group, name, fn, view in JOBS:
    if not want(group, name): continue
    ob, extra, lods = fn()
    export(ob, name, extra, lods)
    if view:
        for o in [ob] + lods: o.location = (0, 0, 0)
        if len(view) > 2 and view[2] == 'patch':
            comp = [(BUILT[view[5]], view[6])] if len(view) > 5 and view[5] in BUILT else []
            obs = patch(ob, view[3], view[4], hash(name) & 0xff, comp)
            preview(sc, cam, obs, name, view[0], view[1], el=14)
            for o in obs: bpy.data.objects.remove(o)
        else:
            preview(sc, cam, [ob], name, *view[:2])
        if lods: preview(sc, cam, [lods[-1]], name + f'_LOD{len(lods)}', *view[:2])
    BUILT[name] = ob
    for o in [ob] + list(extra) + list(lods): o.hide_render = True; o.hide_set(True)
print('VISTA_ASSETS_EXPORTED')
