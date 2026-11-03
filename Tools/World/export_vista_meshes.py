"""Blender: meshes for the stylized-world vista (L_WorldVista). Original shapes, built to the golden-hour fantasy-forest
reference (Dreamscape: Tower video) as a STYLE target only (no floating chained rocks, no tower - per the user).
  blender -b --python export_vista_meshes.py
Writes FBX to OUT plus vista_layout.json (path, lake, ponds, rivers with water heights), all in Blender metres (the
Unreal FBX importer mirrors Y; the Unreal builder flips Y for the JSON points to match).
R02: rivers + ponds carved into the terrain, a far hills/mountain ring that continues the terrain edge, stacked-ledge
cliffs with hanging ivy, round fluffy trees (leaf cards + dark fill, spherical normals), far trees, pollen + drifting leaves."""
import bpy, bmesh, math, json, random
from mathutils import Vector, noise

OUT = 'C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs/World_R01/exports'
import os; os.makedirs(OUT, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
rnd = random.Random(7)

def export(ob, name, normals=False):
    """ob may be (mesh, [UCX_ collision objects]); custom normals are written either way (normals=True just documents it)."""
    ob, extra = ob if isinstance(ob, tuple) else (ob, [])
    bpy.ops.object.select_all(action='DESELECT')
    for o in [ob] + extra: o.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.fbx(filepath=f'{OUT}/{name}.fbx', use_selection=True, axis_forward='-Y', axis_up='Z',
                             mesh_smooth_type='FACE', colors_type='LINEAR')
    print('EXPORTED', name, len(ob.data.polygons))

def obj(name, bm, mats=()):
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    for m in mats:
        me.materials.append(bpy.data.materials.get(m) or bpy.data.materials.new(m))
    ob = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(ob); return ob

def smoothstep(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a))); return t * t * (3 - 2 * t)

# ------------------------------------------------------------------ layout
S = 200.0                              # vista square (m)
PATH = [(-6, -100), (-2, -70), (6, -45), (2, -22), (-8, 0), (-4, 22), (8, 42), (14, 70), (10, 100)]
LAKE = (42.0, -18.0, 17.0)             # centre x, y, radius (m)
# ponds (centre, radius m) - the last one is the deep swimming-hole pool with a jump ledge on its north rim
PONDS = [(74.0, 46.0, 5.0), (-85.0, 44.0, 7.0), (28.0, 50.0, 6.0), (-30.0, -86.0, 5.0), (-45.0, -25.0, 13.0)]
POND_DEPTH = [2.2, 2.2, 2.2, 2.2, 5.0]   # bed depth below the waterline at the centre = 0.2 + this (m): head-under everywhere
LEDGE = (-45.0, -10.5, 2.6)              # jump ledge: centre x, y and height (m) - walkable back slope, sheer drop into the pool
CLIFF_Y = 62.0                         # cliff ridge north of the vista
# rivers, downstream order (control points, m) + width (m) + depth (m); A: spring pond -> lake, B: cliff-foot spring -> west pond
RIVERS = [dict(name='River_A', pts=[(72, 42), (68, 33), (63, 24), (59, 14), (53, 5), (47, -4)], width=4.5, depth=0.8),
          dict(name='River_B', pts=[(-55, 57), (-60, 53), (-66, 50), (-72, 47), (-79, 45)], width=3.4, depth=0.6)]

def seg_closest(p, a, b):
    ab = (b[0] - a[0], b[1] - a[1]); ap = (p[0] - a[0], p[1] - a[1]); L = ab[0] ** 2 + ab[1] ** 2
    t = max(0.0, min(1.0, (ap[0] * ab[0] + ap[1] * ab[1]) / L)) if L else 0.0
    return math.hypot(p[0] - a[0] - t * ab[0], p[1] - a[1] - t * ab[1]), t

def path_dist(x, y):
    return min(seg_closest((x, y), PATH[i], PATH[i + 1])[0] for i in range(len(PATH) - 1))

