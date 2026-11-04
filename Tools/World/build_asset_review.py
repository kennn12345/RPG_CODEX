"""Builds /Game/RPG/Maps/L_AssetReview: a review stage for the world assets (restyle toward the Dreamscape: Tower video).
Neutral grassy ground, the vista's golden-hour light (same ARPGSkyCycle settings, frozen at 17.3 h, same sky dome and
grade), and one "station" per asset group with fixed cameras tagged "AssetShot":
  <station>_Lit  = sun behind-left of the camera (3/4 lit, like most reference frames)
  <station>_Back = looking toward the low sun (backlit canopies / grass glow)
  <station>_Close = close-up where small details matter (bark, blades, petals)
Stations are laid out relative to the sun, so every re-render has the same light angle. Missing assets are skipped.
Assets come from Tools/World/build_vista_assets.py (run it first). Re-running this script rebuilds the map.
  UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/build_asset_review.py" -unattended -RenderOffscreen
Photograph: -ExecCmds="Automation RunTests Visual.AssetShots" -TestExit="Automation Test Queue Empty"
  (env RPG_ASSET_SHOTS=<camera prefix> shoots only those, e.g. AS_Trees) -> Saved/Screenshots/Assets/<camera>.png"""
import unreal, math

MAP = '/Game/RPG/Maps/L_AssetReview'
ROOT = '/Game/RPG/Environment/World'
MESHES, MATS = ROOT + '/Meshes', ROOT + '/Materials'
ROCKS = '/Game/RPG/Environment/Rocks/Meshes'
EAL = unreal.EditorAssetLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem); eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

def load(path): return unreal.load_asset(path) if EAL.does_asset_exist(path) else None
def need(path):
    a = load(path)
    if not a: raise RuntimeError(f'missing asset {path} - run Tools/World/build_vista_assets.py first')
    return a

# asset report (LOD counts / screen sizes) - a quick check that the hand-made LODs made it in
for n in ('SM_Vista_Tree_Round', 'SM_Tree_Ancient', 'SM_Tree_Forest_01', 'SM_Vista_Grass', 'SM_Vista_Poppies', 'SM_Vista_Bush_A', 'SM_Vista_Fern'):
    a = load(f'{MESHES}/{n}')
    if a:
        sm_ = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        sizes = [round(float(v), 3) for v in sm_.get_lod_screen_sizes(a)] if hasattr(sm_, 'get_lod_screen_sizes') else None
        unreal.log(f'REVIEW_LODS {n} lods={a.get_num_lods()} screens={sizes} tris0={a.get_num_triangles(0)}')

if EAL.does_asset_exist(MAP):
    les.load_level(MAP)
    for a in eas.get_all_level_actors():
        if not isinstance(a, (unreal.WorldSettings, unreal.Brush)): eas.destroy_actor(a)
else:
    les.new_level(MAP)

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

# ---- light: same as L_WorldVista (build_vista.py), frozen at golden hour
def dir_light(label, index):
    a = spawn(unreal.DirectionalLight, (0, 0, 3000), (0, -45, 0), label)
    a.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    setp(a.get_component_by_class(unreal.DirectionalLightComponent), (('atmosphere_sun_light', True), ('atmosphere_sun_light_index', index),
         ('cast_shadows', True), ('light_source_angle', 1.5), ('cast_cloud_shadows', True)), label)
    return a
