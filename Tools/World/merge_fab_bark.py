"""Blender: fuse a Fab tree's bark pieces into one skin with smooth collars at the joints, then unwrap it so the bark grain flows
along the trunk and out into each branch (user 2026-11-05: "merge the tree and add the smooth collars").
  blender -b --factory-startup -P Tools/World/merge_fab_bark.py -- SM_Lux_Tree_A
Why: the pack's bark is separate tubes pushed into each other (Lux A: 25) with UV jumps that cut crisp ridges.
  1 cap the open tube ends, voxel-remesh (a union); thin twigs may go, the user accepted that (leaves hide them)
  2 collars: smooth only near where two original pieces meet; 3 reduce to TRIS
  4 UV: U = distance along the surface from the tree's base / TILE (image x = grain), V = angle around the local limb x its
    girth / TILE, smoothed so there are no bands. V's wrap seam runs ALONG the grain, where it reads as one more groove.
Writes MERGED_DIR/<name>_bark.blend. Leaves, position, origin and collision are untouched; nothing is imported here."""
import bpy, bmesh, heapq, math, os, sys, numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree
OUTS = 'C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs'
FBX_DIR, MERGED_DIR = f'{OUTS}/FabPacks/_canopy_fbx', f'{OUTS}/FabPacks/_bark_merged'
VOXEL, COLLAR_REACH, COLLAR_PASSES = .03, .2, 60   # m, m from a joint, smoothing passes there
TRIS, TILE, BAND = 9000, 2.5, .35                  # target triangles; texture repeat, m; slice length for finding limbs, m
TRIS_LOD1, TRIS_LOD2 = 4000, 1500                  # bark triangles of the lower-detail versions
BARK_SLOT = 0; MIN_PART = 150                      # vertices; smaller voxel-skin parts are twig fragments
MIN_SLICE = 12                                     # vertices; smaller limb slices are specks

def adjacency(n, edges):
    a = edges.reshape(-1); count = np.bincount(a, minlength=n).astype(np.float32); return count
def smooth(co, edges, weight, passes):
    """Laplacian: each vertex moves towards its neighbours' average by weight"""
    n = len(co); cnt = adjacency(n, edges); cnt[cnt == 0] = 1
    for _ in range(passes):
        s = np.zeros_like(co); np.add.at(s, edges[:, 0], co[edges[:, 1]]); np.add.at(s, edges[:, 1], co[edges[:, 0]])
        co = co + (s / cnt[:, None] - co) * weight[:, None]
    return co
def get_co(me): c = np.empty(len(me.vertices) * 3, np.float32); me.vertices.foreach_get('co', c); return c.reshape(-1, 3)
def get_edges(me): e = np.empty(len(me.edges) * 2, np.int32); me.edges.foreach_get('vertices', e); return e.reshape(-1, 2)

