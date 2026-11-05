"""Blender: rebuild the FOLIAGE of the AI-permitted Fab pack trees (Lux Art Studios Vegetation Stylized Kit trees A-J,
JABAMI Anime Tree v4 - both CC BY 4.0) as full ROUNDED CLUMPS (user 2026-11-04: "full, rounded clumps instead of flat
meshes stacked on top of each other"). Trunks + branches (and their UVs / bark material slot) stay the pack's own.
  blender -b --python grow_fab_canopies.py [-- A I Jabami ...]       (default: all 11 trees)
Why: the pack canopies are a few hundred LARGE single flat quads (Tree I: 900 quads, each ~1/6 of the tree height = ~3 m
at 15-20 m) - with our edge-to-edge leaf-spray mask (MI_Fab_OurLeaves_* = hero leaves) every quad reads as a flat sheet.
How (the hero-tree method, Tools/World/export_vista_assets.hero_tree):
- the pack's leaf quads only say WHERE the foliage is: their centres are grouped into clumps (k-means) -> one ellipsoid
  per clump; the quads themselves are dropped.
- every clump gets SPRAYS spread over a fibonacci sphere (front, back, sides AND underside), each rooted on the nearest
  branch point inside the clump and reaching out to a staggered depth (75-105% of the clump radius); along each spray
  smaller folded cards (1.2-1.9 m) at varied tilt / roll, 35% with a crossed partner (no spray vanishes edge-on).
- small irregular gaps: sprays are skipped where a noise field over the clump surface is high (no extra parallel cards).
- shading: every card corner takes its normal from its place on the clump ellipsoid (+ a little crown + up) -> the
  light wraps the whole clump, with shade underneath and inside, instead of each card lighting as its own sheet.
- LOD1/2 = subsets of the LOD0 cards, grown a little (no popping); bark LOD1 from the pack's LOD1.
Output: outputs/FabPacks/_canopy_fbx/<SM_name>.fbx (+ _LOD1/_LOD2, UCX trunk hull, manifest.json) - outside git (pack
geometry). Unreal import: Tools/World/restyle_fab_trees.py (uses these when present). Prints CANOPY lines."""
import bpy, bmesh, math, random, sys, os, json, zlib
from mathutils import Vector, noise
from mathutils.kdtree import KDTree

HERE = 'C:/UNREAL PROJECTS/RPG_CODEX/Tools/World'
OUTS = 'C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs'
SRC_FBX = OUTS + '/FabPacks/_restyle_fbx'                           # pack trees exported from Unreal (restyle_fab_trees.py)
JABAMI_FBX = OUTS + '/FabPacks/Jabami_AnimeTree_v4/extracted/source/inner/medium01.fbx'   # original (the Unreal export lost its UVs)
OUT = OUTS + '/FabPacks/_canopy_fbx'
os.makedirs(OUT, exist_ok=True)

# shared helpers from the vista asset exporter (Mesh accumulator, bunch_card, hero_ramp, LOD_KEEP/GROW, trunk_hull)
_src = open(HERE + '/export_vista_assets.py').read()
_src = _src[:_src.index('# ---------------------------------------------------------------- build')]
V = {'__name__': 'vista_assets_defs'}
exec(compile(_src, 'export_vista_assets', 'exec'), V)
Mesh, bunch_card, hero_ramp, trunk_hull = V['Mesh'], V['bunch_card'], V['hero_ramp'], V['trunk_hull']
LOD_KEEP, LOD_GROW, UP = V['LOD_KEEP'], V['LOD_GROW'], Vector((0, 0, 1))

TARGET = {c: 15.0 + 5.0 * ((i * 0.618) % 1.0) for i, c in enumerate('ABCDEFGHIJ')}   # same 15-20 m as restyle_fab_trees.py
JOBS = [(c, f'{SRC_FBX}/SM_Lux_Tree_{c}.fbx', f'SM_Lux_Tree_{c}', TARGET[c]) for c in 'ABCDEFGHIJ'] + \
       [('Jabami', JABAMI_FBX, 'SM_Jabami_Tree_Ours', 17.5),
        # our own ancient hero tree (Trees_R02, 25 m, vista places it at 0.75): same leaves as the Fab trees (user 2026-11-05);
        # H None = keep its size; its solid 'Fill' crown blobs are dropped with the leaf cards; its own 10 UCX hulls are kept
        ('Ancient', OUTS + '/Trees_R02/exports/SM_Tree_Ancient.fbx', 'SM_Tree_Ancient_Clumps', None)]