sun = dir_light('Sun', 0); moon = dir_light('Moon', 1)
setp(sun.get_component_by_class(unreal.DirectionalLightComponent), (('enable_light_shaft_bloom', True), ('bloom_scale', 0.35)), 'sun')
atm = spawn(unreal.SkyAtmosphere, (0, 0, 0), label='SkyAtmosphere')
setp(atm.get_component_by_class(unreal.SkyAtmosphereComponent), (('aerial_perspective_start_depth', 100.0), ('mie_scattering_scale', 0.006)), 'atmosphere')
clouds = spawn(unreal.VolumetricCloud, (0, 0, 0), label='Clouds')
cc_ = clouds.get_component_by_class(unreal.VolumetricCloudComponent)
setp(cc_, (('layer_bottom_altitude', 2.2), ('layer_height', 4.5), ('material', need(f'{MATS}/MI_Vista_Clouds'))), 'clouds')
sky = spawn(unreal.SkyLight, (0, 0, 800), label='SkyLight')
sky.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
setp(sky.get_component_by_class(unreal.SkyLightComponent), (('real_time_capture', True), ('intensity', 1.35)), 'skylight')
fog = spawn(unreal.ExponentialHeightFog, (0, 0, -300), label='HazeFog')
setp(fog.get_component_by_class(unreal.ExponentialHeightFogComponent),
     (('fog_density', 0.007), ('fog_height_falloff', 0.1), ('start_distance', 2500.0), ('fog_max_opacity', 0.8), ('fog_cutoff_distance', 2.0e6),
      ('directional_inscattering_exponent', 6.0), ('enable_volumetric_fog', True), ('volumetric_fog_scattering_distribution', 0.6)), 'fog')
pp = spawn(unreal.PostProcessVolume, (0, 0, 0), label='Grade'); pp.set_editor_property('unbound', True)
s = pp.get_editor_property('settings')
for prop, val in (('auto_exposure_bias', 0.15), ('auto_exposure_min_brightness', -1.5), ('auto_exposure_max_brightness', 14.0),
                  ('bloom_intensity', 1.1), ('color_saturation', unreal.Vector4(1.15, 1.15, 1.15, 1.0)),
                  ('color_contrast', unreal.Vector4(1.05, 1.05, 1.05, 1.0)), ('vignette_intensity', 0.3)):
    try: s.set_editor_property('override_' + prop, True); s.set_editor_property(prop, val)
    except Exception as ex: unreal.log_warning(f'grade {prop}: {ex}')
pp.set_editor_property('settings', s)
dome_mesh = unreal.load_asset('/Engine/EngineSky/SM_SkySphere')
dome = spawn(dome_mesh, (0, 0, 0), label='SkyDome')
dsm = dome.static_mesh_component
dsm.set_material(0, need(f'{MATS}/M_Vista_Sky')); setp(dsm, (('cast_shadow', False), ('bounds_scale', 10.0)), 'dome')
dsm.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
dome.set_actor_scale3d(unreal.Vector(*(3.0e6 / max(dome_mesh.get_bounds().box_extent.x, 1.0),) * 3))
cyc = spawn(unreal.RPGSkyCycle, (0, 0, 0), label='SkyCycle')
for k, v in (('sun', sun), ('moon', moon), ('sky_light', sky), ('fog', fog), ('grade', pp), ('time_of_day', 17.3), ('animate', False),
             ('sunrise_yaw', 140.0), ('day_length_minutes', 24.0), ('moon_intensity', 0.25), ('exposure_bias_night', -2.5), ('sky_light_day', 0.9), ('sun_intensity', 9.0),
             ('sun_color_low', unreal.LinearColor(1.0, 0.55, 0.28, 1)), ('haze_low', unreal.LinearColor(0.48, 0.32, 0.24, 1)), ('sky_light_night', 1.0)):
    try: cyc.set_editor_property(k, v)
    except Exception as ex: unreal.log_warning(f'sky cycle {k}: {ex}')
cyc.set_time_of_day(17.3)
cyc.set_editor_property('sky_receivers', [dome])

# ---- frame relative to the sun: F = where the light travels (horizontal); cameras look along D (sun behind-left)
sf = sun.get_actor_forward_vector()
FX, FY = sf.x / math.hypot(sf.x, sf.y), sf.y / math.hypot(sf.x, sf.y)
def rot2(x, y, deg):
    a = math.radians(deg); return (x * math.cos(a) - y * math.sin(a), x * math.sin(a) + y * math.cos(a))
