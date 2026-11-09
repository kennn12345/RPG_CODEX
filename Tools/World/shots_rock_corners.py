"""Close-ups of rocks floating against the sky (any gap between chunks shows sky behind it). Each rock is placed 150 m
up over the test square and photographed from 6 angles. Env RPG_SHOT_TAG (default 'rocks_before'); RPG_SHOT_NOLOAD=1
keeps in-memory edits (chained after fixes); RPG_ROCKS=comma list to limit."""
import sys, os, math, unreal
sys.path.append('C:/UNREAL PROJECTS/RPG_CODEX/Tools/World')
import importlib, ground_shots as gs; importlib.reload(gs)
TAG = os.environ.get('RPG_SHOT_TAG', 'rocks_before')
if os.environ.get('RPG_SHOT_NOLOAD') != '1':
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level('/Game/RPG/Maps/L_WorldVista')
NAMES = os.environ.get('RPG_ROCKS', 'Rock_Large_A,Rock_Large_C,Rock_MediumLarge_B,Rock_Medium_A,Rock_Medium_B,Rock_MediumSmall_C,Rock_SmallMedium_C,Rock_Small_B').split(',')
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
SHOTS = []; spawned = []
for i, n in enumerate(NAMES):
    sm = unreal.load_asset('/Game/RPG/Environment/Rocks/Meshes/SM_' + n)
    if not sm: continue
    b = sm.get_bounds(); r = max(b.box_extent.x, b.box_extent.y, b.box_extent.z)
    cx, cy, cz = -6000 + i * 2500, -6000, 15000
    a = eas.spawn_actor_from_object(sm, unreal.Vector(cx - b.origin.x, cy - b.origin.y, cz - b.origin.z))
    spawned.append(a)
    for k, (yaw, pitch) in enumerate(((0, 8), (60, 25), (120, -10), (180, 35), (240, 5), (300, -25))):
        d = r * 2.6
        ex = cx - math.cos(math.radians(yaw)) * math.cos(math.radians(pitch)) * d
        ey = cy - math.sin(math.radians(yaw)) * math.cos(math.radians(pitch)) * d
        ez = cz + math.sin(math.radians(pitch)) * d
        SHOTS.append({'name': f'{n}_{k}', 'loc': (ex, ey, ez), 'rot': (-pitch, yaw), 'fov': 50})
def done():
    for a in spawned: eas.destroy_actor(a)
    unreal.SystemLibrary.quit_editor()
gs.shoot(SHOTS, TAG, then=done, res=(1200, 900), exposure=-0.5)
