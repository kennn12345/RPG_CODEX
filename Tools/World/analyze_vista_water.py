"""Read-only: find water that shows where it shouldn't in L_WorldVista -> Saved/GroundFix/water_<tag>.json.

Ponds/lake: each water sheet is a square (zone half-extent = 1.6 x radius) at the waterline. Every 50 cm of that square is
"wet" when the visible terrain is below the waterline. Wet cells connected to the pond centre are the pond; other wet
islands are stray puddles; wet cells on the square's border show a straight water edge.
Rivers: along each spline, compare the water height with the terrain under the centreline and the banks.
Env: RPG_WATER_TAG (default 'before')."""
import unreal, json, os, math
from pathlib import Path
TAG = os.environ.get('RPG_WATER_TAG', 'before'); STEP = 50.0
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = unreal.EditorLevelLibrary.get_editor_world()
if not world or 'L_WorldVista' not in world.get_path_name():
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level('/Game/RPG/Maps/L_WorldVista')

sm = unreal.load_asset('/Game/RPG/Environment/World/Meshes/SM_Vista_Terrain')
dm = unreal.DynamicMesh(); o = unreal.GeometryScriptCopyMeshFromAssetOptions(); r = unreal.GeometryScriptMeshReadLOD()
r.set_editor_property('lod_type', unreal.GeometryScriptLODType.HI_RES_SOURCE_MODEL)
unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(sm, dm, o, r)
if dm.get_triangle_count() == 0:
    r.set_editor_property('lod_type', unreal.GeometryScriptLODType.SOURCE_MODEL); unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(sm, dm, o, r)
def pick(x, cls):
    for v in (x if isinstance(x, tuple) else (x,)):
        if isinstance(v, cls): return v
BVH = pick(unreal.GeometryScript_MeshSpatial.build_bvh_for_mesh(dm, None), unreal.GeometryScriptDynamicMeshBVH)
QO = unreal.GeometryScriptSpatialQueryOptions()
def ground(x, y):
    res = pick(unreal.GeometryScript_MeshSpatial.find_nearest_ray_intersection_with_mesh(dm, BVH, unreal.Vector(x, y, 50000.0), unreal.Vector(0, 0, -1), QO, None), unreal.GeometryScriptRayHitResult)
    return 50000.0 - res.get_editor_property('ray_parameter') if res and res.get_editor_property('hit') else None