DX, DY = rot2(FX, FY, 35.0)                  # look direction for _Lit cameras
PX, PY = -DY, DX                             # across the view (stations line up along P, items spread along P)
unreal.log(f'REVIEW sun forward {sf} look {DX:.2f},{DY:.2f}')

def W(cx, cy, a, b):
    """station-local (a = across, b = forward along D) -> world XY around station centre (cx, cy)."""
    return (cx + PX * a + DX * b, cy + PY * a + DY * b)

# ---- neutral ground (big flat grassy plane; the review ground material is made by build_vista_assets.py "review")
plane = need('/Engine/BasicShapes/Plane')
ground = spawn(plane, (0, 0, 0), label='Ground', scale=(1200, 1200, 1))
gm = load(f'{MATS}/M_Review_Ground')
if gm: ground.static_mesh_component.set_material(0, gm)

def cam(label, eye, tgt, fov=62.0):
    d = (tgt[0] - eye[0], tgt[1] - eye[1], tgt[2] - eye[2])
    c = spawn(unreal.CameraActor, eye, (math.degrees(math.atan2(d[1], d[0])), math.degrees(math.atan2(d[2], math.hypot(d[0], d[1]))), 0), label)
    c.tags = [unreal.Name('AssetShot')]
    cc = c.get_component_by_class(unreal.CameraComponent); cc.set_editor_property('field_of_view', fov); cc.set_editor_property('constrain_aspect_ratio', False)

def cams(name, cx, cy, dist, eye_h, tgt_h, fov=62.0, close=None):
    """_Lit from in front (sun behind-left), _Back from the far side looking toward the sun."""
    e = W(cx, cy, 0, -dist); cam(f'{name}_Lit', (e[0], e[1], eye_h), (cx, cy, tgt_h), fov)
    bx, by = rot2(DX, DY, 150.0); cam(f'{name}_Back', (cx - bx * dist, cy - by * dist, eye_h), (cx, cy, tgt_h), fov)
    if close:                                     # (across, forward, eye h, target across, target forward, target h, fov)
        a, b, h, ta, tb, th, f = close
        e = W(cx, cy, a, b); t = W(cx, cy, ta, tb); cam(f'{name}_Close', (e[0], e[1], h), (t[0], t[1], th), f)

def place(mesh, cx, cy, a, b, yaw=0.0, scale=1.0, label=None):
    if not mesh: return None
    x, y = W(cx, cy, a, b)
    return spawn(mesh, (x, y, 0), (yaw, 0, 0), label, (scale, scale, scale))

def scatter(label, mesh, count, cx, cy, half, seed, scale=(0.8, 1.2), patch=0.0, cover=0.5, material=None):
    if not mesh: return
    a = spawn(unreal.RPGScatter, (cx, cy, 0), label=label)
    for k, v in (('mesh', mesh), ('count', count), ('half_extent', unreal.Vector2D(*half)), ('seed', seed), ('scale_range', unreal.Vector2D(*scale)),
                 ('min_up_normal', 0.7), ('patch_size', patch), ('patch_coverage', cover), ('skip_water', False), ('cull_distance', 9000.0)):
        a.set_editor_property(k, v)
    if material: a.set_editor_property('material', material)
    a.get_editor_property('instances').set_editor_property('cast_shadow', False)
    a.rebuild()

STEP = 7000.0                                  # stations every 70 m along P
def station(i): return (PX * STEP * (i - 4), PY * STEP * (i - 4))

# 0 round world trees (vista scale range 0.85-1.3 -> shown at 1.0)
cx, cy = station(0)
for k, n in enumerate(('SM_Vista_Tree_Round', 'SM_Vista_Tree_Tall', 'SM_Vista_Tree_Wide')):
    place(load(f'{MESHES}/{n}'), cx, cy, (k - 1) * 1300, 0, k * 70, 1.0, f'Tree_{n[13:]}')