BUDGET = {'Ancient': 1.3}    # leaf budget multiplier (bigger crown)
ARGS = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
LEAF_TRIS = 13000            # LOD0 foliage budget per tree (hero tree: ~12k leaf tris)
CARD = (1.2, 1.9)            # card size range, m (pack quads ~3 m)

def is_leaf(m): return m is not None and any(k in m.name.lower() for k in ('leaf', 'leave', 'bush'))

def load(fbx):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=fbx)
    obs = [o for o in bpy.context.scene.objects if o.type == 'MESH' and not o.name.upper().startswith('UCX')]
    lod = lambda o: int(o.name.rsplit('_LOD', 1)[1]) if '_LOD' in o.name else 0
    return sorted(obs, key=lod)

def world_pts(ob):
    mw = ob.matrix_world
    return [mw @ v.co for v in ob.data.vertices]

def bark_mesh(ob, s, M):
    """the pack's trunk + branches (non-leaf faces) scaled by s, into Mesh M (material 0), with its own UVs + normals."""
    me = ob.data; mw = ob.matrix_world; rot = mw.to_3x3()
    uvl = next((u for u in me.uv_layers if 'light' not in u.name.lower()), me.uv_layers[0] if me.uv_layers else None)
    cn = me.corner_normals if hasattr(me, 'corner_normals') else None
    n = 0
    for p in me.polygons:
        m_ = me.materials[p.material_index] if me.materials else None
        if is_leaf(m_) or (m_ is not None and 'fill' in m_.name.lower()): continue   # leaf cards + solid canopy fill blobs
        idx, uvs = [], []
        for li in p.loop_indices:
            vi = me.loops[li].vertex_index
            nr = (rot @ (cn[li].vector if cn else me.vertices[vi].normal)).normalized()
            idx.append(M.vert((mw @ me.vertices[vi].co) * s, (.4, .3, .2, 1), nr))
            uvs.append(tuple(uvl.data[li].uv) if uvl else (0, 0))
        M.face(idx, uvs, 0); n += 1
    return n

def leaf_islands(ob):
    """centres + diagonals of the pack's leaf quads (connected leaf faces)."""
    me = ob.data; mw = ob.matrix_world
    bm = bmesh.new(); bm.from_mesh(me); bm.faces.ensure_lookup_table()
    leafm = {i for i, m in enumerate(me.materials) if is_leaf(m)}
    seen, out = set(), []
    for f in bm.faces:
        if f.material_index not in leafm or f.index in seen: continue
        st, g = [f], []; seen.add(f.index)
        while st:
            x = st.pop(); g.append(x)
            for e in x.edges:
                for y in e.link_faces:
                    if y.material_index in leafm and y.index not in seen: seen.add(y.index); st.append(y)
        cs = [mw @ v.co for v in {v for x in g for v in x.verts}]
        c = sum(cs, Vector()) / len(cs)
        lo = Vector([min(q[i] for q in cs) for i in range(3)]); hi = Vector([max(q[i] for q in cs) for i in range(3)])
        out.append((c, (hi - lo).length))
    bm.free(); return out

def kmeans(pts, k, seed):
    rnd = random.Random(seed); cen = [p.copy() for p in rnd.sample(pts, k)]
    for _ in range(30):
        groups = [[] for _ in cen]
        for p in pts: groups[min(range(len(cen)), key=lambda i: (p - cen[i]).length_squared)].append(p)
        groups = [g for g in groups if g]
        cen = [sum(g, Vector()) / len(g) for g in groups]
    return groups

def ellipsoid(g, pad, rmin=1.0):
    c = sum(g, Vector()) / len(g)
    r = Vector([max(rmin, 1.7 * math.sqrt(sum((p[i] - c[i]) ** 2 for p in g) / len(g)) + pad) for i in range(3)])
    r.z = max(rmin, r.z * .85)
    return c, r

def e_normal(q, c, r):
    n = Vector(((q.x - c.x) / r.x ** 2, (q.y - c.y) / r.y ** 2, (q.z - c.z) / r.z ** 2))
    return n.normalized() if n.length > 1e-6 else UP.copy()

def e_depth(q, c, r):
    return Vector(((q.x - c.x) / r.x, (q.y - c.y) / r.y, (q.z - c.z) / r.z)).length    # 0 centre .. 1 surface