def merged_bark(name):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=f'{FBX_DIR}/{name}.fbx')
    src = bpy.data.objects[name]; M = src.matrix_world
    # the bark faces alone, in world metres; each connected piece kept for the collar test
    bm = bmesh.new(); bm.from_mesh(src.data); bm.transform(M)
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.material_index != BARK_SLOT], context='FACES')
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=.001)
    bm.verts.ensure_lookup_table(); seen = set(); pieces = []
    for v0 in bm.verts:
        if v0 in seen: continue
        st = [v0]; seen.add(v0); fs = set()
        while st:
            v = st.pop(); fs.update(v.link_faces)
            for e in v.link_edges:
                o = e.other_vert(v)
                if o not in seen: seen.add(o); st.append(o)
        if fs: pieces.append(fs)
    trees = []
    for fs in pieces:
        vid = {}; P = []; F = []
        for f in fs:
            r = []
            for v in f.verts:
                if v.index not in vid: vid[v.index] = len(P); P.append(v.co.copy())
                r.append(vid[v.index])
            F.append(r)
        trees.append(BVHTree.FromPolygons(P, F))
    print(f'MERGE {name}: {len(pieces)} pieces, {sum(len(f) for f in pieces)} faces, open edges {sum(1 for e in bm.edges if e.is_boundary)}')
    bmesh.ops.holes_fill(bm, edges=[e for e in bm.edges if e.is_boundary], sides=0)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new('solid'); bm.to_mesh(me); bm.free()
    ob = bpy.data.objects.new(f'{name}_bark', me); bpy.context.scene.collection.objects.link(ob); bpy.context.view_layer.objects.active = ob
    # 1 union
    r = ob.modifiers.new('r', 'REMESH'); r.mode = 'VOXEL'; r.voxel_size = VOXEL; r.use_smooth_shade = True
    bpy.ops.object.modifier_apply(modifier='r')
    # fragments of twigs too thin for the voxels would float: drop them
    bm2 = bmesh.new(); bm2.from_mesh(ob.data); seen2 = set(); drop = []; kept = 0
    for v0 in bm2.verts:
        if v0 in seen2: continue
        st = [v0]; seen2.add(v0); part = []
        while st:
            v = st.pop(); part.append(v)
            for e in v.link_edges:
                o = e.other_vert(v)
                if o not in seen2: seen2.add(o); st.append(o)
        if len(part) < MIN_PART: drop += part
        else: kept += 1
    bmesh.ops.delete(bm2, geom=drop, context='VERTS'); bm2.to_mesh(ob.data); bm2.free()
    me = ob.data; print(f'MERGE voxel skin: {len(me.polygons)} faces, kept {kept} parts, dropped {len(drop)} fragment vertices')
    # 2 collars: a vertex near two or more original pieces is at a joint
    co = get_co(me); edges = get_edges(me); near = np.zeros(len(co), np.float32); joint = np.zeros(len(co), np.float32)
    for i, c in enumerate(co):
        ds = sorted(d for d in ((t.find_nearest(Vector(c), COLLAR_REACH)[3]) for t in trees) if d is not None)
        if len(ds) >= 2: joint[i] = 1 - min(1.0, ds[1] / COLLAR_REACH)
    w = np.clip(joint * 1.6, 0, 1) ** .7
    w = smooth(w[:, None].repeat(3, 1), edges, np.full(len(co), .6, np.float32), 4)[:, 0]    # soft edge between collar and bark
    print(f'MERGE collar vertices {int((w > .1).sum())} of {len(co)}')
    co = smooth(co, edges, np.full(len(co), .5, np.float32), 2)                               # voxel steps everywhere
    co = smooth(co, edges, w * .6, COLLAR_PASSES)
    me.vertices.foreach_set('co', co.ravel()); me.update()
    # 3 grain UVs, on the dense skin: a reduced trunk has too few points for an accurate angle and its errors bend the ridges
    grain_uv(ob)
    # 4 reduce, which carries the UVs along
    me = ob.data; d = ob.modifiers.new('d', 'DECIMATE'); d.ratio = min(1.0, TRIS / max(1, len(me.polygons)))
    bpy.ops.object.modifier_apply(modifier='d')
    for p in ob.data.polygons: p.use_smooth = True
    print(f'MERGE reduced: {len(ob.data.polygons)} triangles, {len(ob.data.vertices)} vertices')
    return src, ob