scatter('Grass_Trees', load(f'{MESHES}/SM_Vista_Grass'), 2500, cx, cy, (2600, 2600), 3)
cams('AS_Trees_Round', cx, cy, 2700, 170, 520, 72, (300, -700, 170, 0, 0, 260, 60))
# 1 ancient hero tree (vista scale 0.75)
cx, cy = station(1)
place(load(f'{MESHES}/SM_Tree_Ancient'), cx, cy, 0, 0, 40, 0.75, 'Tree_Ancient')
scatter('Grass_Ancient', load(f'{MESHES}/SM_Vista_Grass'), 3000, cx, cy, (2800, 2800), 4)
cams('AS_Tree_Ancient', cx, cy, 3400, 170, 900, 75, (-200, -1300, 170, 0, 0, 120, 62))
# 2 Blender forest trees (Trees_R02, reusable)
cx, cy = station(2)
for k in range(4):
    place(load(f'{MESHES}/SM_Tree_Forest_0{k + 1}'), cx, cy, (k - 1.5) * 1100, 0, k * 50, 1.0, f'Tree_Forest_0{k + 1}')
scatter('Grass_Forest', load(f'{MESHES}/SM_Vista_Grass'), 2500, cx, cy, (2600, 2600), 5)
cams('AS_Trees_Forest', cx, cy, 2600, 170, 500, 75)
# 3 ground cover: grass, tall grass, poppies, daisies, wheat
cx, cy = station(3)
scatter('Cover_Grass', load(f'{MESHES}/SM_Vista_Grass'), 9000, cx, cy, (1800, 1800), 11)
scatter('Cover_GrassTall', load(f'{MESHES}/SM_Vista_GrassTall'), 1200, cx, cy, (1800, 1800), 12, patch=900, cover=0.45)
scatter('Cover_Poppies', load(f'{MESHES}/SM_Vista_Poppies'), 900, cx, cy, (1800, 1800), 13, (1.1, 1.5), patch=700, cover=0.35)
scatter('Cover_Daisies', load(f'{MESHES}/SM_Vista_Daisies'), 400, cx, cy, (1800, 1800), 14, (0.9, 1.2), patch=600, cover=0.3)
wx, wy = W(cx, cy, 0, 1300)
scatter('Cover_Wheat', load(f'{MESHES}/SM_Vista_Wheat'), 2500, wx, wy, (700, 500), 15, (0.9, 1.3))
cams('AS_Cover', cx, cy, 1500, 150, 60, 66, (0, -350, 110, 0, 0, 20, 55))
# 4 ground swatch: terrain material with path / shore / rock-slope vertex colours (SM_Review_GroundSwatch)
cx, cy = station(4)
sw = place(load(f'{MESHES}/SM_Review_GroundSwatch'), cx, cy, 0, 0, math.degrees(math.atan2(DY, DX)) - 90.0, 1.0, 'GroundSwatch')
if sw: sw.set_actor_location(unreal.Vector(cx, cy, 75), False, False)   # above the review plane (swatch bumps are +-8 cm, wet hollow -60)
cams('AS_Ground', cx, cy, 1300, 260, 0, 66, (250, -500, 150, 0, 0, 0, 60))
# 5 rocks: the Blender rock library, one of each size
cx, cy = station(5)
for k, n in enumerate(('Rock_Large_A', 'Rock_MediumLarge_B', 'Rock_Medium_A', 'Rock_MediumSmall_C', 'Rock_SmallMedium_A', 'Pebbles_A')):
    place(load(f'{ROCKS}/SM_{n}'), cx, cy, (k - 2.5) * 420 + (k > 0) * 200, (k % 2) * 150, k * 33, 1.0, n)
