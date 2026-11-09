"""Read-only: find what makes rocks see-through at corners -> Saved/GroundFix/rocks_<tag>.json.
Per rock (the 21 library meshes): open border loops (holes), connected pieces, and inward-facing ("flipped") triangles -
a triangle whose front side points into the rock is culled from outside, which reads as a see-through hole. Tested by
the fast winding number just in front of each triangle: inside the solid there means the triangle faces inward.
Env RPG_ROCK_TAG (default 'before'), RPG_NO_QUIT=1 to keep the editor open."""
import unreal, json, os, math
from pathlib import Path
TAG = os.environ.get('RPG_ROCK_TAG', 'before')
M = '/Game/RPG/Environment/Rocks/Meshes/'
NAMES = [f'Rock_{c}_{v}' for c in ('Large', 'MediumLarge', 'Medium', 'MediumSmall', 'SmallMedium', 'Small') for v in 'ABC'] + [f'Pebbles_{v}' for v in 'ABC']

def pick(x, cls):
    for v in (x if isinstance(x, tuple) else (x,)):
        if isinstance(v, cls): return v

def load(sm):
    dm = unreal.DynamicMesh(); o = unreal.GeometryScriptCopyMeshFromAssetOptions(); r = unreal.GeometryScriptMeshReadLOD()
    r.set_editor_property('lod_type', unreal.GeometryScriptLODType.SOURCE_MODEL)
    unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(sm, dm, o, r)
    return dm

def inward_triangles(dm, sample_every=1):
    bvh = pick(unreal.GeometryScript_MeshSpatial.build_bvh_for_mesh(dm, None), unreal.GeometryScriptDynamicMeshBVH)
    wo = unreal.GeometryScriptSpatialQueryOptions()
    ids = []; n = dm.get_triangle_count()
    for t in range(0, n, sample_every):
        r3 = unreal.GeometryScript_MeshQueries.get_triangle_positions(dm, t)
        vs = [v for v in (r3 if isinstance(r3, tuple) else (r3,)) if isinstance(v, unreal.Vector)][:3]
        if len(vs) < 3: continue
        a, b, c = vs; e1 = b - a; e2 = c - a; nrm = e1.cross(e2); L = nrm.length()
        if L < 1e-6: continue
        nrm = nrm / L; cen = (a + b + c) / 3.0
        edge = max((b - a).length(), (c - b).length(), (a - c).length())
        probe = cen + nrm * max(0.5, edge * 0.25)
        res = unreal.GeometryScript_MeshSpatial.is_point_inside_mesh(dm, bvh, probe, wo)
        inside = res if isinstance(res, bool) else pick(res, bool)
        if inside: ids.append(t)
    return ids

out = {}
for name in NAMES:
    sm = unreal.load_asset(M + 'SM_' + name)
    if not sm: continue
    dm = load(sm)
    Q = unreal.GeometryScript_MeshQueries
    loops = Q.get_num_open_border_loops(dm); loops = loops[0] if isinstance(loops, tuple) else loops
    islands = Q.get_num_connected_components(dm)
    inward = inward_triangles(dm)
    b = sm.get_bounds()
    # where the inward triangles sit: share near the mesh's bounding-box corners/edges (outer 20% on 2+ axes)
    corner = 0
    for t in inward:
        r3 = Q.get_triangle_positions(dm, t); vs = [v for v in (r3 if isinstance(r3, tuple) else (r3,)) if isinstance(v, unreal.Vector)][:3]
        c = (vs[0] + vs[1] + vs[2]) / 3.0
        axes = sum(abs((c.x, c.y, c.z)[i] - (b.origin.x, b.origin.y, b.origin.z)[i]) > 0.6 * (b.box_extent.x, b.box_extent.y, b.box_extent.z)[i] for i in range(3))
        corner += axes >= 2
    tags = [str(t) for t in unreal.EditorAssetLibrary.get_metadata_tag_values(sm).keys()] if hasattr(unreal.EditorAssetLibrary, 'get_metadata_tag_values') else []
    out[name] = {'tris': dm.get_triangle_count(), 'open_loops': loops, 'islands': islands, 'inward_tris': len(inward), 'inward_near_corners': corner,
                 'extent': [round(b.box_extent.x), round(b.box_extent.y), round(b.box_extent.z)], 'nanite': sm.get_editor_property('nanite_settings').get_editor_property('enabled')}
    unreal.log(f'ROCK {name} tris={dm.get_triangle_count()} holes={loops} islands={islands} inward={len(inward)} (near corners {corner})')
p = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())) / 'GroundFix'
p.mkdir(parents=True, exist_ok=True); (p / f'rocks_{TAG}.json').write_text(json.dumps(out, indent=1))
unreal.log('ROCKS_DONE')
if os.environ.get('RPG_NO_QUIT') != '1': unreal.SystemLibrary.quit_editor()