def base_height(x, y):
    n = noise.noise(Vector((x / 45.0, y / 45.0, 0.3))) * 4.5 + noise.noise(Vector((x / 14.0, y / 14.0, 2.1))) * 0.9
    r = (y - CLIFF_Y - 6.0 * math.sin(x / 17.0)) / 9.0                       # cliff ridge to the north, rising steeply
    n += 22.0 / (1.0 + math.exp(-r * 2.2)) * (0.7 + 0.3 * math.sin(x / 31.0 + 1.0))
    for cx, cy, cr, dep in [LAKE + (3.2,)] + [p + (1.7,) for p in PONDS]:     # lake + pond hollows
        d = math.hypot(x - cx, y - cy) / cr
        if d < 1.6: n -= dep * max(0.0, 1 - d * d / 1.6 ** 2) ** 1.4
    lx_, ly_, lh_ = LEDGE                                                    # grassy rise on the deep pool's rim (its pool side is cut away)
    n += lh_ * math.exp(-((x - lx_) / 7.0) ** 2 - ((y - ly_) / 4.5) ** 2)
    pd = path_dist(x, y)                                                     # flatter along the path
    if pd < 6.0: n = n * (0.6 + 0.4 * pd / 6.0) + noise.noise(Vector((x / 9.0, y / 9.0, 4.0))) * 0.15
    return n

def water_level(cx, cy, cr):
    return min(base_height(cx + math.cos(a) * cr * 1.05, cy + math.sin(a) * cr * 1.05) for a in [i * math.pi / 12 for i in range(24)]) - 0.25

LAKE_Z = water_level(*LAKE)
POND_Z = [water_level(*p) for p in PONDS]

def catmull(pts, step=1.0):
    out = []
    P = [pts[0]] + list(pts) + [pts[-1]]
    for i in range(1, len(P) - 2):
        p0, p1, p2, p3 = (Vector(q) for q in (P[i - 1], P[i], P[i + 1], P[i + 2]))
        n = max(2, int((p2 - p1).length / step))
        for k in range(n):
            t = k / n
            out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t ** 3))
    out.append(Vector(pts[-1])); return [(v.x, v.y) for v in out]

# river water surface: follows the ground downhill (running minimum, a little below it), ends at the lake / pond level
for rv, end_z in zip(RIVERS, (LAKE_Z, POND_Z[1])):
    dense = catmull(rv['pts'])
    z, surf = 1e9, []
    for x, y in dense:
        z = min(z, base_height(x, y) - 0.35); surf.append(max(z, end_z))
    n = len(surf)                                     # last quarter eases down to meet the lake / pond surface exactly
    surf = [s_ + (end_z - s_) * smoothstep(0.7, 1.0, i / (n - 1)) for i, s_ in enumerate(surf)]
    rv['dense'], rv['surf'] = dense, surf

WATER_DISCS = [(LAKE[0], LAKE[1], LAKE[2] * 1.25)] + [(p[0], p[1], p[2] * 1.25) for p in PONDS]

def river_info(x, y):
    """(distance to the nearest river centreline, its water height there, half width, depth) or None."""
    best = None
    for rv in RIVERS:
        d_ = rv['dense']
        for i in range(len(d_) - 1):
            if abs(x - d_[i][0]) > 12 or abs(y - d_[i][1]) > 12: continue
            dist, t = seg_closest((x, y), d_[i], d_[i + 1])
            if best is None or dist < best[0]:
                best = (dist, rv['surf'][i] + (rv['surf'][i + 1] - rv['surf'][i]) * t, rv['width'] / 2, rv['depth'])
    return best

def height(x, y):
    """final terrain: base + lake / pond beds (always below their water out to the shore) + river channels."""
    h = base_height(x, y)
    for (cx, cy, cr), wz, dep in zip([LAKE[:3]] + [p[:3] for p in PONDS], [LAKE_Z] + POND_Z, [2.0] + POND_DEPTH):
        d = math.hypot(x - cx, y - cy) / cr
        if d < 1.0: h = min(h, wz - 0.2 - dep * (1 - d * d))
    ri = river_info(x, y)
    if ri:
        d, surf, half, dep = ri
        if d < half: h = min(h, surf - 0.05 - dep * (1 - (d / half) ** 2))
        elif d < half + 3.0 and not any(math.hypot(x - cx, y - cy) < cr for cx, cy, cr in WATER_DISCS):
            w = 1 - smoothstep(half, half + 3.0, d)
            h = h + (surf + 0.12 + (d - half) * 0.35 - h) * w
    return h

def wetness(x, y, h):
    """vertex G: river bed / shoreline (wet earth in the terrain material)."""
    ri = river_info(x, y)
    g = 0.0
    if ri: g = 1 - smoothstep(ri[2] * 0.7, ri[2] + 1.2, ri[0])
    for (cx, cy, cr), wz in zip(WATER_DISCS, [LAKE_Z] + POND_Z):
        if math.hypot(x - cx, y - cy) < cr * 1.3: g = max(g, 1 - smoothstep(wz + 0.05, wz + 0.45, h))
    return g