scatter('Grass_Rocks', load(f'{MESHES}/SM_Vista_Grass'), 2500, cx, cy, (1800, 1800), 6)
cams('AS_Rocks', cx, cy, 1300, 170, 120, 70, (-650, -500, 150, -800, 0, 120, 55))
# 6 cliffs with ivy
cx, cy = station(6)
place(load(f'{MESHES}/SM_Vista_Cliff_A'), cx, cy, -800, 400, 0, 1.0, 'Cliff_A')
place(load(f'{MESHES}/SM_Vista_Cliff_B'), cx, cy, 900, 600, 60, 1.0, 'Cliff_B')
cams('AS_Cliffs', cx, cy, 2600, 200, 500, 72, (-500, -900, 170, -800, 400, 400, 62))
# 7 detail pieces (bushes, tufts, fallen leaves, path stones, reeds - whichever exist)
cx, cy = station(7)
details = ['SM_Vista_Bush_A', 'SM_Vista_Bush_B', 'SM_Vista_Tuft', 'SM_Vista_Reeds', 'SM_Vista_PathStones', 'SM_Vista_FallenLeaves']
for k, n in enumerate(details):
    place(load(f'{MESHES}/{n}'), cx, cy, (k - 2.5) * 350, 0, k * 40, 1.0, n[9:])
scatter('Grass_Details', load(f'{MESHES}/SM_Vista_Grass'), 120, cx, cy, (1600, 1200), 7)        # sparse: the pieces must stay visible
cams('AS_Details', cx, cy, 750, 130, 50, 70, (-500, -330, 90, -700, 0, 20, 55))
# 8 forest-floor life: moss, ferns, wildflowers, clover, mushrooms, log, stump, branches, ivy curtain on a rock
cx, cy = station(8)
life = [('SM_Vista_Log', -700, 150), ('SM_Vista_Stump', 450, 250), ('SM_Vista_MossPatch', -150, -100), ('SM_Vista_Fern', 250, -150),
        ('SM_Vista_Bellflowers', -450, -250), ('SM_Vista_BlueFlowers', 50, -330), ('SM_Vista_Clover', -250, -350), ('SM_Vista_Mushrooms', 380, -20),
        ('SM_Vista_Branches', -50, 120)]
for k, (n, a_, b_) in enumerate(life):
    place(load(f'{MESHES}/{n}'), cx, cy, a_, b_, k * 37, 1.0, n[9:])
rk = place(load(f'{ROCKS}/SM_Rock_MediumLarge_B'), cx, cy, 900, 500, 0, 1.0, 'IvyRock')
iv = place(load(f'{MESHES}/SM_Vista_IvyCurtain'), cx, cy, 900, 380, 0, 1.0, 'IvyCurtain')
if iv: iv.set_actor_location(iv.get_actor_location() + unreal.Vector(0, 0, 200), False, False)
scatter('Grass_Life', load(f'{MESHES}/SM_Vista_Grass'), 150, cx, cy, (1600, 1200), 8)
cams('AS_Life', cx, cy, 950, 150, 40, 70, (-180, -620, 80, -180, -220, 5, 55))

# 9 gallery: every world asset alone on bare ground, one framed camera each (AS_Item_NN_<name>) - per-asset previews
GALLERY = ['SM_Tree_Ancient', 'SM_Tree_Forest_01', 'SM_Tree_Forest_02', 'SM_Tree_Forest_03', 'SM_Tree_Forest_04', 'SM_Vista_Tree_Round',
           'SM_Vista_Tree_Tall', 'SM_Vista_Tree_Wide', 'SM_Vista_Bush_A', 'SM_Vista_Bush_B', 'SM_Vista_Grass', 'SM_Vista_GrassTall', 'SM_Vista_Tuft',
           'SM_Vista_Wheat', 'SM_Vista_Poppies', 'SM_Vista_Daisies', 'SM_Vista_Bellflowers', 'SM_Vista_BlueFlowers', 'SM_Vista_Fern',
           'SM_Vista_Clover', 'SM_Vista_MossPatch', 'SM_Vista_Mushrooms', 'SM_Vista_Reeds', 'SM_Vista_Log', 'SM_Vista_Stump', 'SM_Vista_Branches',
           'SM_Vista_FallenLeaves', 'SM_Vista_PathStones', 'SM_Vista_IvyCurtain', 'SM_Vista_Cliff_A', 'SM_Vista_Cliff_B',
           'ROCK:Rock_Large_A', 'ROCK:Rock_MediumLarge_C', 'ROCK:Rock_Medium_B', 'ROCK:Pebbles_A']
