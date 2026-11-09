"""Ground and water refinements for L_WorldVista's test square, made in Unreal (Geometry Script), user request 2026-11-09:
  1. stray hollows that showed pond water outside the pond (beside Pool_Deep, beside Pond_3) are filled to just above
     their waterline;
  2. River_A / River_B end 12 cm below the water they flow into, so their square strip ends tuck under that surface;
  3. Pond_4 (the pond nearest the player spawn) becomes a larger, deeper, organic pond on locally tessellated terrain,
     with a shallow shelf, wet-earth shore colour, a disc-shaped water sheet and matching zone / mist;
  4. scatter instances (grass, flowers, rocks, trees, details) inside the new pond are removed, those on reshaped
     ground are re-seated.
Everything happens in memory; edit(save=True) saves the terrain, the new water disc mesh and the level (the editor must
be closed - an open editor keeps those files locked). Entry point: Tools/World/apply_ground_fixes.py."""
import unreal, math, json
from pathlib import Path

GS = unreal
TERRAIN = '/Game/RPG/Environment/World/Meshes/SM_Vista_Terrain'
DISC = '/Game/RPG/Environment/Water/SM_Water_Disc'
MARK, MARK_VALUE = 'RPGGroundFix', '2026-11-09'                # metadata tag on the terrain once these edits are saved
LOG = []
def log(msg):
    LOG.append(msg); unreal.log('GROUNDEDIT ' + msg)

def pick(x, cls):
    for v in (x if isinstance(x, tuple) else (x,)):
        if isinstance(v, cls): return v
    return None

# ------------------------------------------------------------------ terrain access
def load_terrain_mesh():
    sm = unreal.load_asset(TERRAIN)
    dm = unreal.DynamicMesh(); o = unreal.GeometryScriptCopyMeshFromAssetOptions(); r = unreal.GeometryScriptMeshReadLOD()
    o.set_editor_property('apply_build_settings', False)
    r.set_editor_property('lod_type', unreal.GeometryScriptLODType.SOURCE_MODEL)
    unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(sm, dm, o, r)
    return sm, dm

class Heights:
    """Ray-cast height queries against a dynamic mesh."""
    def __init__(self, dm):
        self.dm = dm
        self.bvh = pick(unreal.GeometryScript_MeshSpatial.build_bvh_for_mesh(dm, None), unreal.GeometryScriptDynamicMeshBVH)
        self.qo = unreal.GeometryScriptSpatialQueryOptions()
    def __call__(self, x, y):
        h = pick(unreal.GeometryScript_MeshSpatial.find_nearest_ray_intersection_with_mesh(
            self.dm, self.bvh, unreal.Vector(x, y, 50000.0), unreal.Vector(0, 0, -1), self.qo, None), unreal.GeometryScriptRayHitResult)
        return 50000.0 - h.get_editor_property('ray_parameter') if h and h.get_editor_property('hit') else None

def get_positions(dm):
    lst = pick(unreal.GeometryScript_MeshQueries.get_all_vertex_positions(dm, False), unreal.GeometryScriptVectorList)
    return list(unreal.GeometryScript_List.convert_vector_list_to_array(lst))

def set_positions(dm, pts):
    lst = unreal.GeometryScript_List.convert_array_to_vector_list(pts)
    lst = pick(lst, unreal.GeometryScriptVectorList) or lst
    unreal.GeometryScript_MeshEdits.set_all_mesh_vertex_positions(dm, lst)

def smooth(a, b, x):
    t = min(1.0, max(0.0, (x - a) / (b - a))) if b != a else (1.0 if x >= a else 0.0)
    return t * t * (3 - 2 * t)

# ------------------------------------------------------------------ level access
def actors():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()

def actor(label):
    return next(a for a in actors() if a.get_actor_label() == label)

def water_level(body):
    z = body.get_editor_property('zone')
    return body.get_actor_location().z + z.get_unscaled_box_extent().z

