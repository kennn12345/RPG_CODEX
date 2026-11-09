"""Read-only: dump L_WorldVista ground/water/rock facts to Saved/GroundFix/inspect.json, then quit.
Run: UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="<repo>/Tools/World/inspect_vista_ground.py" -unattended -RenderOffscreen"""
import unreal, json
from pathlib import Path
out = {}
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level('/Game/RPG/Maps/L_WorldVista')
SMEL = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) if hasattr(unreal, 'StaticMeshEditorSubsystem') else None

def mesh_info(sm):
    d = {'path': sm.get_path_name()}
    try:
        b = sm.get_bounds(); d['bounds_origin'] = [b.origin.x, b.origin.y, b.origin.z]; d['bounds_extent'] = [b.box_extent.x, b.box_extent.y, b.box_extent.z]
    except Exception as e: d['bounds_err'] = str(e)
    try:
        ns = sm.get_editor_property('nanite_settings')
        d['nanite'] = {k: str(ns.get_editor_property(k)) for k in ('enabled', 'fallback_percent_triangles', 'fallback_relative_error', 'keep_percent_triangles') if True}
    except Exception as e: d['nanite_err'] = str(e)
    try:
        bs = sm.get_editor_property('body_setup')
        d['collision_trace_flag'] = str(bs.get_editor_property('collision_trace_flag'))
        agg = bs.get_editor_property('agg_geom')
        d['simple'] = {k: len(agg.get_editor_property(k)) for k in ('convex_elems', 'box_elems', 'sphere_elems', 'sphyl_elems')}
    except Exception as e: d['body_err'] = str(e)
    try:
        d['lod0_tris'] = SMEL.get_number_triangles(sm, 0) if SMEL else None   # may be fallback for Nanite
    except Exception as e: d['tri_err'] = str(e)
    return d

terrain = unreal.load_asset('/Game/RPG/Environment/World/Meshes/SM_Vista_Terrain')
out['terrain'] = mesh_info(terrain)
out['far'] = mesh_info(unreal.load_asset('/Game/RPG/Environment/World/Meshes/SM_Vista_Far'))

water, rocks, other = [], [], {}
for a in eas.get_all_level_actors():
    lab = a.get_actor_label(); cls = a.get_class().get_name()
    other[cls] = other.get(cls, 0) + 1
    if cls in ('RPGWaterBody', 'RPGStylizedRiver'):
        e = {'label': lab, 'class': cls, 'loc': [a.get_actor_location().x, a.get_actor_location().y, a.get_actor_location().z]}
        try:
            z = a.get_editor_property('zone'); ext = z.get_editor_property('box_extent') if hasattr(z, 'box_extent') else z.get_unscaled_box_extent()
            e['zone_extent'] = [ext.x, ext.y, ext.z]
        except Exception as ex: e['zone_err'] = str(ex)
        try:
            s = a.get_editor_property('surface_scale'); e['surface_scale'] = [s.x, s.y, s.z]
        except Exception: pass
        water.append(e)
    if lab.startswith(('Rocks_', 'Pebbles_', 'ShoreRocks_')):
        try:
            rocks.append({'label': lab, 'mesh': a.get_editor_property('mesh').get_name(), 'placed': a.get_editor_property('placed'),
                          'collision': a.get_editor_property('collision'), 'sink': a.get_editor_property('sink_depth')})
        except Exception as ex: rocks.append({'label': lab, 'err': str(ex)})
    if lab == 'Terrain':
        smc = a.static_mesh_component
        out['terrain_actor'] = {'loc': [a.get_actor_location().x, a.get_actor_location().y, a.get_actor_location().z],
                                'scale': [a.get_actor_scale3d().x, a.get_actor_scale3d().y, a.get_actor_scale3d().z],
                                'collision_profile': str(smc.get_collision_profile_name())}
out['water'] = water; out['rocks'] = rocks; out['class_counts'] = other
out['geometry_script'] = [n for n in dir(unreal) if n.startswith('GeometryScript') and 'Library' in n][:40]
layout = json.load(open('C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs/World_R01/exports/vista_layout.json'))
out['layout_keys'] = list(layout.keys())
out['layout_lake'] = layout.get('lake'); out['layout_lake_z'] = layout.get('lake_z'); out['layout_ponds'] = layout.get('ponds')
out['layout_rivers'] = [{'name': r['name'], 'width': r.get('width'), 'n': len(r['points'])} for r in layout.get('rivers', [])]
p = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())) / 'GroundFix'
p.mkdir(parents=True, exist_ok=True)
(p / 'inspect.json').write_text(json.dumps(out, indent=1, default=str))
unreal.log('GROUND_INSPECT_DONE')
unreal.SystemLibrary.quit_editor()