gx, gy = DX * 30000.0 - PX * 14000.0, DY * 30000.0 - PY * 14000.0; off = 0.0      # 300 m in front of the stations: bare ground behind every item
for k, n in enumerate(GALLERY):
    mesh = load(f'{ROCKS}/SM_{n[5:]}') if n.startswith('ROCK:') else load(f'{MESHES}/{n}')
    if not mesh: continue
    b = mesh.get_bounds(); e = b.box_extent; size = max(e.x, e.y, e.z)
    off += size + 400.0
    x, y = gx + PX * off, gy + PY * off                          # along the row, across the look direction
    a = spawn(mesh, (x - b.origin.x, y - b.origin.y, 0.0 if not n.endswith('IvyCurtain') else e.z * 2 + 20), (0, 0, 0), 'G_' + n.split(':')[-1])
    off += size + 400.0
    tz = b.origin.z * (0 if n.endswith('IvyCurtain') else 1) + (e.z * 2 + 20 if n.endswith('IvyCurtain') else 0) * .5
    tz = max(15.0, min(tz, e.z * 1.1)) if not n.endswith('IvyCurtain') else e.z + 20
    d = max(size * 3.8, 170.0)
    ex, ey = x - DX * d * .92 + PX * d * .38, y - DY * d * .92 + PY * d * .38
    cam(f'AS_Item_{k:02d}_{n.split(":")[-1].replace("SM_Vista_", "").replace("SM_", "")}', (ex, ey, tz + d * .28), (x, y, tz), 50.0)

# 10 LOD sweep + seam check (R02b): a round tree and a forest tree photographed from 6 distances along one line, so the
# LOD switches can be compared frame to frame (AS_LOD_<tree>_dNN), plus a close-up of the round tree's mid trunk (AS_Seam)
sx, sy = DX * -30000.0, DY * -30000.0                    # 300 m behind the stations: open ground
for k, (n, a_) in enumerate((('SM_Vista_Tree_Round', -900), ('SM_Tree_Forest_02', 900))):
    mesh = load(f'{MESHES}/{n}')
    if not mesh: continue
    x, y = sx + PX * a_, sy + PY * a_
    spawn(mesh, (x, y, 0), (0, 0, 0), 'LOD_' + n)
    for d in (6, 12, 20, 32, 50, 80):
        e = (x - DX * d * 100, y - DY * d * 100)
        cam(f'AS_LOD_{n.replace("SM_Vista_", "").replace("SM_", "")}_d{d:02d}', (e[0], e[1], 170), (x, y, 520 if d > 12 else 420), 60.0)
    if k == 0:
        cam('AS_Seam_RoundTrunk', (x - DX * 260 + PX * 60, y - DY * 260 + PY * 60, 290), (x, y, 300), 45.0)