out = {'ponds': [], 'rivers': []}
for a in eas.get_all_level_actors():
    cls = a.get_class().get_name(); lab = a.get_actor_label()
    if cls == 'RPGWaterBody':
        zone = a.get_editor_property('zone'); ext = zone.get_unscaled_box_extent()
        L = a.get_actor_location(); wz = L.z + ext.z                       # zone top = waterline
        half = max(ext.x, ext.y); n = int(2 * half / STEP) + 1          # square grid over the (possibly rectangular) zone
        wet = [[False] * n for _ in range(n)]; gz = [[None] * n for _ in range(n)]
        for j in range(n):
            for i in range(n):
                x, y = L.x - half + i * STEP, L.y - half + j * STEP
                if abs(x - L.x) > ext.x or abs(y - L.y) > ext.y: continue      # outside the zone
                g = ground(x, y); gz[j][i] = None if g is None else round(g, 1)
                wet[j][i] = g is not None and g < wz
        # flood fill from the centre (the pond)
        c = n // 2; seen = [[False] * n for _ in range(n)]; stack = [(c, c)] if wet[c][c] else []
        while stack:
            jj, ii = stack.pop()
            if not (0 <= jj < n and 0 <= ii < n) or seen[jj][ii] or not wet[jj][ii]: continue
            seen[jj][ii] = True; stack += [(jj + 1, ii), (jj - 1, ii), (jj, ii + 1), (jj, ii - 1)]
        main = sum(map(sum, seen)); stray = sum(wet[j][i] and not seen[j][i] for j in range(n) for i in range(n))
        border = sum(wet[j][i] for j in range(n) for i in range(n)
                     if abs(-half + i * STEP) > ext.x - STEP or abs(-half + j * STEP) > ext.y - STEP)   # wet on the zone's edge
        # stray islands as boxes
        islands = []; lab2 = [[0] * n for _ in range(n)]
        for j in range(n):
            for i in range(n):
                if wet[j][i] and not seen[j][i] and not lab2[j][i]:
                    st = [(j, i)]; cells = []
                    while st:
                        jj, ii = st.pop()
                        if not (0 <= jj < n and 0 <= ii < n) or lab2[jj][ii] or not wet[jj][ii] or seen[jj][ii]: continue
                        lab2[jj][ii] = 1; cells.append((jj, ii)); st += [(jj + 1, ii), (jj - 1, ii), (jj, ii + 1), (jj, ii - 1)]
                    ys = [L.y - half + q[0] * STEP for q in cells]; xs = [L.x - half + q[1] * STEP for q in cells]
                    depth = max(wz - gz[q[0]][q[1]] for q in cells)
                    islands.append({'cells': len(cells), 'x': [min(xs), max(xs)], 'y': [min(ys), max(ys)], 'max_depth_cm': round(depth, 1)})
        # radius of the main pond (max distance of a pond cell from centre)
        rmax = max((math.hypot((i - c) * STEP, (j - c) * STEP) for j in range(n) for i in range(n) if seen[j][i]), default=0)
        out['ponds'].append({'label': lab, 'center': [L.x, L.y], 'water_z': wz, 'half': half, 'main_cells': main, 'main_radius_cm': rmax,
                             'stray_cells': stray, 'border_wet_cells': border, 'islands': sorted(islands, key=lambda d: -d['cells'])[:12],
                             'min_ground': min((g for row in gz for g in row if g is not None), default=None)})
        unreal.log(f'WATER_POND {lab} main={main} r={rmax:.0f} stray={stray} border={border} islands={len(islands)}')
    elif cls == 'RPGStylizedRiver':
        spl = a.get_editor_property('spline'); length = spl.get_spline_length(); width = a.get_editor_property('width')
        samples = []; d = 0.0
        while d <= length:
            p = spl.get_location_at_distance_along_spline(d, unreal.SplineCoordinateSpace.WORLD)
            rt = spl.get_right_vector_at_distance_along_spline(d, unreal.SplineCoordinateSpace.WORLD)
            sc = spl.get_scale_at_distance_along_spline(d); hw = width * 0.5 * sc.y
            gc = ground(p.x, p.y); gl = ground(p.x - rt.x * hw, p.y - rt.y * hw); gr = ground(p.x + rt.x * hw, p.y + rt.y * hw)
            samples.append({'d': round(d), 'x': round(p.x), 'y': round(p.y), 'water_z': round(p.z, 1), 'centre_depth': None if gc is None else round(p.z - gc, 1),
                            'left_bank': None if gl is None else round(gl - p.z, 1), 'right_bank': None if gr is None else round(gr - p.z, 1), 'half_width': round(hw)})
            d += 100.0
        floating = [s for s in samples if s['centre_depth'] is not None and s['centre_depth'] > 150]
        dry_bed = [s for s in samples if s['centre_depth'] is not None and s['centre_depth'] < 5]
        low_bank = [s for s in samples if (s['left_bank'] is not None and s['left_bank'] < -5) or (s['right_bank'] is not None and s['right_bank'] < -5)]
        out['rivers'].append({'label': lab, 'length': length, 'samples': samples, 'deep_over_ground_150': len(floating), 'bed_above_water': len(dry_bed), 'bank_below_water_edge': len(low_bank)})
        unreal.log(f'WATER_RIVER {lab} len={length:.0f} deep>150={len(floating)} bed_above_water={len(dry_bed)} bank_edge_wet={len(low_bank)}')
p = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())) / 'GroundFix'
p.mkdir(parents=True, exist_ok=True); (p / f'water_{TAG}.json').write_text(json.dumps(out))
unreal.log('WATER_DONE')
if os.environ.get('RPG_NO_QUIT') != '1': unreal.SystemLibrary.quit_editor()
