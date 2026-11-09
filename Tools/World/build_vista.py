"""Builds /Game/RPG/Maps/L_WorldVista: the "hero vista" slice of the stylized world (style target: golden-hour fantasy
forest of the Dreamscape: Tower video - original content; no floating chained rocks, no tower, no birds yet - per the user).
R02: day/night cycle (ARPGSkyCycle + sky dome: big sun/moon, stars, Milky Way - approved sky concept 10), the stylized
water (Docs/WATER_UNREAL_HANDOFF.md) on the lake, 4 ponds and 2 rivers, far hills + mountains, stacked cliffs with ivy,
round fluffy trees, lake mist, pollen motes / fireflies and drifting leaves.
LAYOUT ONLY (2026-11-03): assets come from Tools/World/build_vista_assets.py (run that first); this script only places them.
Run in the full editor (saves and quits):
  UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/build_vista.py" -unattended -RenderOffscreen
Photograph: -ExecCmds="Automation RunTests Visual.WorldShots" (WorldShot cameras) / Visual.SkyShots (SkyShot cameras x 5 hours)."""
import unreal, math, json, os

SRC = 'C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs'
EXP = SRC + '/World_R01/exports'
ROOT = '/Game/RPG/Environment/World'
MESHES, MATS, FOLIAGE = ROOT + '/Meshes', ROOT + '/Materials', ROOT + '/Foliage'   # reusable assets, by kind
WATER = '/Game/RPG/Environment/Water'
MAP = '/Game/RPG/Maps/L_WorldVista'
EAL, MEL = unreal.EditorAssetLibrary, unreal.MaterialEditingLibrary
LAYOUT = json.load(open(EXP + '/vista_layout.json'))
YS = -1.0                               # the FBX importer mirrors Blender Y (see Docs/WATER_UNREAL_HANDOFF.md)
def U(x, y): return (x * 100.0, YS * y * 100.0)   # Blender metres -> Unreal cm (XY)

def lin(c): return [((v / 255) / 12.92) if v / 255 <= .04045 else ((v / 255 + .055) / 1.055) ** 2.4 for v in c]

# ------------------------------------------------------------------ assets (made by build_vista_assets.py, only loaded here)
# This script is LAYOUT ONLY: it never creates, imports or deletes an asset. Run Tools/World/build_vista_assets.py first
# (it updates every mesh / material / foliage type in place; the map picks the changes up without a rebuild).
def need(path):
    a = unreal.load_asset(path) if EAL.does_asset_exist(path) else None
    if not a: raise RuntimeError(f'missing asset {path} - run Tools/World/build_vista_assets.py first')
    return a
M_ = {n: need(f'{MESHES}/{n}') for n in ('SM_Vista_Terrain', 'SM_Vista_Far', 'SM_Vista_Grass', 'SM_Vista_GrassTall', 'SM_Vista_Poppies',
                                          'SM_Vista_Tree_Far', 'SM_Vista_Motes', 'SM_Vista_DriftLeaves', 'SM_Vista_Cliff_A', 'SM_Vista_Cliff_B',
                                          'SM_Vista_Tree_Round', 'SM_Vista_Tree_Tall', 'SM_Vista_Tree_Wide', 'SM_Vista_Wheat', 'SM_Vista_Daisies')}
TREE_ANCIENT = need(f'{MESHES}/SM_Tree_Ancient')
PLANE = need(f'{MESHES}/SM_Vista_MistSheet')
MMIST, MSKY = need(f'{MATS}/M_Vista_Mist'), need(f'{MATS}/M_Vista_Sky')
def ROCK(n): return need(f'/Game/RPG/Environment/Rocks/Meshes/SM_{n}')

# ------------------------------------------------------------------ level
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem); eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if EAL.does_asset_exist(MAP):
    les.load_level(MAP)
    for a in eas.get_all_level_actors():
        if not isinstance(a, (unreal.WorldSettings, unreal.Brush)): eas.destroy_actor(a)
else:
    les.new_level(MAP)
world = unreal.EditorLevelLibrary.get_editor_world()

def spawn(what, loc, rot=(0, 0, 0), label=None, scale=None):
    r = unreal.Rotator(roll=rot[2], pitch=rot[1], yaw=rot[0])
    a = eas.spawn_actor_from_class(what, unreal.Vector(*loc), r) if isinstance(what, type) else eas.spawn_actor_from_object(what, unreal.Vector(*loc), r)
    if label: a.set_actor_label(label)
    if scale: a.set_actor_scale3d(unreal.Vector(*scale))
    return a