def terrain():
    N = 250
    bm = bmesh.new(); col = bm.verts.layers.float_color.new('Col'); g = []
    for j in range(N + 1):
        row = []
        for i in range(N + 1):
            x = -S / 2 + S * i / N; y = -S / 2 + S * j / N
            h = height(x, y)
            v = bm.verts.new((x, y, h))
            v[col] = (max(0.0, min(1.0, (2.0 - path_dist(x, y)) / 0.9)), wetness(x, y, h), 0, 1)   # R = dirt path, G = wet
            row.append(v)
        g.append(row)
    for j in range(N):
        for i in range(N): bm.faces.new((g[j][i], g[j][i + 1], g[j + 1][i + 1], g[j + 1][i]))
    uv = bm.loops.layers.uv.new('UVMap')
    for f in bm.faces:
        for l in f.loops: l[uv].uv = (l.vert.co.x / 2, l.vert.co.y / 2)
    ob = obj('SM_Vista_Terrain', bm)
    for p in ob.data.polygons: p.use_smooth = True
    return ob

def far_land():
    """Ring around the vista: starts at the terrain edge height, rolls into hills, then ridged mountains far out."""
    bm = bmesh.new(); col = bm.verts.layers.float_color.new('Col')
    NA, NR, R0, R1 = 384, 64, 92.0, 2600.0
    rings = []
    for k in range(NR + 1):
        r = R0 * (R1 / R0) ** (k / NR); row = []
        for a_ in range(NA):
            a = 2 * math.pi * a_ / NA; x, y = math.cos(a) * r, math.sin(a) * r
            ds = max(abs(x), abs(y)) - S / 2
            ex, ey = max(-S / 2, min(S / 2, x)), max(-S / 2, min(S / 2, y))
            edge = height(ex, ey)
            hills = 9 + 26 * noise.noise(Vector((x / 230.0, y / 230.0, 5.0))) + 8 * noise.noise(Vector((x / 70.0, y / 70.0, 6.0)))
            ridge = (1 - abs(noise.noise(Vector((x / 620.0, y / 620.0, 9.0))))) ** 2 * 230 + (1 - abs(noise.noise(Vector((x / 210.0, y / 210.0, 3.0))))) ** 2 * 70
            if ds < 0: h = edge - 9.0                                       # hidden skirt: well below pond beds
            else: h = edge + (max(hills, 2.0) - edge) * smoothstep(0, 90, ds) + ridge * smoothstep(600, 1500, r)   # low distant ranges: the setting sun stays above them
            v = bm.verts.new((x, y, h)); v[col] = (0, 0, 0, 1); row.append(v)
        rings.append(row)
    for k in range(NR):
        for a_ in range(NA):
            b_ = (a_ + 1) % NA
            bm.faces.new((rings[k][a_], rings[k + 1][a_], rings[k + 1][b_], rings[k][b_]))   # counter-clockwise from above (faces up)
    uv = bm.loops.layers.uv.new('UVMap')
    for f in bm.faces:
        for l in f.loops: l[uv].uv = (l.vert.co.x / 2, l.vert.co.y / 2)
    ob = obj('SM_Vista_Far', bm)
    for p in ob.data.polygons: p.use_smooth = True
    return ob

def blade(bm, col, base, ang, lean, h, w, bend, segs=3):
    d = Vector((math.cos(ang), math.sin(ang), 0)); side = Vector((-d.y, d.x, 0)); rows = []
    for s in range(segs + 1):
        t = s / segs; a = math.radians(lean + bend * t * t)
        p = base + d * (math.sin(a) * h * t) + Vector((0, 0, math.cos(a) * h * t))
        ww = w * (1 - t) ** 0.8
        if s == segs: v = bm.verts.new(p); v[col] = (t, 0, 0, 1); rows.append([v]); break
        a_ = bm.verts.new(p - side * ww / 2); b_ = bm.verts.new(p + side * ww / 2)
        a_[col] = (t, 0, 0, 1); b_[col] = (t, 0, 0, 1); rows.append([a_, b_])
    for s in range(segs - 1): bm.faces.new((rows[s][0], rows[s][1], rows[s + 1][1], rows[s + 1][0]))
    bm.faces.new((rows[segs - 1][0], rows[segs - 1][1], rows[segs][0]))

