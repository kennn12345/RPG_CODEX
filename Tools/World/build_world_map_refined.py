"""Create a separate refined map from v1 without saving over the open blockout.
The original native import helper is used in a private editor world. Its source
file is swapped only for the synchronous import/check and restored in finally.
"""
import unreal
import json, struct, shutil, hashlib, traceback
from pathlib import Path
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
ROOT=Path(unreal.Paths.project_dir())
SOURCE=ROOT/'Tools/World/WorldMapSourceV2'
OLD_SOURCE=ROOT/'Tools/World/WorldMapSource/heightmap.r16'
REPORT=Path(unreal.Paths.project_saved_dir())/'WorldMapRefined'
REPORT.mkdir(exist_ok=True,parents=True)
MAP='/Game/RPG/Maps/L_WorldMap_Refined'
EAL=unreal.EditorAssetLibrary
les=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ues=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert not EAL.does_asset_exist(MAP), 'Refined map already exists; refusing overwrite'
meta=json.loads((SOURCE/'refinement.json').read_text())
original_map=ROOT/'Content/RPG/Maps/L_WorldMap_Blockout.umap'
shutil.copy2(original_map,REPORT/'L_WorldMap_Blockout_before_refinement.umap')
original_map_hash=hashlib.sha256(original_map.read_bytes()).hexdigest()
assert les.load_level('/Game/RPG/Maps/L_WorldMap_Blockout')
world=ues.get_editor_world()
old_raw=OLD_SOURCE.read_bytes()
raw=(SOURCE/'heightmap.r16').read_bytes()
old_sha=hashlib.sha256(old_raw).hexdigest()
for a in list(eas.get_all_level_actors()):
    if isinstance(a,unreal.Landscape):
        eas.destroy_actor(a)
try:
    OLD_SOURCE.write_bytes(raw)
    unreal.SystemLibrary.execute_console_command(world,'RPG.Editor.ImportWorldMapBlockout')
finally:
    OLD_SOURCE.write_bytes(old_raw)
assert hashlib.sha256(OLD_SOURCE.read_bytes()).hexdigest()==old_sha
land=next(a for a in eas.get_all_level_actors() if isinstance(a,unreal.Landscape))
land.set_actor_label('WorldMap_Refined_Terrain_14km_x_9km')
land.set_folder_path('Terrain')

def sample(x,y):
    px=max(0,min(4032,round((x+700000)/347.22222222222223)))
    py=max(0,min(2520,round((y+450000)/357.14285714285717)))
    return (struct.unpack_from('<H',raw,(py*4033+px)*2)[0]-32768)/128*100
markers=[]
for a in eas.get_all_level_actors():
    if 'WorldMapSettlement' in [str(t) for t in a.tags]:
        p=a.get_actor_location()
        z=sample(p.x,p.y)+100
        # Keep source XY, but do not hide an editor planning marker below water.
        a.set_actor_location(unreal.Vector(p.x,p.y,max(100,z)),False,False)
        markers.append(a.get_actor_label())
    if isinstance(a,unreal.PlayerStart):
        p=a.get_actor_location()
        a.set_actor_location(unreal.Vector(p.x,p.y,sample(p.x,p.y)+200),False,False)

water_mat=unreal.load_asset('/Game/RPG/Environment/WorldMap/M_WorldMap_Water_Blockout')
river_path='/Game/RPG/Environment/WorldMap/M_WorldMap_River_Refined'
river_mat=unreal.load_asset(river_path) if EAL.does_asset_exist(river_path) else EAL.duplicate_asset('/Game/RPG/Environment/WorldMap/M_WorldMap_Water_Blockout',river_path)
river_mat.set_editor_property('used_with_spline_meshes',True)
unreal.MaterialEditingLibrary.recompile_material(river_mat)
assert EAL.save_loaded_asset(river_mat)
plane=unreal.load_asset('/Engine/BasicShapes/Plane')
strip=unreal.load_asset('/Game/RPG/Environment/Water/SM_Water_River_Strip10')
if not strip:
    # Asset path in the existing water library is discovered explicitly.
    candidates=[p for p in EAL.list_assets('/Game/RPG',recursive=True) if p.split('/')[-1].split('.')[0]=='SM_Water_River_Strip10']
    assert len(candidates)==1,candidates
    strip=unreal.load_asset(candidates[0])