def grain_uv(ob):
    me = ob.data; bm = bmesh.new(); bm.from_mesh(me); bmesh.ops.triangulate(bm, faces=bm.faces); bm.to_mesh(me); bm.free()
    co = get_co(me); edges = get_edges(me); n = len(co)
    nb = [[] for _ in range(n)]
    L = np.linalg.norm(co[edges[:, 0]] - co[edges[:, 1]], axis=1)
    for (a, b), l in zip(edges, L): nb[a].append((b, float(l))); nb[b].append((a, float(l)))
    # U: distance along the surface from the lowest point of each connected part
    dist = np.full(n, np.inf); parts = 0
    for i in np.argsort(co[:, 2]):
        if np.isfinite(dist[i]): continue
        parts += 1; dist[i] = max(0.0, float(co[i, 2] - co[:, 2].min())); hp = [(dist[i], int(i))]
        if parts == 1:
            for j in np.nonzero(co[:, 2] < co[:, 2].min() + .08)[0]: dist[j] = 0.0; hp.append((0.0, int(j)))
            heapq.heapify(hp)
        while hp:
            d0, v = heapq.heappop(hp)
            if d0 > dist[v]: continue
            for o, l in nb[v]:
                if d0 + l < dist[o]: dist[o] = d0 + l; heapq.heappush(hp, (d0 + l, o))
    print(f'MERGE skin parts {parts}, longest path {dist.max():.1f} m')
    # slices of the limb: same band + connected. Each gives a centre, an axis and a girth for its vertices
    band = (dist / BAND).astype(int); comp = np.full(n, -1); ncomp = 0
    for i in range(n):
        if comp[i] >= 0: continue
        comp[i] = ncomp; st = [i]
        while st:
            v = st.pop()
            for o, _ in nb[v]:
                if comp[o] < 0 and band[o] == band[v]: comp[o] = ncomp; st.append(o)
        ncomp += 1
    # stray specks: a few vertices can land in a slice but not touch the rest of it; on their own they get a wild centre and
    # angle, which shows as a jagged notch. They join whichever neighbouring slice they touch most
    for _ in range(4):
        size = np.bincount(comp, minlength=ncomp); moved = 0
        for c in np.nonzero(size < MIN_SLICE)[0]:
            idx = np.nonzero(comp == c)[0]
            if not len(idx): continue
            around = [comp[o] for i in idx for o, _ in nb[i] if comp[o] != c]
            if around: comp[idx] = np.bincount(around).argmax(); moved += 1
        if not moved: break
    print(f'MERGE slices {len(np.unique(comp))} of {ncomp}, smallest {np.bincount(comp)[np.bincount(comp) > 0].min()} vertices')
    # each slice's "zero angle" comes from the slice below it, so the wrap line stays on one side going up a limb; if it were
    # chosen per slice it would wander sideways, across the grain, and read as a cut
    parent = {}
    links = {}
    for a, b in edges:
        ca, cb = comp[a], comp[b]
        if ca != cb and abs(band[a] - band[b]) == 1:
            lo, hi = (ca, cb) if band[a] < band[b] else (cb, ca); links[(hi, lo)] = links.get((hi, lo), 0) + 1
    for (hi, lo), k in sorted(links.items(), key=lambda kv: kv[1]): parent[hi] = lo   # most shared edges wins
    first = {}
    for i in range(n): first.setdefault(comp[i], i)
    centre = np.zeros((n, 3), np.float32); girth = np.zeros(n, np.float32); axes = {}; frame = {}
    for c in sorted(first, key=lambda c: band[first[c]]):
        idx = np.nonzero(comp == c)[0]; P = co[idx]; C = P.mean(0); ax = np.zeros(3)
        for i in idx:
            for o, _ in nb[i]: ax += (co[o] - co[i]) * (dist[o] - dist[i])
        ax = ax / (np.linalg.norm(ax) + 1e-6)
        ref = frame.get(parent.get(c), np.array((1.0, 0, 0)) if abs(ax[0]) < .8 else np.array((0, 1.0, 0)))
        e1 = ref - ax * ref.dot(ax)
        if np.linalg.norm(e1) < .1: e1 = np.cross(ax, (0, 0, 1.0))
        e1 /= np.linalg.norm(e1); frame[c] = e1; axes[c] = ax
        centre[idx] = C; girth[idx] = 2 * math.pi * np.linalg.norm(P - C, axis=1).mean()
    centre = smooth(centre, edges, np.full(n, .6, np.float32), 12)
    theta = np.zeros(n, np.float32)
    for c in first:
        idx = np.nonzero(comp == c)[0]; Q = co[idx] - centre[idx]; e1 = frame[c]; e2 = np.cross(axes[c], e1)
        theta[idx] = np.arctan2(Q @ e2, Q @ e1)
    z = np.stack([np.cos(theta), np.sin(theta), np.zeros(n, np.float32)], 1).astype(np.float32)   # blends the slices' small differences
    z = smooth(z, edges, np.full(n, .5, np.float32), 4); theta = np.arctan2(z[:, 1], z[:, 0])
    girth = smooth(girth[:, None].repeat(3, 1), edges, np.full(n, .7, np.float32), 40)[:, 0]
    # one texture repeat around the trunk's base (the texture tiles, so no wrap seam there); make_bark_textures.py paints its ridges
    # across = TILE / this, so ridges keep their real width. Limbs scale with their girth, easing from the whole-number wrap
    tile_v = float(np.median(girth[co[:, 2] < co[:, 2].min() + 1.0]))
    g = girth / tile_v; s = np.clip((g - .35) / .4, 0, 1); s = s * s * (3 - 2 * s)
    repeats = np.where(g >= .75, np.maximum(1, np.round(g)), g + (1 - g) * s)
    print(f'MERGE trunk girth {tile_v:.2f} m: recipe across = {TILE / tile_v:.2f}')
    for lo, hi in ((0, 1), (1, 2), (2, 3), (3, 5), (5, 8), (8, 12)):
        m = (co[:, 2] >= lo) & (co[:, 2] < hi)
        if m.any(): print(f'MERGE height {lo}-{hi} m: girth {np.percentile(girth[m], 10):.2f}-{np.percentile(girth[m], 90):.2f} m, repeats {np.percentile(repeats[m], 10):.2f}-{np.percentile(repeats[m], 90):.2f}')
    uvl = me.uv_layers.new(name='UVMap'); seams = 0
    for p in me.polygons:
        vs = list(p.vertices); t = theta[vs].copy()
        if t.max() - t.min() > math.pi: t[t < 0] += 2 * math.pi; seams += 1       # this face crosses the wrap: keep it whole
        for li, v, a in zip(p.loop_indices, vs, t):
            uvl.data[li].uv = (dist[v] / TILE, a / (2 * math.pi) * repeats[v])
    for p in me.polygons: p.use_smooth = True
    me.update(); print(f'MERGE final: {len(me.polygons)} triangles, wrap-seam faces {seams}')