def grass_clump(name, blades, hmin, hmax, wid, spread):
    bm = bmesh.new(); col = bm.verts.layers.float_color.new('Col'); r = random.Random(hash(name) & 0xffff)
    for k in range(blades):
        a = r.uniform(0, 2 * math.pi); rr = spread * math.sqrt(r.random())
        blade(bm, col, Vector((math.cos(a) * rr, math.sin(a) * rr, -0.02)), a + r.uniform(-.6, .6), r.uniform(4, 28),
              r.uniform(hmin, hmax), wid * r.uniform(.8, 1.2), r.uniform(10, 45))
    uv = bm.loops.layers.uv.new('UVMap')
    for f in bm.faces:
        for l in f.loops: l[uv].uv = (0.5, l.vert[col][0])
    return obj(name, bm)

def flowers(name):
    """Three poppy-like flowers: thin stems (G=0) + 5-petal heads (G=1) with a dark centre (G=0.5)."""
    bm = bmesh.new(); col = bm.verts.layers.float_color.new('Col'); r = random.Random(3)
    for k in range(3):
        base = Vector((r.uniform(-.15, .15), r.uniform(-.15, .15), -0.02)); h = r.uniform(.38, .6)
        top = base + Vector((r.uniform(-.06, .06), r.uniform(-.06, .06), h))
        s0 = [bm.verts.new(base + Vector((dx, dy, 0))) for dx, dy in ((.008, 0), (-.004, .007), (-.004, -.007))]
        s1 = [bm.verts.new(top + Vector((dx, dy, 0))) for dx, dy in ((.006, 0), (-.003, .005), (-.003, -.005))]
        for v in s0 + s1: v[col] = ((v.co.z - base.z) / h, 0, 0, 1)
        for i in range(3): bm.faces.new((s0[i], s0[(i + 1) % 3], s1[(i + 1) % 3], s1[i]))
        c = bm.verts.new(top + Vector((0, 0, .015))); c[col] = (1, .5, 0, 1)
        for p in range(5):
            a0 = 2 * math.pi * p / 5 + k; a1 = a0 + 2 * math.pi / 5 * .9
            e0 = bm.verts.new(top + Vector((math.cos(a0) * .07, math.sin(a0) * .07, .03)))
            e1 = bm.verts.new(top + Vector((math.cos(a1) * .07, math.sin(a1) * .07, .03)))
            m = bm.verts.new(top + Vector((math.cos((a0 + a1) / 2) * .085, math.sin((a0 + a1) / 2) * .085, .045)))
            for v in (e0, e1, m): v[col] = (1, 1, 0, 1)
            bm.faces.new((c, e0, m)); bm.faces.new((c, m, e1))
    uv = bm.loops.layers.uv.new('UVMap')
    for f in bm.faces:
        for l in f.loops: l[uv].uv = (l.vert.co.x * 4 + .5, l.vert.co.z * 2 + l.vert.co.y)
    return obj(name, bm)

def boulder(name, size, seed, subdiv=3, flat=.3):
    bm = bmesh.new(); bmesh.ops.create_icosphere(bm, subdivisions=subdiv, radius=1.0)
    off = Vector((seed * 3.1, seed * 1.7, seed * .9))
    for v in bm.verts:
        p = v.co.copy(); n = 1 + .22 * noise.noise(p * 1.3 + off) + .08 * noise.noise(p * 3.0 + off)
        v.co = Vector((p.x * size[0] / 2 * n, p.y * size[1] / 2 * n, p.z * size[2] / 2 * n))
        if v.co.z < -size[2] / 2 * flat: v.co.z = -size[2] / 2 * flat - .05 * noise.noise(p * 2 + off)   # flat, sunk base
    zmin = min(v.co.z for v in bm.verts)
    for v in bm.verts: v.co.z -= zmin + size[2] * .12                                                  # pivot near ground, sunk 12%
    uv = bm.loops.layers.uv.new('UVMap')
    for f in bm.faces:
        for l in f.loops: l[uv].uv = (l.vert.co.x / 3, (l.vert.co.y + l.vert.co.z) / 3)
    return obj(name, bm)

# ------------------------------------------------------------------ leaves (shared by trees + ivy)
NKEYS = ('nx', 'ny', 'nz', 'hn')
def set_n(bm, v, n):
    """store a shading normal on the vertex (float layers; BMVert refs do not survive later bmesh ops)."""
    for k, val in zip(NKEYS, (n.x, n.y, n.z, 1.0)): v[bm.verts.layers.float.get(k)] = val

def apply_normals(ob, flat_rest=False):
    """custom normals: stored ones where set, else the vertex normal (or the face normal when flat_rest)."""
    me = ob.data
    A = [me.attributes[k].data for k in NKEYS]
    has = [A[3][i].value > .5 for i in range(len(me.vertices))]
    for p in me.polygons: p.use_smooth = (not flat_rest) or any(has[v] for v in p.vertices)
    me.update()
    loop_n = []
    for p in me.polygons:
        for li in p.loop_indices:
            vi = me.loops[li].vertex_index
            loop_n.append(Vector((A[0][vi].value, A[1][vi].value, A[2][vi].value)) if has[vi] else
                          (p.normal.copy() if flat_rest else me.vertices[vi].normal.copy()))
    me.normals_split_custom_set(loop_n)
    for k in NKEYS: me.attributes.remove(me.attributes[k])