x0,y0,x1,y1=meta['lake_bounds_cm']
lake=eas.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector((x0+x1)/2,(y0+y1)/2,meta['lake_level_m']*100))
lake.set_actor_label('Lake_Basin_Water_22m')
lake.set_folder_path('Water/Lake')
lake.set_actor_scale3d(unreal.Vector((x1-x0)/100+20,(y1-y0)/100+20,1))
lc=lake.get_component_by_class(unreal.StaticMeshComponent)
lc.set_static_mesh(plane);lc.set_material(0,water_mat)
lc.set_collision_profile_name('NoCollision')
lc.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
lc.set_cast_shadow(False)
lake.tags=['WorldMapRefinedWater','Lake']
rivers=[]
for rv in meta['rivers']:
    actor=eas.spawn_actor_from_class(unreal.RPGStylizedRiver,unreal.Vector())
    actor.set_actor_label(rv['name']);actor.set_folder_path('Water/Rivers')
    actor.tags=['WorldMapRefinedWater','River']
    for key,val in (('water_mesh',strip),('water_material',river_mat),('width',rv['width_cm']),('piece_length',7500.0),('create_water_zones',False)):
        actor.set_editor_property(key,val)
    spline=actor.get_editor_property('spline')
    spline.clear_spline_points(False)
    for p in rv['points_cm']:
        spline.add_spline_point(unreal.Vector(*p),unreal.SplineCoordinateSpace.LOCAL,False)
    spline.update_spline()
    for piece in actor.get_components_by_class(unreal.SplineMeshComponent):
        piece.destroy_component(actor)
    actor.rebuild()
    rivers.append(actor)

count=0
def finish(delta):
    global count
    count+=1
    if count<150:return
    unreal.unregister_slate_post_tick_callback(handle)
    try:
        native_path=Path(unreal.Paths.project_saved_dir())/'WorldMapBlockout/height_verification.json'
        native_before=native_path.read_bytes() if native_path.exists() else None
        try:
            OLD_SOURCE.write_bytes(raw)
            unreal.SystemLibrary.execute_console_command(world,'RPG.Editor.VerifyWorldMapBlockout')
            check=json.loads(native_path.read_text(encoding='utf-8-sig'))
            assert check['mismatches']==0 and check['components']==640,check
        finally:
            OLD_SOURCE.write_bytes(old_raw)
            if native_before is not None:native_path.write_bytes(native_before)
        assert hashlib.sha256(original_map.read_bytes()).hexdigest()==original_map_hash
        assert unreal.EditorLoadingAndSavingUtils.save_map(world,MAP)
        assert hashlib.sha256(original_map.read_bytes()).hexdigest()==original_map_hash
        report=dict(map=MAP,original_map_unchanged=True,original_source_unchanged=hashlib.sha256(OLD_SOURCE.read_bytes()).hexdigest()==old_sha,
            height_check=check,river_count=len(rivers),river_length_m=sum(r['length_m'] for r in meta['rivers']),settlements=len(markers),lake_level_m=meta['lake_level_m'],saved=True)
        (REPORT/'build_report.json').write_text(json.dumps(report,indent=2))
        unreal.log('WORLD_MAP_REFINED_SAVED '+json.dumps(report))
    except Exception:
        unreal.log_error('WORLD_MAP_REFINED_FAILED '+traceback.format_exc())
    unreal.SystemLibrary.quit_editor()
handle=unreal.register_slate_post_tick_callback(finish)