# ------------------------------------------------------------------ 1. stray hollows
def stray_mask(H, body, step=50.0):
    """Grid cells under the body's square sheet that are below its waterline but not connected to the pond centre."""
    L = body.get_actor_location(); half = body.get_editor_property('zone').get_unscaled_box_extent().x; W = water_level(body)
    n = int(2 * half / step) + 1; wet = [[False] * n for _ in range(n)]
    for j in range(n):
        for i in range(n):
            g = H(L.x - half + i * step, L.y - half + j * step); wet[j][i] = g is not None and g < W
    c = n // 2; main = [[False] * n for _ in range(n)]; st = [(c, c)]
    while st:
        jj, ii = st.pop()
        if 0 <= jj < n and 0 <= ii < n and wet[jj][ii] and not main[jj][ii]:
            main[jj][ii] = True; st += [(jj + 1, ii), (jj - 1, ii), (jj, ii + 1), (jj, ii - 1)]
    stray = [(L.x - half + i * step, L.y - half + j * step) for j in range(n) for i in range(n) if wet[j][i] and not main[j][i]]
    mains = [(L.x - half + i * step, L.y - half + j * step) for j in range(n) for i in range(n) if main[j][i]]
    return W, stray, mains

FILLED = []                                                       # (x, y) of raised vertices: their wet colour is cleared
def fill_hollows(pts, H, bodies, lift=12.0, reach=120.0):
    """Raise vertices within `reach` of a stray cell (and not near the pond itself) to waterline + lift.
    Returns the boxes that changed (for re-seating scatter instances)."""
    moved = 0; boxes = []
    for label in bodies:
        body = actor(label); W, stray, mains = stray_mask(H, body)
        if not stray:
            log(f'hollow {label}: none'); continue
        xs = [p[0] for p in stray]; ys = [p[1] for p in stray]
        bx0, bx1, by0, by1 = min(xs) - reach, max(xs) + reach, min(ys) - reach, max(ys) + reach
        L = body.get_actor_location(); half = body.get_editor_property('zone').get_unscaled_box_extent().x
        ox, oy, step = L.x - half, L.y - half, 50.0                     # the grid the cells came from
        key = lambda x, y: (round((x - ox) / step), round((y - oy) / step))
        sset = {key(x, y) for x, y in stray}; mset = {key(x, y) for x, y in mains}
        def near(cells, x, y, r):
            i0, j0 = (x - ox) / step, (y - oy) / step; m = int(r / step) + 1
            for i in range(int(i0) - m, int(i0) + m + 2):
                for j in range(int(j0) - m, int(j0) + m + 2):
                    if (i, j) in cells and math.hypot(ox + i * step - x, oy + j * step - y) <= r: return True
            return False
        n = 0
        for k, p in enumerate(pts):
            if not (bx0 <= p.x <= bx1 and by0 <= p.y <= by1): continue
            if p.z >= W + lift or near(mset, p.x, p.y, 75.0) or not near(sset, p.x, p.y, reach): continue
            pts[k] = unreal.Vector(p.x, p.y, W + lift); n += 1; FILLED.append((p.x, p.y))
        moved += n; boxes.append((bx0, bx1, by0, by1))
        log(f'hollow {label}: {len(stray)} stray cells ({len(stray) * 0.25:.0f} m2), raised {n} vertices to waterline+{lift:.0f} cm')
    return boxes

# ------------------------------------------------------------------ 2. river ends
def tuck_river_ends(drop=12.0):
    for label, into in (('River_A', 'Lake'), ('River_B', 'Pond_2')):
        river = actor(label); W = water_level(actor(into)); spl = river.get_editor_property('spline')
        n = spl.get_number_of_spline_points()
        for i in (n - 2, n - 1):
            p = spl.get_location_at_spline_point(i, unreal.SplineCoordinateSpace.WORLD)
            spl.set_location_at_spline_point(i, unreal.Vector(p.x, p.y, min(p.z, W - drop)), unreal.SplineCoordinateSpace.WORLD, True)
        river.rebuild()
        log(f'river {label}: last two points now {drop:.0f} cm under {into} (water {W:.0f})')

# ------------------------------------------------------------------ 3. Pond_4
LOBES = ((0.12, 2, 0.7), (0.07, 3, 2.1), (0.04, 5, 0.4))
def shape_r(theta, rx, ry, rot):
    t = theta - rot
    base = (rx * ry) / math.sqrt((ry * math.cos(t)) ** 2 + (rx * math.sin(t)) ** 2)
    return base * (1 + sum(a * math.sin(k * theta + ph) for a, k, ph in LOBES))

def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay; L2 = dx * dx + dy * dy
    t = 0.0 if L2 == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L2))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))