# the Blender trees' leaf: Trees_R01/source/trees_build.py leaf_card (folded card, 2x2 maple atlas T_Leaf_Maple_Atlas,
# vertex colour from LEAF_RAMP) - the round trees and the cliff ivy share it and its material (M_Vista_Leaf)
LEAF_RAMP = [(0, (24, 56, 48)), (.30, (38, 92, 58)), (.52, (96, 148, 42)), (.74, (182, 210, 38)), (1, (238, 242, 62))]
def lin1(v): v /= 255; return v / 12.92 if v <= .04045 else ((v + .055) / 1.055) ** 2.4
def leaf_tint(t):
    t = max(0.0, min(1.0, t))
    for (a, ca), (b, cb) in zip(LEAF_RAMP, LEAF_RAMP[1:]):
        if t <= b:
            k = (t - a) / (b - a); return tuple(lin1(ca[c] + (cb[c] - ca[c]) * k) for c in range(3)) + (1,)
    return tuple(lin1(c) for c in LEAF_RAMP[-1][1]) + (1,)

def leaf(bm, col, p, n, size, light, tint, normal):
    """one folded maple leaf card at p facing n (material 1, atlas UVs); light -> leaf ramp colour; normal = shading normal."""
    tv = Vector((rnd.gauss(0, 1), rnd.gauss(0, 1), rnd.gauss(0, 1))) * .45 - Vector((0, 0, .45)) + n.cross(Vector((0, 0, 1))) * .6
    tv = tv - n * tv.dot(n)
    if tv.length < 1e-4: tv = n.orthogonal()
    tip = tv.normalized(); side = tip.cross(n).normalized(); fold = math.radians(rnd.uniform(14, 30)); L = size
    sl = -side * math.cos(fold) + n * math.sin(fold); sr = side * math.cos(fold) + n * math.sin(fold)
    b0 = p - tip * .15 * L; t0 = p + tip * .85 * L; w = .5 * L
    vs = [bm.verts.new(c) for c in (b0, t0, t0 + sl * w, b0 + sl * w, b0 + sr * w, t0 + sr * w)]
    c_ = leaf_tint(light)
    for v in vs:
        v[col] = c_; set_n(bm, v, normal)
    cell = rnd.randrange(4); u0 = (cell % 2) * .5; v0 = .5 if cell < 2 else 0.0
    um, ul, ur, vb, vt = u0 + .25, u0, u0 + .5, v0, v0 + .5
    uvl = bm.loops.layers.uv.get('UVMap')
    for idx, uvs in (((0, 1, 2, 3), ((um, vb), (um, vt), (ul, vt), (ul, vb))), ((0, 4, 5, 1), ((um, vb), (ur, vb), (ur, vt), (um, vt)))):
        f = bm.faces.new([vs[i] for i in idx]); f.material_index = 1
        for l, uv in zip(f.loops, uvs): l[uvl].uv = uv
    return []

def trunk_hull(name, base, top, r):
    """UCX_ convex collision around the trunk (Unreal imports it as the mesh's simple collision)."""
    bm = bmesh.new()
    pts = [bm.verts.new(c + Vector((math.cos(a) * rr, math.sin(a) * rr, 0))) for c, rr in ((base, r * 1.3), (top, r * .8))
           for a in [2 * math.pi * i / 8 for i in range(8)]]
    bmesh.ops.convex_hull(bm, input=pts)
    return obj(f'UCX_{name}_00', bm)

def tube(bm, col, a, b, r0, r1, sides=8, flare=0.0):
    """tapered bark cylinder a->b (material 0); flare widens the first ring (root flare)."""
    d = (b - a); axis = d.normalized()
    t = axis.cross(Vector((0, 0, 1)) if abs(axis.z) < .9 else Vector((1, 0, 0))).normalized(); s = axis.cross(t)
    rings = []
    for k, (p, r) in enumerate(((a, r0 * (1 + flare)), (a.lerp(b, .5), (r0 + r1) / 2), (b, r1))):
        ring = []
        for i in range(sides):
            ang = 2 * math.pi * i / sides
            v = bm.verts.new(p + (t * math.cos(ang) + s * math.sin(ang)) * r * (1 + (.25 * noise.noise(p + Vector((ang, 0, 0))) if flare and k == 0 else 0)))
            v[col] = (0, 1, .5, 1); ring.append(v)
        rings.append(ring)
    for k in range(2):
        for i in range(sides):
            f = bm.faces.new((rings[k][i], rings[k][(i + 1) % sides], rings[k + 1][(i + 1) % sides], rings[k + 1][i])); f.material_index = 0