def fib(n, i):
    z = 1 - 2 * (i + .5) / n; a = i * 2.39996
    return Vector((math.cos(a) * math.sqrt(1 - z * z), math.sin(a) * math.sqrt(1 - z * z), z))

def grow(key, fbx, name, H):
    obs = load(fbx)
    if not obs: print('CANOPY_PROBLEM no mesh in', fbx); return
    ucx = [o for o in bpy.context.scene.objects if o.type == 'MESH' and o.name.upper().startswith('UCX')] if key == 'Ancient' else []
    for li in ((1, 2) if len(obs) == 1 else ()):                       # LODs in separate files (our Blender trees)
        lf = fbx[:-4] + f'_LOD{li}.fbx'
        if not os.path.exists(lf): break
        before = set(bpy.context.scene.objects); bpy.ops.import_scene.fbx(filepath=lf)
        obs += [o for o in bpy.context.scene.objects if o not in before and o.type == 'MESH' and not o.name.upper().startswith('UCX')][:1]
    lod0 = obs[0]
    pts = world_pts(lod0); z0 = min(p.z for p in pts)
    if H is None: H = max(p.z for p in pts) - z0
    s = H / (max(p.z for p in pts) - z0)
    isl = [(c * s, d * s) for c, d in leaf_islands(lod0)]
    if not isl: print('CANOPY_PROBLEM no leaf faces', name); return
    anchors = [c for c, _ in isl]; med_d = sorted(d for _, d in isl)[len(isl) // 2]
    rnd = random.Random(zlib.crc32(name.encode()))                     # stable per tree (hash() changes every run)
    k = max(4, min(10, round(math.sqrt(len(anchors)) / 4)))          # user 2026-11-04 picked these full clumps over tighter ones
    clumps = [ellipsoid(g, med_d * .3) for g in kmeans(anchors, k, rnd.randrange(1 << 20)) if len(g) >= 3]
    crown_c, crown_r = ellipsoid(anchors, med_d * .3, 2.0)
    # branch points inside the canopy (spray roots): pack bark vertices above 30% of the height
    M0 = Mesh(); nb = bark_mesh(lod0, s, M0)
    bark_pts = [Vector(v) for v in M0.v if v[2] > z0 * s + .3 * H]
    kd = KDTree(max(1, len(bark_pts)))
    for i, p in enumerate(bark_pts): kd.insert(p, i)
    kd.balance()
    # sprays per clump by surface area (budget: LEAF_TRIS / ~19 tris per spray)
    area = [r.x * r.y + r.y * r.z + r.x * r.z for _, r in clumps]
    n_total = LEAF_TRIS * BUDGET.get(key, 1.0) / 19.0
    cards = []
    for ci, ((c, r), a) in enumerate(zip(clumps, area)):
        ns = max(24, int(n_total * a / sum(area)))
        nseed = Vector((rnd.uniform(0, 50), rnd.uniform(0, 50), rnd.uniform(0, 50)))
        for i in range(ns):
            u = (fib(ns, i) + Vector((rnd.gauss(0, .12), rnd.gauss(0, .12), rnd.gauss(0, .12)))).normalized()
            if noise.noise(u * 1.8 + nseed) > .3: continue                   # small irregular gaps in the silhouette
            tip = c + Vector((u.x * r.x, u.y * r.y, u.z * r.z)) * rnd.uniform(.75, 1.05)
            inner = c + Vector((u.x * r.x, u.y * r.y, u.z * r.z)) * .3
            base = inner
            if bark_pts:
                bp, _, dist = kd.find(inner)
                if dist < max(r) * .9: base = bp                              # rooted on the branch inside the clump
            d = tip - base
            if d.length < .4: d = u * .8; tip = base + d
            L = d.length; dv = d.normalized()
            n_c = max(2, int(L / .5))
            for j in range(n_c):
                t = .4 + .65 * j / max(1, n_c - 1)                             # cards over the outer part of the spray
                side = dv.cross(UP); side = side.normalized() if side.length > 1e-3 else Vector((1, 0, 0))
                p = base + d * t + side * rnd.uniform(-.25, .25) + UP * rnd.uniform(-.2, .2) - UP * .12 * t * t * L * .3
                n_e = e_normal(p, c, r)
                # tilt: mostly out of the clump, a light pull up only on the top half, wide random spread (a uniform
                # up-pull made horizontal bands on the lower clumps - iter 1)
                facing = (n_e * .7 + UP * (.22 * max(0.0, n_e.z)) + Vector((rnd.gauss(0, .42), rnd.gauss(0, .42), rnd.gauss(0, .3)))).normalized()
                tipd = (dv + Vector((rnd.gauss(0, .25), rnd.gauss(0, .25), rnd.gauss(0, .15))) - UP * .15 * t).normalized()
                sz = rnd.uniform(*CARD) * (1.08 - .2 * t)
                key_, cs = rnd.random(), rnd.randrange(1 << 30)
                cards.append((key_, cs, p, facing, tipd, sz, ci))
                if rnd.random() < .35:                                         # crossed partner: a face from every view
                    f2 = tipd.cross(facing).normalized()
                    cards.append((key_, rnd.randrange(1 << 30), p, (f2 + facing * .3).normalized(), tipd, sz * .9, ci))
    def shade(ci):
        c, r = clumps[ci]
        def vn(q): return (e_normal(q, c, r) * .72 + e_normal(q, crown_c, crown_r) * .18 + UP * .1).normalized()
        def vc(q):
            n = e_normal(q, c, r); dep = min(1.1, e_depth(q, c, r))
            return hero_ramp(.5 + .4 * n.z + .35 * (dep - .8))                 # darker inside + underneath
        return vc, vn
    shaders = [shade(i) for i in range(len(clumps))]
    lods = []
    for lod in range(3):
        if lod == 0: M = M0
        else:
            src = next((obs[i] for i in (lod, 1) if i < len(obs) and any(not is_leaf(m) for m in obs[i].data.materials)), lod0)
            M = Mesh()
            bark_mesh(src, s, M)
        for key_, cs, p, facing, tipd, sz, ci in cards:
            if key_ > LOD_KEEP[lod]: continue
            vc, vn = shaders[ci]
            bunch_card(M, random.Random(cs), p, facing, sz * LOD_GROW[lod], vc, vn, mat=1, fold_deg=(20, 38), tip_dir=tipd, full_uv=True)
        ob = M.build(name if lod == 0 else f'{name}_LOD{lod}', ('Bark', 'Leaves'), (True, True))
        lods.append(ob)
    low = [Vector(v) for v in M0.v[:sum(1 for _ in M0.v)] if v[2] < z0 * s + .06 * H]
    mid = [Vector(v) for v in M0.v if z0 * s + .3 * H < v[2] < z0 * s + .4 * H] or low
    bc = sum(low, Vector()) / max(1, len(low)) if low else Vector((0, 0, z0 * s))
    tc = sum(mid, Vector()) / max(1, len(mid))
    rr = max(.15, sum(math.hypot(p.x - bc.x, p.y - bc.y) for p in low) / max(1, len(low)))
    if ucx:                                                            # keep the tree's own collision hulls
        for i, o in enumerate(ucx): o.name = f'UCX_{name}_{i:02d}'
        hulls = ucx
    else:
        hulls = [trunk_hull(name, Vector((bc.x, bc.y, z0 * s)), Vector((tc.x, tc.y, tc.z)), rr)]
    for o, fn, ext in ((lods[0], name, hulls), (lods[1], f'{name}_LOD1', []), (lods[2], f'{name}_LOD2', [])):
        bpy.ops.object.select_all(action='DESELECT')
        for x in [o] + ext: x.select_set(True)
        bpy.context.view_layer.objects.active = o
        bpy.ops.export_scene.fbx(filepath=f'{OUT}/{fn}.fbx', use_selection=True, axis_forward='-Y', axis_up='Z',
                                 mesh_smooth_type='FACE', colors_type='LINEAR')
    tris = [sum(len(p.vertices) - 2 for p in o.data.polygons) for o in lods]
    man_p = OUT + '/manifest.json'; man = json.load(open(man_p)) if os.path.exists(man_p) else {}
    man[name] = dict(height_m=round(H, 2), dims=[round(v, 2) for v in lods[0].dimensions], triangles=tris,
                     clumps=len(clumps), cards=len(cards), pack_leaf_quads=len(isl), bark_faces=nb)
    json.dump(man, open(man_p, 'w'), indent=1)
    print('CANOPY', name, man[name])

for key, fbx, name, H in JOBS:
    if ARGS and key not in ARGS and name not in ARGS: continue
    if not os.path.exists(fbx): print('CANOPY_PROBLEM missing', fbx); continue
    try: grow(key, fbx, name, H)
    except Exception as ex:
        import traceback; print('CANOPY_PROBLEM', name, ex); traceback.print_exc()
print('CANOPY_DONE')