def setp(obj, props, what):
    for prop, val in props:
        try: obj.set_editor_property(prop, val)
        except Exception as ex: unreal.log_warning(f'{what} {prop}: {ex}')

# ---- sky + lights: ARPGSkyCycle drives sun / moon / sky light / haze / exposure through one continuous day
def dir_light(label, index, shadows=True):
    a = spawn(unreal.DirectionalLight, (0, 0, 3000), (0, -45, 0), label)
    a.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    c = a.get_component_by_class(unreal.DirectionalLightComponent)
    setp(c, (('atmosphere_sun_light', True), ('atmosphere_sun_light_index', index), ('cast_shadows', shadows),
             ('light_source_angle', 1.5), ('cast_cloud_shadows', True)), label)
    return a
sun = dir_light('Sun', 0); moon = dir_light('Moon', 1)
setp(sun.get_component_by_class(unreal.DirectionalLightComponent), (('enable_light_shaft_bloom', False), ('bloom_scale', 0.35)), 'sun')   # on, the big sun disc behind the hills made a pale cone and washed out the low-sun sky
# Sun wins forward shading (water, translucency, volumetric fog): with Sun and Moon both at priority 0 Unreal warns
# "multiple directional lights are competing" (user 2026-11-05)
setp(sun.get_component_by_class(unreal.DirectionalLightComponent), (('forward_shading_priority', 1),), 'sun')
atm = spawn(unreal.SkyAtmosphere, (0, 0, 0), label='SkyAtmosphere')
# aerial perspective starts beyond the sky dome (30 km): it would otherwise filter the dome's blue to black; height fog does the haze
setp(atm.get_component_by_class(unreal.SkyAtmosphereComponent), (('aerial_perspective_start_depth', 100.0), ('mie_scattering_scale', 0.006)), 'atmosphere')
clouds = spawn(unreal.VolumetricCloud, (0, 0, 0), label='Clouds')
cc_ = clouds.get_component_by_class(unreal.VolumetricCloudComponent)
setp(cc_, (('layer_bottom_altitude', 2.2), ('layer_height', 4.5)), 'clouds')
cc_.set_editor_property('material', need(f'{MATS}/MI_Vista_Clouds'))   # frozen sky look (made once by the R03 builder)
sky = spawn(unreal.SkyLight, (0, 0, 800), label='SkyLight')
sky.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
setp(sky.get_component_by_class(unreal.SkyLightComponent), (('real_time_capture', True), ('intensity', 1.35)), 'skylight')
fog = spawn(unreal.ExponentialHeightFog, (0, 0, -300), label='HazeFog')
setp(fog.get_component_by_class(unreal.ExponentialHeightFogComponent),
     (('fog_density', 0.007), ('fog_height_falloff', 0.1), ('start_distance', 2500.0), ('fog_max_opacity', 0.8),
      ('fog_cutoff_distance', 2.0e6),                                     # no haze on the sky dome (30 km): keeps the sky blue

      ('directional_inscattering_exponent', 6.0), ('enable_volumetric_fog', True), ('volumetric_fog_scattering_distribution', 0.6)), 'fog')
pp = spawn(unreal.PostProcessVolume, (0, 0, 0), label='Grade'); pp.set_editor_property('unbound', True)
s = pp.get_editor_property('settings')
for prop, val in (('auto_exposure_bias', 0.15), ('auto_exposure_min_brightness', -1.5), ('auto_exposure_max_brightness', 14.0),
                  ('bloom_intensity', 1.1), ('color_saturation', unreal.Vector4(1.15, 1.15, 1.15, 1.0)),
                  ('color_contrast', unreal.Vector4(1.05, 1.05, 1.05, 1.0)), ('vignette_intensity', 0.3)):
    try:
        s.set_editor_property('override_' + prop, True); s.set_editor_property(prop, val)
    except Exception as ex: unreal.log_warning(f'grade {prop}: {ex}')