# 11 hero grove (step 2): three copies of the hero tree with grass + daisies, cameras like the user's screenshots:
# low angle up into the crowns against the sky (User_05), eye level under the canopy (User_01/04), mid distance (User_03)
hx, hy = DX * -30000.0 + PX * 6000.0, DY * -30000.0 + PY * 6000.0
hero = load(f'{MESHES}/SM_Vista_Tree_Hero01')
if hero:
    for k, (a_, b_, yaw, sc) in enumerate(((0, 0, 0, 1.0), (-1300, 900, 130, .9), (1200, 1400, 250, 1.1))):
        place(hero, hx, hy, a_, b_, yaw, sc, f'Hero_{k}')
    # the Fab pack's elm (reference for the leaf technique), 1.8x so it matches our hero's size
    MP = '/Game/StylizedTreesByMayu/01_Trees'
    for k, (mn, mat) in enumerate(((f'{MP}/Elm/Mesh/SM_Elm_Bark', f'{MP}/00_Base/Material/M_Bark'), (f'{MP}/Elm/Mesh/SM_Elm_leaves', f'{MP}/Elm/Material/MI_Elm'))):
        mm = load(mn)
        if mm:
            a_ = place(mm, hx, hy, 2900, 1200, 0, 1.8, f'MayuElm_{k}')
            if load(mat): a_.static_mesh_component.set_material(0, load(mat))
    scatter('Grass_Hero', load(f'{MESHES}/SM_Vista_Grass'), 6000, hx, hy, (2600, 2600), 41)
    scatter('Daisies_Hero', load(f'{MESHES}/SM_Vista_Daisies'), 300, hx, hy, (2600, 2600), 42, patch=900, cover=.4)
    e = W(hx, hy, 500, -1700); cam('AS_Hero_LowUp', (e[0], e[1], 60), (hx, hy, 900), 75.0)
    for k_, (a_, b_) in enumerate(((-260, -330), (300, 260))):        # branch-trunk junction close-ups (two sides)
        e = W(hx, hy, a_, b_); cam(f'AS_Hero_Junction{k_}', (e[0], e[1], 330), (hx, hy, 470), 55.0)
    e = W(hx, hy, -300, -900); cam('AS_Hero_UnderCanopy', (e[0], e[1], 170), (hx + DX * 600, hy + DY * 600, 350), 80.0)
    e = W(hx, hy, 0, -3200); cam('AS_Hero_Mid', (e[0], e[1], 200), (hx, hy, 650), 60.0)
    e = W(hx, hy, 1400, -3600); cam('AS_Hero_VsMayuElm', (e[0], e[1], 200), (hx + PX * 1450 + DX * 600, hy + PY * 1450 + DY * 600, 650), 62.0)
    bx, by = rot2(DX, DY, 150.0); cam('AS_Hero_Backlit', (hx - bx * 2600, hy - by * 2600, 180), (hx, hy, 700), 70.0)

# 12 the Fab pack's trees (Mayu's Stylized Trees) with their showcase-level materials, in a row next to our hero tree
P_ = '/Game/StylizedTreesByMayu/01_Trees'
PACK = [('Elm', [('Elm/Mesh/SM_Elm_Bark', '00_Base/Material/M_Bark'), ('Elm/Mesh/SM_Elm_leaves', 'Elm/Material/MI_Elm')]),
        ('ElmGreen', [('Elm/Mesh/SM_Elm_Bark', '00_Base/Material/M_Bark'), ('Elm/Mesh/SM_Elm_leaves', '/Game/RPG/Environment/World/Materials/MI_Pack_ElmGreen')]),
        ('Fir', [('Fir/Mesh/SM_Fir_bark', '00_Base/Material/M_Bark'), ('Fir/Mesh/SM_Fir_leaf', 'Fir/Material/MI_Fir')]),
        ('Willow', [('Willow/Mesh/SM_Willow_Bark', '00_Base/Material/M_Bark'), ('Willow/Mesh/SM_Willow', 'Willow/Material/MI_Willow')]),
        ('Spherical', [('Variation/Mesh/SM_SphericalTree', None)]),
        ('TreeitLeaves', [('Variation/Mesh/SM_Bark_02', '00_Base/Material/M_Bark'), ('Variation/Mesh/SM_Treeit_Leaves_2', 'Variation/Material/MI_Leaves')]),
        ('HighLeaves', [('Variation/Mesh/SM_Trunk', '00_Base/Material/M_Bark'), ('Variation/Mesh/SM_TreeLeaves_High', 'Variation/Material/MI_Leaves')])]
