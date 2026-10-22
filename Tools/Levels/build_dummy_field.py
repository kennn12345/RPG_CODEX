"""Builds /Game/RPG/Maps/L_DummyField: a 100 m x 100 m test field. The middle 50 m x 50 m is flat and clear for testing;
around it are hills and slopes (gentle to steep), a walled corner, low boundary walls, and four wandering GASP dummies.
Run in the full editor:  UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="<abs path to this file>"
(it saves the level and closes the editor). Re-running rebuilds it from scratch."""
import unreal, math

PATH = '/Game/RPG/Maps/L_DummyField'
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
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

# Player start (south of the middle, looking north) and four dummies: two in the flat middle (small wander area) and
# two near the hills (big wander area, so they walk up and down the slopes).
eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, -1500, 100), unreal.Rotator(roll=0, pitch=0, yaw=90))
for i, (x, y, radius) in enumerate([(400, 0, 500), (-500, 600, 500), (2300, 600, 1600), (-2300, -2300, 1600)]):
    d = eas.spawn_actor_from_class(unreal.RPGCombatTestDummy, unreal.Vector(x, y, 92), unreal.Rotator(roll=0, pitch=0, yaw=-90))
    d.set_actor_label('Dummy_%d' % (i + 1))
    d.set_editor_property('wander', True)
    d.set_editor_property('wander_radius', float(radius))

les.save_current_level()
unreal.log('DUMMY FIELD BUILT')
unreal.SystemLibrary.quit_editor()