pp.set_editor_property('settings', s)
dome_mesh = unreal.load_asset('/Engine/EngineSky/SM_SkySphere') or unreal.load_asset('/Engine/BasicShapes/Sphere')
dome = spawn(dome_mesh, (0, 0, 0), label='SkyDome')
dsm = dome.static_mesh_component
dsm.set_material(0, MSKY); setp(dsm, (('cast_shadow', False), ('bounds_scale', 10.0)), 'dome'); dsm.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
db = dome_mesh.get_bounds().box_extent.x
dome.set_actor_scale3d(unreal.Vector(*(3.0e6 / max(db, 1.0),) * 3))                  # ~30 km radius, beyond the far mountains
cyc = spawn(unreal.RPGSkyCycle, (0, 0, 0), label='SkyCycle')
for k, v in (('sun', sun), ('moon', moon), ('sky_light', sky), ('fog', fog), ('grade', pp), ('time_of_day', 17.3),
             ('sunrise_yaw', 140.0), ('day_length_minutes', 120.0), ('moon_intensity', 0.25), ('exposure_bias_night', -2.5), ('sky_light_day', 0.9), ('sun_intensity', 9.0),
             ('sun_color_low', unreal.LinearColor(1.0, 0.55, 0.28, 1)), ('haze_low', unreal.LinearColor(0.48, 0.32, 0.24, 1)), ('sky_light_night', 1.0)):
    cyc.set_editor_property(k, v)
cyc.set_time_of_day(17.3)

# terrain + far hills / mountains
spawn(M_['SM_Vista_Terrain'], (0, 0, 0), label='Terrain')
spawn(M_['SM_Vista_Far'], (0, 0, 0), label='FarLand')

def ground(x, y, default=0.0):
    hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, 20000), unreal.Vector(x, y, -20000),
                                                 unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [], unreal.DrawDebugTrace.NONE, True)
    t = hit.to_tuple() if hit else None
    if not t or not t[0]: return default
    for v in t:
        if isinstance(v, unreal.Vector) and abs(v.z) < 19999: return v.z
    return default

# ---- water: the stylized water from L_WaterTest (same materials, waves, underwater look, caustics)
MI_RIVER, MI_LAKE = unreal.load_asset(f'{WATER}/MI_StylizedWater_River'), unreal.load_asset(f'{WATER}/MI_StylizedWater_Lake')
SM_LAKE, SM_STRIP = unreal.load_asset(f'{WATER}/SM_Water_Lake'), unreal.load_asset(f'{WATER}/SM_Water_River_Strip10')
M_UNDER, M_CAUST = unreal.load_asset(f'{WATER}/M_UnderwaterPP'), unreal.load_asset(f'{WATER}/M_WaterCaustics')
WAVES = {'River': (2.0, 400.0, 0.8, 0.0), 'Lake': (3.0, 550.0, 0.8, 30.0)}             # MUST match the MI wave params (build_water_test.py)
def waves(kind):
    s_ = unreal.RPGWaveSettings(); s_.height, s_.length, s_.speed, s_.direction = WAVES[kind]; return s_
LAKE_EXT = SM_LAKE.get_bounds().box_extent.x

def water_body(label, cx, cy, r, z, half_depth=300.0):
    """zone box (top = the waterline, 2 x half_depth deep - deeper than the bed) + the surface mesh."""
    X, Y = U(cx, cy); Z = z * 100.0
    b_ = spawn(unreal.RPGWaterBody, (X, Y, Z - half_depth), label=label)
    zone = b_.get_editor_property('zone')
    zone.set_box_extent(unreal.Vector(r * 100 * 1.6, r * 100 * 1.6, half_depth), True)
    setp(zone, (('waves', waves('Lake')), ('underwater_material', M_UNDER), ('caustic_material', M_CAUST)), label)
    sc_ = r * 100 * 1.6 / max(LAKE_EXT, 1.0)
    b_.set_editor_property('surface_scale', unreal.Vector(sc_, sc_, 1))
    b_.set_editor_property('water_mesh', SM_LAKE); b_.set_editor_property('water_material', MI_LAKE)
    return b_
lx, ly, lr = LAYOUT['lake']; LX, LY = U(lx, ly); LAKE_Z = LAYOUT['lake_z'] * 100.0
water_body('Lake', lx, ly, lr, LAYOUT['lake_z'])
for i, (px, py, pr, pz) in enumerate(LAYOUT['ponds']):
    if pr >= 10: water_body('Pool_Deep', px, py, pr, pz, 450.0)      # 5 m deep swimming hole (walk on the bottom)
    else: water_body(f'Pond_{i + 1}', px, py, pr, pz)