def fluffy_tree(name, seed, trunk_h, canopy_r, clumps, lean=0.4, leaf_size=.5, density=210):
    """Round stylized tree: bark trunk + branches to 1 + `clumps` canopy domes; each dome = dark noisy fill blob +
    outward leaf cards. Shading normals are bent outward from the canopy centre so the crown reads soft and round."""
    global rnd
    rnd = random.Random(seed)
    bm = bmesh.new(); col = bm.verts.layers.float_color.new('Col'); nrm = []; bm.loops.layers.uv.new('UVMap')
    for k in NKEYS: bm.verts.layers.float.new(k)
    top = Vector((rnd.uniform(-lean, lean), rnd.uniform(-lean, lean), trunk_h))
    mid = Vector((top.x * .35 + rnd.uniform(-.2, .2), top.y * .35 + rnd.uniform(-.2, .2), trunk_h * .5))
    tube(bm, col, Vector((0, 0, -.4)), mid, .42 * trunk_h / 6, .3 * trunk_h / 6, 10, flare=.7)
    tube(bm, col, mid, top, .3 * trunk_h / 6, .2 * trunk_h / 6, 10)
    C = top + Vector((0, 0, canopy_r * .4))
    domes = [(C + Vector((0, 0, canopy_r * .1)), canopy_r * .6)]
    # big clumps spread wide and low (crown wider than tall), then small clumps breaking up the outline
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
    for di, (c, r) in enumerate(domes):
        tint = rnd.random()
        # dark fill blob hides the gaps between cards
        fill = [v for v in bmesh.ops.create_icosphere(bm, subdivisions=2, radius=r * .82)['verts'] if v.is_valid]
        for v in fill:
            u = v.co.normalized(); v.co = c + u * r * .82 * (1 + .12 * noise.noise(u * 2.5 + Vector((seed, di, 0))))
            v[col] = (1, .12 + .18 * smoothstep(zlo, zhi, v.co.z), tint, 1)
            set_n(bm, v, ((v.co - C).normalized() * .6 + u * .4).normalized())
        for f in {f for v in fill for f in v.link_faces}: f.material_index = 2
        for k in range(int(density * r * r)):
            u = Vector((rnd.gauss(0, 1), rnd.gauss(0, 1), rnd.gauss(0, 1))).normalized()
            if u.z < -.5 and rnd.random() < .6: continue                       # fewer cards underneath
            p = c + u * r * rnd.uniform(.86, 1.08)
            if any((p - c2).length < r2 * .8 for c2, r2 in domes if c2 is not c): continue   # buried inside a neighbour
            up = smoothstep(zlo, zhi, p.z)
            light = max(0.0, min(1.0, .3 + .45 * up + .35 * (u.z * .5 + .5) + rnd.uniform(-.12, .12)))
            n = (u + Vector((rnd.uniform(-.5, .5), rnd.uniform(-.5, .5), rnd.uniform(-.2, .6)))).normalized()
            shade_n = ((p - C).normalized() * .55 + u * .45).normalized()
            nrm += leaf(bm, col, p, n, leaf_size * rnd.uniform(.8, 1.2), light, tint, shade_n)
    uv = bm.loops.layers.uv.get('UVMap')
    for f in bm.faces:
        if f.material_index == 1: continue                                   # leaf cards keep their atlas UVs
        for l in f.loops:
            co = l.vert.co; l[uv].uv = ((math.atan2(co.y, co.x) / math.pi + 1) * 1.0, co.z / 2.0)
    ob = obj(name, bm, ('Bark', 'Leaves', 'Fill'))
    apply_normals(ob)
    return ob, [trunk_hull(name, Vector((0, 0, -.3)), mid.lerp(top, .5), .42 * trunk_h / 6)]