px0, py0 = DX * -30000.0 - PX * 9000.0, DY * -30000.0 - PY * 9000.0
for k, (nm, parts) in enumerate(PACK):
    x, y = px0 + PX * k * 1100.0, py0 + PY * k * 1100.0
    top = 0.0
    for mp, matp in parts:
        mm = load(f'{P_}/{mp}')
        if not mm: continue
        act = spawn(mm, (x, y, 0), (0, 0, 0), f'Pack_{nm}')
        if matp:
            mt = load(matp if matp.startswith('/Game') else f'{P_}/{matp}')
            if mt:
                for i in range(act.static_mesh_component.get_num_materials()): act.static_mesh_component.set_material(i, mt)
        top = max(top, mm.get_bounds().box_extent.z * 2)
    e = (x - DX * top * 1.5, y - DY * top * 1.5)
    cam(f'AS_Pack_{k:02d}_{nm}', (e[0], e[1], 180), (x, y, top * .5), 55.0)
if hero: place(hero, px0, py0, -1500, 0, 0, 1.0, 'Pack_OurHero')
scatter('Grass_Pack', load(f'{MESHES}/SM_Vista_Grass'), 4000, px0 + PX * 3000, py0 + PY * 3000, (4800, 1600), 43)
mx, my = px0 + PX * 2800, py0 + PY * 2800
e = (mx - DX * 3200, my - DY * 3200); cam('AS_Pack_Group', (e[0], e[1], 300), (mx, my, 500), 75.0)

# 13 AI-permitted Fab packs (CC BY 4.0): Lux Art Studios Vegetation Stylized Kit + JABAMI anime tree, one camera each + group
LUXP = '/Game/Vegetation_Stylized_Pack_ByLuxArtStudios/Mesh'
FABITEMS = [(f'Lux_Tree_{c}', f'{LUXP}/Trees/S_Tree_{c}') for c in 'ABCDEFGHIJ'] + [('Jabami_Tree', '/Game/FabPacks/JabamiAnimeTree/SM_Jabami_Tree')] +            [(f'Lux_{n}', f'{LUXP}/Bushes/S_{n}') for n in ('Bush_A', 'Bush_C', 'Fern_B', 'Flowers_A', 'Flowers_E', 'Cattail_A', 'Clovers_A', 'Grass_01A')]
fx0, fy0 = DX * -30000.0 + PX * 16000.0, DY * -30000.0 + PY * 16000.0
off = 0.0
for k, (nm, mp) in enumerate(FABITEMS):
    mm = load(mp)
    if not mm: continue
    e_ = mm.get_bounds().box_extent; sz = max(e_.x, e_.y, e_.z)
    off += max(e_.x, e_.y) + 250.0
    x, y = fx0 + PX * off, fy0 + PY * off; off += max(e_.x, e_.y) + 250.0
    spawn(mm, (x, y, 0), (k * 40, 0, 0), f'Fab_{nm}')
    d = max(sz * 2.7, 220.0); tz = e_.z * 2 * .48
    cam(f'AS_Fab_{k:02d}_{nm}', (x - DX * d, y - DY * d, max(160.0, tz * .55)), (x, y, tz), 55.0)
if hero: place(hero, fx0, fy0, -1600, 0, 0, 1.0, 'Fab_OurHero')
scatter('Grass_Fab', load(f'{MESHES}/SM_Vista_Grass'), 6000, fx0 + PX * off * .5, fy0 + PY * off * .5, (off * .55 + 2000, 2000), 44)
gx_, gy_ = fx0 + PX * 2600, fy0 + PY * 2600
cam('AS_Fab_Group', (gx_ - DX * 3600, gy_ - DY * 3600, 320), (gx_, gy_, 450), 78.0)

les.save_current_level()
unreal.log('ASSET_REVIEW_BUILT')
unreal.SystemLibrary.quit_editor()