def export_lods(name, ob):
    """MERGED_DIR/<name>[_LOD1|_LOD2].fbx = merged bark (slot 0) + the leaves of the same canopy FBX (their positions, normals and
    colours untouched), LOD0 with the tree's collision hulls. Same export settings as grow_fab_canopies.py."""
    for o in list(bpy.data.objects):
        if o is not ob: bpy.data.objects.remove(o)
    for suffix, tris in (('', TRIS), ('_LOD1', TRIS_LOD1), ('_LOD2', TRIS_LOD2)):
        fn = name + suffix
        for mat in list(bpy.data.materials): bpy.data.materials.remove(mat)   # or the next import makes "Bark.001": extra Unreal slots
        bpy.ops.object.select_all(action='DESELECT')
        bpy.ops.import_scene.fbx(filepath=f'{FBX_DIR}/{fn}.fbx')
        new = list(bpy.context.selected_objects); leaf = next(o for o in new if not o.name.startswith('UCX_') and o.type == 'MESH')
        hulls = [o for o in new if o.name.startswith('UCX_')]; junk = [o for o in new if o is not leaf and o not in hulls]
        for o in junk: bpy.data.objects.remove(o)
        leaf.name = fn; lm = leaf.data
        bm = bmesh.new(); bm.from_mesh(lm); bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.material_index == BARK_SLOT], context='FACES'); bm.to_mesh(lm); bm.free()
        bark = ob.copy(); bark.data = ob.data.copy(); bpy.context.scene.collection.objects.link(bark)
        bpy.ops.object.select_all(action='DESELECT'); bark.select_set(True); bpy.context.view_layer.objects.active = bark
        if len(bark.data.polygons) > tris:
            d = bark.modifiers.new('d', 'DECIMATE'); d.ratio = tris / len(bark.data.polygons); bpy.ops.object.modifier_apply(modifier='d')
        bm = bmesh.new(); bm.from_mesh(bark.data); bmesh.ops.triangulate(bm, faces=bm.faces); bm.to_mesh(bark.data); bm.free()
        bark.data.materials.clear(); bark.data.materials.append(lm.materials[BARK_SLOT])
        for p in bark.data.polygons: p.material_index = 0; p.use_smooth = True
        bark.data.update(); vn = [tuple(v.normal) for v in bark.data.vertices]; bark.data.normals_split_custom_set_from_vertices(vn)
        tb = len(bark.data.polygons)
        bpy.ops.object.select_all(action='DESELECT'); bark.select_set(True); leaf.select_set(True); bpy.context.view_layer.objects.active = leaf
        bpy.ops.object.join()
        for o in hulls: o.select_set(True)
        leaf.select_set(True); bpy.context.view_layer.objects.active = leaf
        bpy.ops.export_scene.fbx(filepath=f'{MERGED_DIR}/{fn}.fbx', use_selection=True, axis_forward='-Y', axis_up='Z', mesh_smooth_type='FACE', colors_type='LINEAR')
        tl = sum(len(p.vertices) - 2 for p in leaf.data.polygons)
        print(f'MERGE exported {fn}: bark {tb} + leaves {tl - tb} = {tl} triangles, hulls {len(hulls)}, slots {[m.name for m in leaf.data.materials]}, uv {[u.name for u in leaf.data.uv_layers]}')
        for o in [leaf] + hulls: bpy.data.objects.remove(o)

if __name__ == '__main__':
    names = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    os.makedirs(MERGED_DIR, exist_ok=True)
    for name in names:
        src, ob = merged_bark(name)
        bpy.ops.wm.save_as_mainfile(filepath=f'{MERGED_DIR}/{name}_bark.blend')
        export_lods(name, ob)
    print('MERGE_DONE')