for rv in LAYOUT['rivers']:
    river = spawn(unreal.RPGStylizedRiver, (0, 0, 0), label=rv['name'])
    for k, v in (('water_mesh', SM_STRIP), ('water_material', MI_RIVER), ('width', 800.0), ('piece_length', 600.0), ('waves', waves('River')),
                 ('flow_speed', 90.0), ('underwater_material', M_UNDER), ('caustic_material', M_CAUST), ('zone_depth', 250.0)):
        river.set_editor_property(k, v)
    spl = river.get_editor_property('spline'); spl.clear_spline_points(True)
    for i, (x, y, z) in enumerate(rv['points']):
        spl.add_spline_point(unreal.Vector(x * 100, YS * y * 100, z * 100), unreal.SplineCoordinateSpace.LOCAL, True)
        spl.set_scale_at_spline_point(i, unreal.Vector(1, (rv['width'] + 0.8) / 8.0, 1), True)   # a little wider than the channel: hidden in the banks
    river.rebuild()

# mist sheets over the lake and ponds
for i, (cx, cy, r, z) in enumerate([(lx, ly, lr, LAYOUT['lake_z'])] + [tuple(p) for p in LAYOUT['ponds']]):
    X, Y = U(cx, cy)
    for k, (dz, s_) in enumerate(((30, 1.25), (90, 1.05))):
        mst = spawn(PLANE, (X, Y, z * 100 + dz), (k * 37, 0, 0), f'Mist_{i}_{k}', (r * s_, r * s_, 1))
        mst.static_mesh_component.set_material(0, MMIST); setp(mst.static_mesh_component, (('cast_shadow', False),), 'mist')
        mst.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)   # scatter traces must reach the ground

PATH = [unreal.Vector(*U(x, y), 0) for x, y in LAYOUT['path']]

def scatter(label, mesh, count, center=(0, 0), half=(9800, 9800), seed=1, scale=(0.8, 1.2), up=0.75, align=False, sink=0.0,
            path_r=0.0, patch=0.0, cover=0.5, collision=False, cull=0.0, material=None, margin=10.0, circles=(), shadow=True):
    a = spawn(unreal.RPGScatter, (center[0], center[1], 0), label=label)
    for k, v in (('mesh', mesh), ('count', count), ('half_extent', unreal.Vector2D(*half)), ('seed', seed), ('scale_range', unreal.Vector2D(*scale)),
                 ('min_up_normal', up), ('align_to_surface', align), ('sink_depth', sink), ('exclude_path', PATH), ('exclude_radius', path_r),
                 ('patch_size', patch), ('patch_coverage', cover), ('collision', collision), ('cull_distance', cull),
                 ('skip_water', True), ('water_margin', margin), ('exclude_circles', [unreal.Vector(*c_) for c_ in circles])):
        a.set_editor_property(k, v)
    if material: a.set_editor_property('material', material)
    a.get_editor_property('instances').set_editor_property('cast_shadow', shadow)   # small foliage: no shadows (VSM cost)
    a.rebuild(); unreal.log(f'SCATTER {label}: {a.get_editor_property("placed")}')
    return a

