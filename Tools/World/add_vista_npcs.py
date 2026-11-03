"""Copies the NPCs from /Game/RPG/Maps/L_DummyField into /Game/RPG/Maps/L_WorldVista (same BP_RPGNpc instances with all
their per-instance settings), places them in the meadow ahead of the PlayerStart, and gives the vista navigation bounds
(the navmesh is generated at runtime - RuntimeGeneration=Dynamic). Safe to re-run: replaces the vista's NPC copies.
L_DummyField itself is only read, never saved.
  UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/add_vista_npcs.py" -unattended
build_vista.py runs this at the end of every rebuild."""
import unreal

SRC_MAP, VISTA = '/Game/RPG/Maps/L_DummyField', '/Game/RPG/Maps/L_WorldVista'
NPC_CLASS = 'BP_RPGNpc_C'
# where each NPC goes, as (right, forward) cm from the PlayerStart (vista player faces north up the path)
PLACES = {'Standard_Bandit': (300, 1600), 'Light_Scout': (-600, 2200), 'Heavy_Brute': (1500, 2600), 'Steady_Veteran': (-1800, 900)}
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem); eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

def world(): return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
def edit(cmd): unreal.SystemLibrary.execute_console_command(world(), cmd)
def npcs(): return [a for a in eas.get_all_level_actors() if a.get_class().get_name() == NPC_CLASS]

def ground(x, y):
    hit = unreal.SystemLibrary.line_trace_single(world(), unreal.Vector(x, y, 30000), unreal.Vector(x, y, -30000),
                                                 unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, IGNORE, unreal.DrawDebugTrace.NONE, True)
    for v in (hit.to_tuple() if hit else ()):
        if isinstance(v, unreal.Vector) and abs(v.z) < 29999: return v.z
    return 0.0

# 1. copy from the dummy field (editor clipboard keeps every instance property and component setting)
les.load_level(SRC_MAP)
src = npcs()
unreal.log(f'NPC_SOURCE {[a.get_actor_label() for a in src]}')
eas.set_selected_level_actors(src)
edit('EDIT COPY')

# 2. paste into the vista, replacing earlier copies
les.load_level(VISTA)
for a in npcs(): eas.destroy_actor(a)
eas.select_nothing()
edit('EDIT PASTE')
pasted = npcs()
unreal.log(f'NPC_PASTED {[a.get_actor_label() for a in pasted]}')

# 3. face the player up the path and stand the NPCs in the meadow ahead of it
start = next(a for a in eas.get_all_level_actors() if isinstance(a, unreal.PlayerStart))
start.set_actor_rotation(unreal.Rotator(0, 0, -90), False)          # yaw -90 = north (Unreal -Y), up the path
p = start.get_actor_location()
# a just-loaded level has no physics bodies yet (traces miss): trace against a fresh temporary copy of the terrain
terrain = next(a for a in eas.get_all_level_actors() if a.get_actor_label() == 'Terrain')
probe = eas.spawn_actor_from_object(terrain.static_mesh_component.static_mesh, terrain.get_actor_location(), terrain.get_actor_rotation())
IGNORE = [a for a in eas.get_all_level_actors() if a != probe]          # the ground only - never a rock or tree top
for a in pasted:
    right, fwd = PLACES.get(a.get_actor_label(), (0, 1200))
    x, y = p.x - right, p.y - fwd                                    # facing -Y: forward is -Y, the player's right is -X
    a.set_actor_location(unreal.Vector(x, y, ground(x, y) + 100), False, False)
    a.set_actor_rotation(unreal.Rotator(0, 0, 90), False)             # facing the player (south)
    unreal.log(f'NPC_PLACED {a.get_actor_label()} {a.get_actor_location()}')
start.set_actor_location(unreal.Vector(p.x, p.y, ground(p.x, p.y) + 120), False, False)
# Visual.WorldShots camera: the player's spawn view of the NPCs
for a in eas.get_all_level_actors():
    if a.get_actor_label() == 'WS_14_PlayerSpawnNPCs': eas.destroy_actor(a)
cam = eas.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(p.x, p.y + 500, ground(p.x, p.y + 500) + 260), unreal.Rotator(0, -8, -90))
cam.set_actor_label('WS_14_PlayerSpawnNPCs'); cam.tags = [unreal.Name('WorldShot')]
cc = cam.get_component_by_class(unreal.CameraComponent); cc.set_editor_property('field_of_view', 70.0); cc.set_editor_property('constrain_aspect_ratio', False)
eas.destroy_actor(probe)

# 4. navigation over the whole vista (wander / AI move)
for a in eas.get_all_level_actors():
    if isinstance(a, unreal.NavMeshBoundsVolume): eas.destroy_actor(a)
nav = eas.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector(0, 0, 0))
nav.set_actor_label('NavBounds'); nav.set_actor_scale3d(unreal.Vector(100, 100, 40))   # brush is 200 cm -> 200 x 200 x 80 m
# the navmesh actor itself (tiles are generated at runtime); without one saved, the crowd manager warns at startup
if not any(isinstance(a, unreal.RecastNavMesh) for a in eas.get_all_level_actors()):
    eas.spawn_actor_from_class(unreal.RecastNavMesh, unreal.Vector(0, 0, 0)).set_actor_label('RecastNavMesh-Default')
les.save_current_level()
unreal.log('VISTA_NPCS_DONE')