def far_tree(name):
    """Distant hillside tree: three soft smooth-shaded crown lumps (spherical normals) + short trunk - cheap enough for
    thousands of instances, reads as the round trees at a distance."""
    bm = bmesh.new(); col = bm.verts.layers.float_color.new('Col')
    for k in NKEYS: bm.verts.layers.float.new(k)
    tube(bm, col, Vector((0, 0, -.5)), Vector((0, 0, 4.5)), .4, .25, 6)
    C = Vector((0, 0, 7.0))
    for i, (off, r) in enumerate(((Vector((0, 0, .6)), 3.4), (Vector((2.2, .8, -.4)), 2.6), (Vector((-1.8, -1.6, -.2)), 2.7))):
        crown = [v for v in bmesh.ops.create_icosphere(bm, subdivisions=2, radius=1.0)['verts'] if v.is_valid]
        for v in crown:
            u = v.co.normalized(); v.co = C + off + u * r * (1 + .1 * noise.noise(u * 3 + Vector((i, 0, 0))))
            v[col] = (1, .22 + .38 * smoothstep(3.5, 11.0, v.co.z), .3 * i, 1)
            set_n(bm, v, ((v.co - C).normalized() * .6 + u * .4).normalized())
        for f in {f for v in crown for f in v.link_faces}: f.material_index = 1
    uv = bm.loops.layers.uv.new('UVMap')
    for f in bm.faces:
        for l in f.loops: l[uv].uv = (l.vert.co.x / 2, l.vert.co.z / 2)
    ob = obj(name, bm, ('Bark', 'Leaves'))
    apply_normals(ob)
    return ob

def cliff(name, seed, layers=4):
    """Stylized stacked-ledge cliff: irregular flat-topped slabs shrinking upward (material 0) with ivy curtains hanging
    from the ledge edges (leaf cards, material 1)."""
    global rnd
    rnd = random.Random(seed)
    bm = bmesh.new(); col = bm.verts.layers.float_color.new('Col'); nrm = []; bm.loops.layers.uv.new('UVMap')
    for k in NKEYS: bm.verts.layers.float.new(k)
    z = -1.6; rx, ry = 8.0, 5.0; ox, oy = 0.0, 0.0
    for L in range(layers):
        h = rnd.uniform(2.4, 3.6); npts = rnd.randint(9, 13)
        base = [(2 * math.pi * i / npts + rnd.uniform(-.15, .15)) for i in range(npts)]
        rad = [rnd.uniform(.82, 1.12) for _ in base]
        def ring(zz, s, push=0.0):
            out = []
            for a, rr in zip(base, rad):
                v = bm.verts.new((ox + math.cos(a) * rx * rr * s + push * math.cos(a), oy + math.sin(a) * ry * rr * s + push * math.sin(a), zz))
                v[col] = (0, 1, .5, 1); out.append(v)
            return out
        r0 = ring(z, 1.0); r1 = ring(z + h * .5, .98, rnd.uniform(.1, .45)); r2 = ring(z + h, .9)
        for ra, rb in ((r0, r1), (r1, r2)):
            for i in range(npts): bm.faces.new((ra[i], ra[(i + 1) % npts], rb[(i + 1) % npts], rb[i])).material_index = 0
        cap = bm.faces.new(r2); cap.material_index = 0
        # ivy curtains from this ledge's top edge
        for i in range(npts):
            if rnd.random() > .45: continue
            a = base[i] + rnd.uniform(0, 2 * math.pi / npts); rr = rad[i]
            top = Vector((ox + math.cos(a) * rx * rr * .9, oy + math.sin(a) * ry * rr * .9, z + h))
            out = Vector((math.cos(a) * ry, math.sin(a) * rx, 0)).normalized()
            for strand in range(rnd.randint(2, 4)):
                sx = out.cross(Vector((0, 0, 1))) * rnd.uniform(-.8, .8)
                length = rnd.uniform(1.2, h * .95); s_ = 0.0
                while s_ < length:
                    p = top + sx + out * (.25 + .08 * math.sin(s_ * 3)) + Vector((0, 0, -s_))
                    light = .35 + .5 * (1 - s_ / length) + rnd.uniform(-.1, .1)
                    nrm += leaf(bm, col, p + out * rnd.uniform(0, .1), (out + Vector((rnd.uniform(-.6, .6), rnd.uniform(-.6, .6), .3))).normalized(),
                                rnd.uniform(.3, .42), light * .8, 0, (out + Vector((0, 0, .3))).normalized())
                    s_ += rnd.uniform(.12, .2)
        z += h - .15; rx *= rnd.uniform(.72, .86); ry *= rnd.uniform(.72, .86); ox += rnd.uniform(-1, 1); oy += rnd.uniform(-.8, .8)
    bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 4])
    uv = bm.loops.layers.uv.get('UVMap')
    for f in bm.faces:
        if f.material_index == 1: continue
        for l in f.loops: l[uv].uv = (l.vert.co.x / 3, (l.vert.co.y + l.vert.co.z) / 3)
    ob = obj(name, bm, ('Rock', 'Leaves'))
    apply_normals(ob, flat_rest=True)   # rock stays faceted, leaves get their bent normals
    return ob

