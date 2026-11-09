"""Read-only: how far the terrain's collision surface (Nanite fallback = render LOD0) sits from its visible surface
(the full-resolution source mesh). Writes Saved/GroundFix/terrain_gap_<tag>.json (grid of visible - collision heights, cm).
Run: UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="<repo>/Tools/World/measure_terrain_collision.py" -unattended -RenderOffscreen
Env RPG_GAP_TAG names the output (default 'before'); RPG_GAP_STEP grid step in cm (default 100)."""
import unreal, json, os, math
from pathlib import Path
GS = unreal
TAG = os.environ.get('RPG_GAP_TAG', 'before'); STEP = float(os.environ.get('RPG_GAP_STEP', '100'))
HALF = 9800.0                                    # the 196 x 196 m playable square
sm = unreal.load_asset('/Game/RPG/Environment/World/Meshes/SM_Vista_Terrain')

def copy(lod_type):
    dm = unreal.DynamicMesh()
    opts = unreal.GeometryScriptCopyMeshFromAssetOptions()
    opts.set_editor_property('apply_build_settings', True)
    req = unreal.GeometryScriptMeshReadLOD(); req.set_editor_property('lod_type', lod_type); req.set_editor_property('lod_index', 0)
    dm, outcome = unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(sm, dm, opts, req)
    return dm, str(outcome)

def pick(r, cls):
    for v in (r if isinstance(r, tuple) else (r,)):
        if isinstance(v, cls): return v
    return None

def bvh(dm):
    return pick(unreal.GeometryScript_MeshSpatial.build_bvh_for_mesh(dm, None), unreal.GeometryScriptDynamicMeshBVH)

vis, o1 = copy(unreal.GeometryScriptLODType.HI_RES_SOURCE_MODEL)
if 'Success' not in o1 or vis.get_triangle_count() == 0:
    vis, o1 = copy(unreal.GeometryScriptLODType.SOURCE_MODEL)
col, o2 = copy(unreal.GeometryScriptLODType.RENDER_DATA)
info = {'visible_tris': vis.get_triangle_count(), 'collision_tris': col.get_triangle_count(), 'copy': [o1, o2], 'step': STEP}
unreal.log(f'GAP meshes visible={info["visible_tris"]} collision={info["collision_tris"]}')
BV, BC = bvh(vis), bvh(col)
qo = unreal.GeometryScriptSpatialQueryOptions()

def height(dm, b, x, y):
    r = unreal.GeometryScript_MeshSpatial.find_nearest_ray_intersection_with_mesh(
        dm, b, unreal.Vector(x, y, 50000.0), unreal.Vector(0, 0, -1), qo, None)
    res = pick(r, unreal.GeometryScriptRayHitResult)
    if res is None or not res.get_editor_property('hit'): return None
    t = res.get_editor_property('ray_parameter')
    return 50000.0 - t if t and t > 0 else None

_r = unreal.GeometryScript_MeshSpatial.find_nearest_ray_intersection_with_mesh(vis, BV, unreal.Vector(0, 0, 50000.0), unreal.Vector(0, 0, -1), qo, None)
unreal.log('GAP probe ' + repr(_r) + ' | ' + ' ; '.join(f'{type(v).__name__}:{v}' for v in (_r if isinstance(_r, tuple) else (_r,))))
try:
    hr = pick(_r, unreal.GeometryScriptRayHitResult); unreal.log('GAP probe fields ' + str({k: str(hr.get_editor_property(k)) for k in ('hit', 'ray_parameter', 'hit_triangle_id', 'hit_position')}))
except Exception as ex: unreal.log('GAP probe err ' + str(ex))
n = int(2 * HALF / STEP) + 1
grid = []
stats = {'samples': 0, 'max_sink': 0.0, 'max_float': 0.0, 'over5': 0, 'over10': 0, 'over20': 0}
for j in range(n):
    y = -HALF + j * STEP; row = []
    for i in range(n):
        x = -HALF + i * STEP
        hv, hc = height(vis, BV, x, y), height(col, BC, x, y)
        if hv is None or hc is None: row.append(None); continue
        d = hv - hc                               # > 0: you see ground ABOVE where your feet stand (legs sink)
        row.append(round(d, 1)); stats['samples'] += 1
        stats['max_sink'] = max(stats['max_sink'], d); stats['max_float'] = min(stats['max_float'], d)
        a = abs(d)
        stats['over5'] += a > 5; stats['over10'] += a > 10; stats['over20'] += a > 20
    grid.append(row)
info.update(stats)
p = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())) / 'GroundFix'
p.mkdir(parents=True, exist_ok=True)
(p / f'terrain_gap_{TAG}.json').write_text(json.dumps({'info': info, 'half': HALF, 'grid': grid}))
unreal.log('GAP_DONE ' + json.dumps(info))
if os.environ.get('RPG_NO_QUIT') != '1': unreal.SystemLibrary.quit_editor()