# ground cover first: its down-traces must hit terrain, not tree canopies or rocks (water zones already exist -> skipped)
scatter('Grass', M_['SM_Vista_Grass'], 60000, seed=21, scale=(0.8, 1.3), up=0.7, path_r=150, cull=9000, shadow=False)
scatter('GrassTall', M_['SM_Vista_GrassTall'], 14000, seed=22, scale=(0.8, 1.25), up=0.75, path_r=220, patch=2600, cover=0.5, cull=9000, shadow=False)
scatter('GrassShore', M_['SM_Vista_GrassTall'], 2500, (LX, LY), (lr * 170, lr * 170), 26, (0.9, 1.4), 0.7, False, 0, 0, 0, 0.5, False, 9000, None, 25.0, shadow=False)
WX, WY = U(-55, 22)
scatter('Wheat', M_['SM_Vista_Wheat'], 9000, (WX, WY), (2300, 1500), 23, (0.9, 1.3), 0.8, False, 0, 260, 0, 0.5, False, 9000, shadow=False)
scatter('Poppies', M_['SM_Vista_Poppies'], 12000, seed=24, scale=(1.1, 1.5), up=0.8, path_r=170, patch=1500, cover=0.4, cull=7000, shadow=False)
scatter('Daisies', M_['SM_Vista_Daisies'], 4000, seed=25, scale=(0.9, 1.2), up=0.8, path_r=160, patch=1100, cover=0.3, cull=6000, shadow=False)
# big things (collision on: trees and rocks block the player)
CA, CB = U(0, LAYOUT['cliff_y'] - 2)[1], U(0, LAYOUT['cliff_y'] + 8)[1]   # two rows along the ridge foot and slope
scatter('Cliffs_A', M_['SM_Vista_Cliff_A'], 9, (0, CA), (9500, 900), 3, (0.9, 1.4), 0.0, False, 120.0, 400, 0, 0.5, True)
scatter('Cliffs_B', M_['SM_Vista_Cliff_B'], 9, (0, CB), (9500, 900), 6, (0.8, 1.3), 0.0, False, 120.0, 400, 0, 0.5, True)
SPAWN_CLEAR = [(*U(-4, -72), 2400)]                                   # open ground at the player spawn + NPCs (add_vista_npcs.py)
# Blender rocks, every variant: (size class, count, scale, sink cm, path clearance cm, collision + shadow)
for k, (cls, count, sc, sink, pr, solid) in enumerate((('Large', 5, (0.8, 1.3), 30, 500, True), ('MediumLarge', 6, (0.8, 1.2), 20, 400, True),
                                                       ('Medium', 10, (0.8, 1.2), 12, 300, True), ('MediumSmall', 16, (0.8, 1.3), 6, 200, True),
                                                       ('SmallMedium', 30, (0.8, 1.4), 2, 120, False), ('Small', 30, (0.8, 1.5), 1, 100, False))):
    for j, v in enumerate('ABC'):
        scatter(f'Rocks_{cls}_{v}', ROCK(f'Rock_{cls}_{v}'), count, seed=60 + k * 3 + j, scale=sc, up=0.55, align=True, sink=sink, path_r=pr,
                collision=solid, shadow=solid, margin=-30.0, circles=SPAWN_CLEAR if solid else ())
for j, v in enumerate('ABC'):
    scatter(f'Pebbles_{v}', ROCK(f'Pebbles_{v}'), 25, seed=80 + j, scale=(0.9, 1.6), up=0.6, align=True, sink=1, path_r=60, shadow=False, margin=-30.0)
# stones along the lake shore
for j, v in enumerate('ABC'):
    scatter(f'ShoreRocks_{v}', ROCK(f'Rock_MediumSmall_{v}'), 8, (LX, LY), (lr * 150, lr * 150), 90 + j, (0.8, 1.4), 0.5, True, 6, 0, 0, 0.5, True,
            margin=-40.0)
spawn(TREE_ANCIENT, (LX - 2600, LY + YS * -900, ground(LX - 2600, LY + YS * -900) - 30), (40, 0, 0), 'Tree_Hero', (0.75, 0.75, 0.75))
# open ground around the lake and ponds so the water reads from the paths and cameras
OPEN = [(LX, LY, lr * 100 * 1.55)] + [(*U(px, py), pr * 100 * 1.9) for px, py, pr, pz in LAYOUT['ponds']]
for i, n in enumerate(('SM_Vista_Tree_Round', 'SM_Vista_Tree_Tall', 'SM_Vista_Tree_Wide')):
    scatter(f'Trees_{n[13:]}', M_[n], 26, seed=10 + i, scale=(0.85, 1.3), up=0.82, sink=15, path_r=450, patch=3500, cover=0.65, collision=True,
            margin=150.0, circles=OPEN)
# far forests on the surrounding hills (cheap blob trees, outside the vista square)
for i, (cx, cy_, hx, hy) in enumerate(((0, 28000, 45000, 17500), (0, -28000, 45000, 17500), (28000, 0, 17500, 10500), (-28000, 0, 17500, 10500))):
    scatter(f'FarTrees_{i}', M_['SM_Vista_Tree_Far'], 2500, (cx, cy_), (hx, hy), 40 + i, (1.0, 1.8), 0.8, False, 50, 0, 6000, 0.5, shadow=False)

# life: pollen / fireflies and drifting leaves (boxes 30 x 30 m around each actor)
for i, (x, y) in enumerate(((-3, -60), (lx, ly), (-20, 20))):
    X, Y = U(x, y); mo = spawn(M_['SM_Vista_Motes'], (X, Y, ground(X, Y)), (i * 50, 0, 0), f'Motes_{i}')
    mo.static_mesh_component.set_editor_property('cast_shadow', False)