def drifters(name, count, size, box):
    """Tiny cards spread through a box (cm in Unreal = box m here); the material drifts and wraps them (vertex RGB = seeds).
    UV1 = the card's centre in Unreal local cm (Y mirrored like the FBX import, V flipped like the importer) so the
    material wraps each card as one piece."""
    r = random.Random(hash(name) & 0xffff)
    bm = bmesh.new(); col = bm.verts.layers.float_color.new('Col'); centres = {}
    for k in range(count):
        c = Vector((r.uniform(-box[0], box[0]), r.uniform(-box[1], box[1]), r.uniform(.3, box[2])))
        n = Vector((r.gauss(0, 1), r.gauss(0, 1), r.gauss(0, 1))).normalized()
        t = n.cross(Vector((0, 0, 1))).normalized() if abs(n.z) < .9 else Vector((1, 0, 0)); b = n.cross(t)
        seed = (r.random(), r.random(), r.random(), 1)
        vs = [bm.verts.new(c + (t * x + b * y) * size) for x, y in ((-.5, 0), (0, -.35), (.5, 0), (0, .35))]
        for v in vs: v[col] = seed
        centres[bm.faces.new(vs)] = c
    uv = bm.loops.layers.uv.new('UVMap'); uv1 = bm.loops.layers.uv.new('Centre')
    for f in bm.faces:
        for i, l in enumerate(f.loops):
            l[uv].uv = ((0, .5), (.5, 0), (1, .5), (.5, 1))[i]
            l[uv1].uv = (centres[f].x * 100, 1 + centres[f].y * 100)
    return obj(name, bm)

T = terrain(); export(T, 'SM_Vista_Terrain')
export(far_land(), 'SM_Vista_Far')
export(grass_clump('SM_Vista_Grass', 12, .32, .62, .06, .22), 'SM_Vista_Grass')
export(grass_clump('SM_Vista_GrassTall', 14, .6, 1.0, .07, .25), 'SM_Vista_GrassTall')
export(flowers('SM_Vista_Poppies'), 'SM_Vista_Poppies')
# boulders: the vista uses the Blender rock library (Rocks_R01 via Tools/World/import_rocks.py) instead
export(cliff('SM_Vista_Cliff_A', 31), 'SM_Vista_Cliff_A', normals=True)
export(cliff('SM_Vista_Cliff_B', 47, layers=3), 'SM_Vista_Cliff_B', normals=True)
export(fluffy_tree('SM_Vista_Tree_Round', 11, 5.5, 4.4, 6, leaf_size=.72, density=120), 'SM_Vista_Tree_Round', normals=True)
export(fluffy_tree('SM_Vista_Tree_Tall', 12, 7.5, 3.6, 5, lean=.3, leaf_size=.72, density=120), 'SM_Vista_Tree_Tall', normals=True)
export(fluffy_tree('SM_Vista_Tree_Wide', 13, 4.6, 5.4, 7, lean=.6, leaf_size=.78, density=100), 'SM_Vista_Tree_Wide', normals=True)
export(far_tree('SM_Vista_Tree_Far'), 'SM_Vista_Tree_Far')
export(drifters('SM_Vista_Motes', 140, .05, (15, 15, 5)), 'SM_Vista_Motes')
export(drifters('SM_Vista_DriftLeaves', 160, .16, (15, 15, 7)), 'SM_Vista_DriftLeaves')

def river_out(rv):
    """river spline points every ~4 m: x, y, water z (m) + width."""
    d, s = rv['dense'], rv['surf']; step = 4
    idx = list(range(0, len(d), step)) + ([len(d) - 1] if (len(d) - 1) % step else [])
    return dict(name=rv['name'], width=rv['width'], points=[[round(d[i][0], 2), round(d[i][1], 2), round(s[i], 3)] for i in idx])

json.dump(dict(path=PATH, lake=LAKE, lake_z=round(LAKE_Z, 3), cliff_y=CLIFF_Y,
               ponds=[[p[0], p[1], p[2], round(z, 3)] for p, z in zip(PONDS, POND_Z)],
               rivers=[river_out(rv) for rv in RIVERS],
               height_samples=[[x, y, round(height(x, y), 3)] for x, y in PATH]),
          open(f'{OUT}/vista_layout.json', 'w'), indent=1)
print('VISTA_EXPORTED')
