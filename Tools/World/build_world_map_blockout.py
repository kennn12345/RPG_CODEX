"""Build the isolated, editable 14x9 km world-map Landscape. Refuses overwrites.
Requires the editor-only RPG.Editor.ImportWorldMapBlockout helper, and prepared R16.
Run with UnrealEditor -ExecutePythonScript=<this file> -unattended -RenderOffscreen.
No startup, existing map, gameplay, or environment assets are changed.
"""
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
import csv
import json
import struct
from pathlib import Path

MAP = '/Game/RPG/Maps/L_WorldMap_Blockout'
ASSETS = '/Game/RPG/Environment/WorldMap'
SOURCE = Path(unreal.Paths.project_dir()) / 'Tools/World/WorldMapSource'
REPORT = Path(unreal.Paths.project_saved_dir()) / 'WorldMapBlockout'
REPORT.mkdir(parents=True, exist_ok=True)
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if EAL.does_asset_exist(MAP):
    raise RuntimeError('World map already exists; refusing to replace it')
assert (SOURCE / 'heightmap.r16').stat().st_size == 4033*2521*2

def material(name, color, roughness):
    path = ASSETS + '/' + name
    if EAL.does_asset_exist(path):
        return unreal.load_asset(path)
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, ASSETS, unreal.Material, unreal.MaterialFactoryNew())
    col = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector)
    col.set_editor_property('constant', unreal.LinearColor(*color, 1))
    MEL.connect_material_property(col, '', unreal.MaterialProperty.MP_BASE_COLOR)
    rough = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant)
    rough.set_editor_property('r', roughness)
    MEL.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat

terrain_mat = material('M_WorldMap_Blockout', (0.23, 0.32, 0.12), 0.95)
water_mat = material('M_WorldMap_Water_Blockout', (0.025, 0.16, 0.24), 0.24)
assert les.new_level(MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world, 'RPG.Editor.ImportWorldMapBlockout')
lands = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.Landscape)]
assert len(lands) == 1, 'Landscape import did not complete'
land = lands[0]
land.set_folder_path('Terrain')

def spawn(cls, label, xyz=(0,0,0), rot=None):
    a = eas.spawn_actor_from_class(cls, unreal.Vector(*xyz), rot or unreal.Rotator())
    assert a
    a.set_actor_label(label)
    a.set_actor_location(unreal.Vector(*xyz), False, False)
    if rot:
        a.set_actor_rotation(rot, False)
    return a

# One zero-collision preview water sheet. Positive terrain occludes it; negative
# ocean and lake beds expose it. Water gameplay is deliberately a separate pass.
water = spawn(unreal.StaticMeshActor, 'Water_SeaLevel_0m_Blockout')
water.set_folder_path('Water')
wc = water.get_component_by_class(unreal.StaticMeshComponent)
wc.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Plane'))
wc.set_material(0, water_mat)
wc.set_collision_profile_name('NoCollision')
wc.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
wc.set_cast_shadow(False)
water.set_actor_scale3d(unreal.Vector(14000, 9000, 1))
water.tags = ['WorldMapBlockout', 'PreviewWater_NoGameplay']

sun = spawn(unreal.DirectionalLight, 'Blockout_Sun', (0,0,100000), unreal.Rotator(pitch=-50, yaw=-35))
sun.set_folder_path('Lighting')
light = sun.get_component_by_class(unreal.DirectionalLightComponent)
light.set_mobility(unreal.ComponentMobility.MOVABLE)
light.set_editor_property('intensity', 5.0)
light.set_editor_property('atmosphere_sun_light', True)
sky = spawn(unreal.SkyAtmosphere, 'Blockout_SkyAtmosphere')
sky.set_folder_path('Lighting')
ambient = spawn(unreal.SkyLight, 'Blockout_SkyLight', (0,0,10000))
ambient.set_folder_path('Lighting')
sc = ambient.get_component_by_class(unreal.SkyLightComponent)
sc.set_mobility(unreal.ComponentMobility.MOVABLE)
sc.set_editor_property('real_time_capture', True)
sc.set_editor_property('intensity', 1.0)

raw = (SOURCE / 'heightmap.r16').read_bytes()
def sample_height_cm(x,y):
    px = max(0, min(4032, round((x+700000)/347.22222222222223)))
    py = max(0, min(2520, round((y+450000)/357.14285714285717)))
    return (struct.unpack_from('<H', raw, (py*4033+px)*2)[0]-32768)/128*100

settlements = []
with (SOURCE / 'settlements.csv').open(newline='') as f:
    for row in csv.DictReader(f):
        x,y = float(row['UnrealX_cm']),float(row['UnrealY_cm'])
        z = sample_height_cm(x,y)
        marker = spawn(unreal.TargetPoint, row['Name'] + '_Location', (x,y,z+100))
        marker.set_folder_path('Planning/SettlementLocations')
        marker.set_editor_property('is_editor_only_actor', True)
        marker.tags = ['WorldMapSettlement', row['Class'], 'ProvisionalLocation']
        settlements.append(dict(name=row['Name'], xyz_cm=[x,y,z], classification=row['Class']))
start_at = settlements[0]['xyz_cm']
start = spawn(unreal.PlayerStart, 'WorldMap_PlayerStart_Frosthaven', (start_at[0],start_at[1],start_at[2]+200))
start.set_folder_path('Gameplay')

# Store useful views in the map without capturing review images.
for label, loc, rot in (
    ('Overview_NorthUp', (0,0,1500000), unreal.Rotator(pitch=-90, yaw=-90)),
    ('Overview_Angled', (-100000,-900000,1000000), unreal.Rotator(pitch=-52,yaw=83)),
):
    camera = spawn(unreal.CameraActor, label, loc, rot)
    camera.set_folder_path('Planning/Cameras')
    camera.get_component_by_class(unreal.CameraComponent).set_field_of_view(90)
unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
    unreal.Vector(-100000,-900000,1000000), unreal.Rotator(pitch=-52,yaw=83))

ticks = 0
def finish(delta):
    global ticks
    ticks += 1
    if ticks < 120:
        return
    unreal.unregister_slate_post_tick_callback(handle)
    try:
        center, extent = land.get_actor_bounds(False)
        bounds = [extent.x*2, extent.y*2]
        assert abs(bounds[0]-1400000) < 2 and abs(bounds[1]-900000) < 2, bounds
        assert abs(center.x)<1 and abs(center.y)<1, [center.x,center.y]
        assert len(settlements)==19
        assert les.save_current_level()
        report = dict(map=MAP, extent_cm=bounds, center_cm=[center.x,center.y,center.z],
            resolution=[4033,2521], expected_components=640, settlements=settlements,
            sea_level_cm=0, water_collision=False, saved=True,
            scope='Land/water terrain blockout. Final mountains, rivers, towns and dressing are future work.')
        (REPORT/'build_report.json').write_text(json.dumps(report,indent=2))
        unreal.log('WORLD_MAP_BUILD_SAVED '+json.dumps(report))
    except Exception as ex:
        unreal.log_error('WORLD_MAP_BUILD_FAILED '+str(ex))
    unreal.SystemLibrary.quit_editor()
handle = unreal.register_slate_post_tick_callback(finish)