for i, (x, y) in enumerate(((0, -45), (-20, 0), (30, -5))):
    X, Y = U(x, y); dl = spawn(M_['SM_Vista_DriftLeaves'], (X, Y, ground(X, Y)), (i * 70, 0, 0), f'DriftLeaves_{i}')
    dl.static_mesh_component.set_editor_property('cast_shadow', False)

# cameras (Visual.WorldShots / Visual.SkyShots)
def cam(label, eye_xy, eye_h, tgt_xy, tgt_h, fov=62.0, tag='WorldShot'):
    e = (eye_xy[0], eye_xy[1], ground(*eye_xy) + eye_h); t = (tgt_xy[0], tgt_xy[1], ground(*tgt_xy) + tgt_h)
    d = (t[0] - e[0], t[1] - e[1], t[2] - e[2])
    c = spawn(unreal.CameraActor, e, (math.degrees(math.atan2(d[1], d[0])), math.degrees(math.atan2(d[2], math.hypot(d[0], d[1]))), 0), label)
    c.tags = [unreal.Name(tag)]
    cc = c.get_component_by_class(unreal.CameraComponent); cc.set_editor_property('field_of_view', fov); cc.set_editor_property('constrain_aspect_ratio', False)
cam('WS_01_Path', U(-3, -75), 170, U(4, -20), 150)
cam('WS_02_Lake', U(20, -45), 300, U(lx, ly + 4), 0, 66)
cam('WS_03_Meadow', U(-32, -12), 90, U(-10, 28), 120, 58)
cam('WS_04_Overlook', U(-60, -95), 2300, U(10, 35), 0, 70)
cam('WS_05_Wheat', U(-30, 2), 160, U(-62, 25), 150, 60)
cam('WS_06_LakeShore', U(lx - 20, ly - 12), 120, U(lx + 8, ly + 6), 60, 70)
cam('WS_07_RiverA', U(73, 6), 260, U(60, 27), 0, 62)
cam('WS_08_PondWest', U(-70, 30), 180, U(-85, 44), 0, 64)
cam('WS_09_Wide', U(-20, -55), 900, U(40, 30), 300, 75)
DP = next(p for p in LAYOUT['ponds'] if p[2] >= 10)
cam('WS_15_DeepPool', U(DP[0] + 4, DP[1] + 19), 260, U(DP[0], DP[1] - 2), 0, 72)   # from behind the jump ledge
# aerial overviews of the whole vista
cam('WS_10_AerialSouth', U(-10, -150), 6000, U(10, 0), 0, 70)
cam('WS_11_AerialEast', U(150, -20), 5500, U(0, 10), 0, 70)
cam('WS_12_AerialNorthWest', U(-130, 110), 6500, U(10, -20), 0, 70)
cam('WS_13_TopDown', U(0, -40), 16000, U(0, -5), 0, 75)
# sky: one view toward the sunset / morning horizon, one up toward the night moon
SKY_EYE = U(20, -42)                                                   # open meadow south-west of the lake
cam('SKY_A_Sunset', SKY_EYE, 600, (SKY_EYE[0] + math.cos(math.radians(-40)) * 10000, SKY_EYE[1] + math.sin(math.radians(-40)) * 10000), 1500, 80, 'SkyShot')
cam('SKY_B_Overhead', SKY_EYE, 2500, (SKY_EYE[0] + math.cos(math.radians(230)) * 10000, SKY_EYE[1] + math.sin(math.radians(230)) * 10000), 9000, 85, 'SkyShot')
spawn(unreal.PlayerStart, (U(-4, -85)[0], U(-4, -85)[1], ground(*U(-4, -85)) + 120), label='PlayerStart')
# everything whose material follows the day/night cycle
cyc.set_editor_property('sky_receivers', [a for a in eas.get_all_level_actors() if a.get_actor_label().startswith(('SkyDome', 'Mist_', 'Motes_'))])
cyc.set_time_of_day(17.3)
les.save_current_level()
unreal.log('VISTA_BUILT')
# the NPCs from L_DummyField + navigation bounds (copies them in and saves the vista again)
exec(open('C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/add_vista_npcs.py').read())
unreal.SystemLibrary.quit_editor()
