"""Builds /Game/RPG/Maps/L_DummyField (and the root-locked stagger clip the dummies use): a 100 m x 100 m test field. The middle 50 m x 50 m is flat and clear for testing;
around it are hills and slopes (gentle to steep), a walled corner, low boundary walls, and four wandering GASP dummies.
Run in the full editor:  UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="<abs path to this file>"
(it saves the level and closes the editor). Re-running rebuilds it from scratch."""
import unreal, math

PATH = '/Game/RPG/Maps/L_DummyField'

# The dummy's stagger step-back: GASP's backward walk-to-stop, copied so the dummy can tune it. Its root motion is kept:
# the dummy moves to where the root ends, so the feet stay planted.
STAGGER = '/Game/RPG/Combat/GASP/M_Dummy_StaggerBack'
if not unreal.EditorAssetLibrary.does_asset_exist(STAGGER):
    unreal.EditorAssetLibrary.duplicate_asset('/Game/Characters/UEFN_Mannequin/Animations/Walk/M_Neutral_Walk_Stop_B_Lfoot', STAGGER)
clip = unreal.load_asset(STAGGER)
clip.set_editor_property('force_root_lock', False)
unreal.EditorAssetLibrary.save_asset(STAGGER)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if unreal.EditorAssetLibrary.does_asset_exist(PATH):
    # Rebuild in place: open it and clear it (it may be the startup map, already open, where new_level would only
    # add a second copy of everything).
    les.load_level(PATH)
    for a in eas.get_all_level_actors():
        if not isinstance(a, (unreal.WorldSettings, unreal.Brush)):
            eas.destroy_actor(a)
else:
    try:
        les.new_level(PATH, False)
    except TypeError:
        les.new_level(PATH)

CUBE = unreal.load_asset('/Engine/BasicShapes/Cube')

def box(name, center, size, rot=(0.0, 0.0, 0.0)):
    a = eas.spawn_actor_from_object(CUBE, unreal.Vector(*center), unreal.Rotator(roll=rot[2], pitch=rot[1], yaw=rot[0]))
    a.set_actor_label(name)
    a.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    return a

def plateau(name, cx, cy, px, py, h):
    box(name, (cx, cy, (h - 100.0) / 2.0), (px, py, h + 100.0))

def slope(name, edge, u, width, h, deg, thick=60.0):
    """A slab rising toward the plateau edge at `edge` (x, y) from the ground, coming from direction u (outward unit)."""
    t = math.radians(deg)
    run = h / math.tan(t)
    top = (edge[0] + u[0] * run / 2.0, edge[1] + u[1] * run / 2.0, h / 2.0)
    n = (u[0] * math.sin(t), u[1] * math.sin(t), math.cos(t))
    c = (top[0] - n[0] * thick / 2.0, top[1] - n[1] * thick / 2.0, top[2] - n[2] * thick / 2.0)
    yaw = math.degrees(math.atan2(-u[1], -u[0]))
    box(name, c, (h / math.sin(t) + 120.0, width, thick), (yaw, deg, 0.0))

def ridge(name, cx, cy, px, py, h, deg, axis):
    plateau(name + '_Top', cx, cy, px, py, h)
    for s in (1, -1):
        if axis == 'x': slope('%s_Slope%+d' % (name, s), (cx + s * px / 2.0, cy), (s, 0), py, h, deg)
        else: slope('%s_Slope%+d' % (name, s), (cx, cy + s * py / 2.0), (0, s), px, h, deg)

# Ground (top at Z 0) and low boundary walls.
box('Ground', (0, 0, -50), (10000, 10000, 100))
for i, (x, y, sx, sy) in enumerate([(0, 5025, 10100, 50), (0, -5025, 10100, 50), (5025, 0, 50, 10100), (-5025, 0, 50, 10100)]):
    box('BoundaryWall_%d' % i, (x, y, 50), (sx, sy, 100))

# Hills and slopes, all outside the flat middle (-2500..2500).
ridge('HillA_15deg', 3700, 0, 800, 1600, 200, 15, 'x')
ridge('HillB_10deg', -3700, 2000, 1000, 1000, 120, 10, 'x')
ridge('HillD_8deg', -2000, -3900, 1200, 800, 150, 8, 'x')
plateau('RampC_25deg_Top', 0, 3900, 1200, 600, 250)
slope('RampC_25deg_Slope', (0, 3600), (0, -1), 1200, 250, 25)
# A walled corner (for checking the dummies don't get stuck in corners).
box('CornerWall_A', (3500, -3200, 75), (600, 50, 150))
box('CornerWall_B', (3775, -3475, 75), (50, 600, 150))

# Light and sky.
sun = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1000), unreal.Rotator(roll=0, pitch=-40, yaw=-30))
sun.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property('atmosphere_sun_light', True)
eas.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0))
sky = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 500))
sky.get_component_by_class(unreal.SkyLightComponent).set_editor_property('real_time_capture', True)
eas.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0))
eas.spawn_actor_from_class(unreal.VolumetricCloud, unreal.Vector(0, 0, 0))

# Player start (south of the middle, looking north). The player keeps its average stats (Strength 5, the rest 50).
eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, -1500, 100), unreal.Rotator(roll=0, pitch=0, yaw=90))

# Four NPCs with different builds (the Power System arena's set, plus a Strength for when they fight back): two in
# the flat middle (small wander area), two near the hills (big wander area, so they walk the slopes).
#   label, x, y, wander radius, mass kg, strength, durability, stability, stagger resistance, max health
DUMMIES = [
    ('Standard Bandit', 400, 0, 500, 82, 4, 40, 50, 50, 100),
    ('Light Scout', -500, 600, 500, 55, 3, 35, 40, 40, 70),
    ('Heavy Brute', 2300, 600, 1600, 164, 8, 70, 70, 60, 250),
    ('Steady Veteran', -2300, -2300, 1600, 90, 6, 55, 85, 80, 150),
]
# NPCs (2026-10-26): GASP characters (BP_RPGNpc) walked by AI on the navigation mesh, instead of the old test dummy
# (ARPGCombatTestDummy stays for the automated tests and the Power arena).
NPC = unreal.EditorAssetLibrary.load_blueprint_class('/Game/RPG/NPC/BP_RPGNpc')
for label, x, y, radius, mass, strength, durability, stability, stagger, health in DUMMIES:
    d = eas.spawn_actor_from_class(NPC, unreal.Vector(x, y, 100), unreal.Rotator(roll=0, pitch=0, yaw=-90))
    d.set_actor_label(label.replace(' ', '_'))
    d.set_editor_property('display_label', label)
    d.set_editor_property('wander', True)
    d.set_editor_property('wander_radius', float(radius))
    stats = d.get_editor_property('stats')
    ss = stats.get_editor_property('starting_stats')
    for k, v in (('mass', mass), ('strength', strength), ('durability', durability), ('stability', stability),
                 ('stagger_resistance', stagger), ('max_health', health)):
        ss.set_editor_property(k, float(v))
    stats.set_editor_property('starting_stats', ss)

# Navigation area over the whole field (the navigation mesh itself is built while playing: runtime generation).
nav = eas.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector(0, 0, 300))
nav.set_actor_scale3d(unreal.Vector(52, 52, 8))

les.save_current_level()
unreal.log('DUMMY FIELD BUILT')
unreal.SystemLibrary.quit_editor()