def plan_pond(H, path_pts, label='Pond_4', max_berm=80.0, edge=9750.0):
    """Largest organic pond grown from the old one (its centre stays well inside the new shape, so the old basin is
    swallowed): inside the terrain square (bank included), clear of the path, with a waterline the surrounding ground
    holds - a built-up lip of at most `max_berm` where the ground falls away."""
    body = actor(label); L = body.get_actor_location(); best = None; tried = 0
    segs = list(zip(path_pts, path_pts[1:]))
    shapes = [((1300.0, 900.0), math.radians(a)) for a in (-12, 15, 45, 75, 100)] + [((1150.0, 1000.0), math.radians(a)) for a in (0, 60)]
    for dy in range(-600, 401, 200):
      for dx in range(-600, 601, 200):
        cx, cy = L.x + dx, L.y + dy
        for base, rot in shapes:
            for k in [x / 20 for x in range(20, 9, -1)]:                     # 1.00 .. 0.50 (26 x 18 m at most: about 5x the old pond)
                rx, ry = base[0] * k, base[1] * k; tried += 1
                th0 = math.atan2(L.y - cy, L.x - cx)
                if math.hypot(L.x - cx, L.y - cy) > 0.5 * shape_r(th0, rx, ry, rot): continue   # grow the old pond, don't move it
                outline = [(cx + math.cos(math.radians(a)) * shape_r(math.radians(a), rx, ry, rot),
                            cy + math.sin(math.radians(a)) * shape_r(math.radians(a), rx, ry, rot), math.radians(a)) for a in range(0, 360, 6)]
                far = [(cx + (x - cx) * 1.9, cy + (y - cy) * 1.9) for x, y, _ in outline]
                if any(abs(x) > edge or abs(y) > edge for x, y in far): continue        # bank stays on the terrain
                if min(seg_dist(x, y, *a, *b) for x, y, _ in outline for a, b in segs) < 600: continue   # 6 m clear of the path
                ring = []
                for x, y, th in outline:
                    for s in (1.0, 1.15, 1.3):
                        g = H(cx + (x - cx) * s, cy + (y - cy) * s)
                        if g is not None: ring.append(g)
                ring.sort()
                W = ring[len(ring) // 10] - 25.0                             # most of the rim stands >= 25 cm above the water
                if ring[0] < W - max_berm + 20.0: continue                   # the rest needs at most a low built-up lip
                score = k * k * 1000 + W * 0.01
                if not best or score > best['score']:
                    best = dict(cx=cx, cy=cy, rx=rx, ry=ry, rot=rot, W=W, k=k, score=score, rim_min=ring[0])
                break                                                          # largest k that fits at this centre
    log(f'pond search: {tried} candidates, best k={best["k"] if best else None}')
    return best

def reshape_pond(dm, plan, depth=320.0, shelf=55.0):
    cx, cy, rx, ry, rot, W = plan['cx'], plan['cy'], plan['rx'], plan['ry'], plan['rot'], plan['W']
    rmax = max(rx, ry) * 1.3
    # finer triangles (0.8 m -> 0.4 m) where the shore and bank will be: a smooth organic shoreline instead of steps
    box = unreal.Box(unreal.Vector(cx - rmax * 1.6, cy - rmax * 1.6, -100000), unreal.Vector(cx + rmax * 1.6, cy + rmax * 1.6, 100000))
    sel = pick(unreal.GeometryScript_MeshSelection.select_mesh_elements_in_box(
        dm, box, unreal.GeometryScriptMeshSelectionType.TRIANGLES, False, 3), unreal.GeometryScriptMeshSelection)
    before = dm.get_triangle_count()
    unreal.GeometryScript_MeshSubdivide.apply_selective_tessellation(dm, sel, unreal.GeometryScriptSelectiveTessellateOptions(), 1,
                                                                     unreal.SelectiveTessellatePatternType.CONCENTRIC_RINGS)
    log(f'pond: tessellated {before} -> {dm.get_triangle_count()} triangles around the pond')
    pts = get_positions(dm); changed = []
    for k, p in enumerate(pts):
        dx, dy = p.x - cx, p.y - cy
        if abs(dx) > rmax * 2.1 or abs(dy) > rmax * 2.1: continue
        th = math.atan2(dy, dx); s = math.hypot(dx, dy) / shape_r(th, rx, ry, rot)
        if s >= 2.1: continue
        z0 = p.z
        if s <= 1.0:
            d = shelf * smooth(1.0, 0.8, s) + (depth - shelf) * smooth(0.78, 0.35, s)   # shallow shelf, then the deep middle
            z = min(z0, W - max(d, 4.0))
        else:
            zb = max(z0, W + 20.0 + (s - 1.0) * 60.0)                     # bank never lower than the waterline + 20 cm
            zs = (W - 4.0) + (zb - (W - 4.0)) * smooth(1.0, 1.35, s)      # shore slope up from the waterline
            z = zs + (z0 - zs) * smooth(1.5, 1.9, s)                      # back to the original ground
        if abs(z - z0) > 0.5:
            pts[k] = unreal.Vector(p.x, p.y, z); changed.append(k)
    set_positions(dm, pts)
    log(f'pond: reshaped {len(changed)} vertices; water {W:.0f} cm, size {2 * rx / 100:.1f} x {2 * ry / 100:.1f} m, depth {depth / 100:.1f} m')
    return changed

def wet_shore_colours(dm, plan):
    """Terrain vertex colour G = wet earth: the pond bed and a band just above the new waterline."""
    cx, cy, rx, ry, rot, W = plan['cx'], plan['cy'], plan['rx'], plan['ry'], plan['rot'], plan['W']
    cl = pick(unreal.GeometryScript_VertexColors.get_mesh_per_vertex_colors(dm), unreal.GeometryScriptColorList)
    cols = list(unreal.GeometryScript_List.convert_color_list_to_array(cl)) if cl else None
    if not cols: log('pond: no vertex colours found - shore colour skipped'); return
    pts = get_positions(dm); n = 0
    for k, p in enumerate(pts):
        dx, dy = p.x - cx, p.y - cy; th = math.atan2(dy, dx); s = math.hypot(dx, dy) / shape_r(th, rx, ry, rot)
        if s >= 2.0: continue
        g = smooth(W + 60.0, W + 5.0, p.z) * (1 - smooth(1.15, 1.4, s))    # narrow wet band at the new shore only
        c = cols[k]; cols[k] = unreal.LinearColor(0.0 if s < 1.4 else c.r, max(c.g * smooth(1.45, 2.0, s), g), c.b, c.a); n += 1
    unreal.GeometryScript_VertexColors.set_mesh_per_vertex_colors(dm, unreal.GeometryScript_List.convert_array_to_color_list(cols))
    log(f'pond: shore colours on {n} vertices')

def dry_filled_hollows(dm, reach=260.0, clear=150.0):
    """Filled hollows are dry ground now: fade their wet-earth colour (G) back to grass, softly at the edges."""
    if not FILLED: return
    cell = 100.0; grid = {}
    for x, y in FILLED: grid.setdefault((int(x // cell), int(y // cell)), []).append((x, y))
    cl = pick(unreal.GeometryScript_VertexColors.get_mesh_per_vertex_colors(dm), unreal.GeometryScriptColorList)
    cols = list(unreal.GeometryScript_List.convert_color_list_to_array(cl)) if cl else None
    if not cols: return
    pts = get_positions(dm); n = 0; m = int(reach // cell) + 1
    for k, p in enumerate(pts):
        gx, gy = int(p.x // cell), int(p.y // cell); best = None
        for i in range(gx - m, gx + m + 1):
            for j in range(gy - m, gy + m + 1):
                for x, y in grid.get((i, j), ()):
                    d = math.hypot(x - p.x, y - p.y)
                    if best is None or d < best: best = d
        if best is None or best > reach: continue
        c = cols[k]; cols[k] = unreal.LinearColor(c.r, c.g * smooth(clear, reach, best), c.b, c.a); n += 1
    unreal.GeometryScript_VertexColors.set_mesh_per_vertex_colors(dm, unreal.GeometryScript_List.convert_array_to_color_list(cols))
    log(f'hollows: wet colour faded out on {n} vertices')

def water_disc():
    if unreal.EditorAssetLibrary.does_asset_exist(DISC): return unreal.load_asset(DISC)
    src = unreal.load_asset('/Game/RPG/Environment/Water/SM_Water_Lake')
    dm = unreal.DynamicMesh(); o = unreal.GeometryScriptCopyMeshFromAssetOptions(); r = unreal.GeometryScriptMeshReadLOD()
    r.set_editor_property('lod_type', unreal.GeometryScriptLODType.SOURCE_MODEL)
    unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(src, dm, o, r)
    ext = src.get_bounds().box_extent.x; org = src.get_bounds().origin
    # delete triangles whose centroid lies outside the inscribed circle
    kill = []
    for t in range(dm.get_triangle_count()):
        r3 = unreal.GeometryScript_MeshQueries.get_triangle_positions(dm, t)
        vs = [v for v in (r3 if isinstance(r3, tuple) else (r3,)) if isinstance(v, unreal.Vector)]
        if len(vs) < 3: continue
        mx = sum(v.x for v in vs[:3]) / 3 - org.x; my = sum(v.y for v in vs[:3]) / 3 - org.y
        if math.hypot(mx, my) > ext * 0.995: kill.append(t)
    lst = unreal.GeometryScript_List.convert_array_to_index_list(kill, unreal.GeometryScriptIndexType.TRIANGLE)
    lst = pick(lst, unreal.GeometryScriptIndexList) or lst
    unreal.GeometryScript_MeshEdits.delete_triangles_from_mesh(dm, lst, False)
    opts = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    opts.set_editor_property('enable_collision', False)
    res = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dm, DISC, opts)
    disc = pick(res, unreal.StaticMesh) or unreal.load_asset(DISC)
    disc.set_material(0, src.get_material(0))
    log(f'water disc: {dm.get_triangle_count()} triangles kept of the square sheet (radius = half-extent {ext:.0f})')
    return disc

def pond_water_mesh(plan, label, margin=1.08):
    """The disc water sheet warped to the pond outline (+8%: tucked under the shore, never past the raised bank)."""
    path = f'/Game/RPG/Environment/Water/SM_Water_{label}'
    disc = water_disc(); ext = disc.get_bounds().box_extent.x; org = disc.get_bounds().origin
    dm = unreal.DynamicMesh(); o = unreal.GeometryScriptCopyMeshFromAssetOptions(); r = unreal.GeometryScriptMeshReadLOD()
    r.set_editor_property('lod_type', unreal.GeometryScriptLODType.SOURCE_MODEL)
    unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(disc, dm, o, r)
    pts = get_positions(dm)
    for k, p in enumerate(pts):
        lx, ly = p.x - org.x, p.y - org.y; rho = math.hypot(lx, ly) / ext; th = math.atan2(ly, lx)
        R = shape_r(th, plan['rx'], plan['ry'], plan['rot']) * margin * rho
        pts[k] = unreal.Vector(math.cos(th) * R, math.sin(th) * R, p.z - org.z)
    set_positions(dm, pts)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        sm = unreal.load_asset(path); tl = unreal.GeometryScriptMeshWriteLOD(); tl.set_editor_property('lod_index', 0)
        unreal.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(dm, sm, unreal.GeometryScriptCopyMeshToAssetOptions(), tl)
    else:
        opts = unreal.GeometryScriptCreateNewStaticMeshAssetOptions(); opts.set_editor_property('enable_collision', False)
        sm = pick(unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dm, path, opts), unreal.StaticMesh) or unreal.load_asset(path)
    sm.set_material(0, disc.get_material(0))
    WATER_ASSETS.append(path)
    return sm

WATER_ASSETS = []
def place_pond_water(plan, label='Pond_4', mist_index=4):
    body = actor(label); W, cx, cy = plan['W'], plan['cx'], plan['cy']
    outline = [(math.cos(math.radians(a)) * shape_r(math.radians(a), plan['rx'], plan['ry'], plan['rot']),
                math.sin(math.radians(a)) * shape_r(math.radians(a), plan['rx'], plan['ry'], plan['rot'])) for a in range(0, 360, 3)]
    hx = max(abs(x) for x, _ in outline); hy = max(abs(y) for _, y in outline)   # zone = the pond extent: corners stay on the raised bank
    R = max(hx, hy)
    half_depth = 300.0
    body.set_actor_location(unreal.Vector(cx, cy, W - half_depth), False, False)
    z = body.get_editor_property('zone'); z.set_box_extent(unreal.Vector(hx, hy, half_depth), True)
    body.set_editor_property('water_mesh', pond_water_mesh(plan, label))
    body.set_editor_property('surface_scale', unreal.Vector(1, 1, 1))
    # setting the properties above re-runs the water body construction (surface placed on the zone top)
    rmean = (plan['rx'] + plan['ry']) / 2 / 100
    for k, (dz, s_) in enumerate(((30, 1.25), (90, 1.05))):
        try:
            m = actor(f'Mist_{mist_index}_{k}'); m.set_actor_location(unreal.Vector(cx, cy, W + dz), False, False)
            m.set_actor_scale3d(unreal.Vector(rmean * s_, rmean * s_, 1))
        except StopIteration: pass
    log(f'pond water: {label} at ({cx:.0f}, {cy:.0f}) level {W:.0f}, sheet shaped to the outline, zone {2 * hx:.0f} x {2 * hy:.0f} x {2 * half_depth:.0f}')

# ------------------------------------------------------------------ 4. scatter instances
def fix_scatter(old_H, new_H, plan, regions):
    """Remove instances inside the new pond; re-seat instances on reshaped / filled ground."""
    cx, cy, rx, ry, rot = plan['cx'], plan['cy'], plan['rx'], plan['ry'], plan['rot']
    removed = moved = 0
    for a in actors():
        if a.get_class().get_name() != 'RPGScatter': continue
        ism = a.get_editor_property('instances'); kill = []; n = ism.get_instance_count()
        big = a.get_editor_property('collision')
        for i in range(n):
            t = pick(ism.get_instance_transform(i, True), unreal.Transform); p = t.translation
            dx, dy = p.x - cx, p.y - cy; s = math.hypot(dx, dy) / shape_r(math.atan2(dy, dx), rx, ry, rot)
            if s < (1.25 if big else 1.06):
                kill.append(i); continue
            if s < 2.15 or any(x0 <= p.x <= x1 and y0 <= p.y <= y1 for x0, x1, y0, y1 in regions):
                g0, g1 = old_H(p.x, p.y), new_H(p.x, p.y)
                if g0 is not None and g1 is not None and abs(g1 - g0) > 1.0:
                    t.translation = unreal.Vector(p.x, p.y, p.z + (g1 - g0)); ism.update_instance_transform(i, t, True, False, True); moved += 1
        if kill:
            ism.remove_instances(sorted(kill, reverse=True)); removed += len(kill)
            try: a.set_editor_property('placed', ism.get_instance_count())
            except Exception: pass
    log(f'scatter: removed {removed} instances in the new pond, re-seated {moved} on reshaped ground')

# ------------------------------------------------------------------ main
def edit(save=False):
    LOG.clear(); FILLED.clear(); WATER_ASSETS.clear()
    sm, dm = load_terrain_mesh()
    if unreal.EditorAssetLibrary.get_metadata_tag(sm, MARK) == MARK_VALUE:      # already applied and saved: never re-plan the pond
        log("ground edits already applied (terrain tagged) - skipped"); return None
    old_H = Heights(load_terrain_mesh()[1])                     # untouched copy: the ground before any edit
    layout = json.load(open('C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs/World_R01/exports/vista_layout.json'))
    path_pts = [(x * 100.0, -y * 100.0) for x, y in layout['path']]
    # 1. hollows (Pond_4 is reshaped below, so its own square sheet no longer matters)
    pts = get_positions(dm)
    regions = fill_hollows(pts, old_H, ('Pool_Deep', 'Pond_3', 'Pond_1', 'Pond_2', 'Lake'))
    set_positions(dm, pts)
    # 3. pond (plan on the original ground)
    plan = plan_pond(old_H, path_pts)
    if not plan: raise RuntimeError('no pond shape fits')
    log('pond plan: ' + json.dumps({k: round(v, 3) for k, v in plan.items()}))
    reshape_pond(dm, plan); wet_shore_colours(dm, plan); dry_filled_hollows(dm)
    unreal.GeometryScript_Normals.recompute_normals(dm, unreal.GeometryScriptCalculateNormalsOptions())
    # back into the terrain asset (Nanite + full-detail collision settings are kept on the asset)
    co = unreal.GeometryScriptCopyMeshToAssetOptions()
    for k, v in (('replace_materials', False), ('enable_recompute_normals', False), ('enable_recompute_tangents', True), ('enable_remove_degenerates', True), ('emit_transaction', False)):
        try: co.set_editor_property(k, v)
        except Exception as ex: log(f'copy option {k}: {ex}')
    tl = unreal.GeometryScriptMeshWriteLOD(); tl.set_editor_property('lod_index', 0)
    out = unreal.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(dm, sm, co, tl)
    log(f'terrain written back: {out}')
    new_H = Heights(dm)
    # 2. rivers, water, scatter
    tuck_river_ends()
    place_pond_water(plan)
    fix_scatter(old_H, new_H, plan, regions)
    if save:
        unreal.EditorAssetLibrary.set_metadata_tag(sm, MARK, MARK_VALUE)
        assert unreal.EditorAssetLibrary.save_loaded_asset(sm, only_if_is_dirty=False), "terrain save failed (editor open?)"
        for path in [DISC] + WATER_ASSETS:
            if unreal.EditorAssetLibrary.does_asset_exist(path): unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
        log('saved terrain, water disc and L_WorldVista')
    p = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())) / 'GroundFix'
    (p / 'edit_log.json').write_text(json.dumps({'log': LOG, 'plan': plan}, indent=1))
    return plan
