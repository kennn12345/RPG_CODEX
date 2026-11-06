"""Adds a rock preview station to /Game/RPG/Maps/L_AssetReview, for photographing every rock asset with the real game
lighting. The map is meant to be backed up first and restored afterwards: this station is for previews, not for keeping.
Each rock gets two copies, one turned 180 degrees, under the same camera angle and the same light, plus a same-scale
line-up per variant (A/B/C) next to a 1.8 m marker. Writes Saved/RockPreview/rock_stats.json.
  UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/build_rock_preview.py" -unattended -RenderOffscreen
Photograph: RPG_ASSET_SHOTS=AS_RockPrev + -ExecCmds="Automation RunTests Visual.AssetShots" -TestExit="Automation Test Queue Empty"
Cameras: AS_RockPrev_<set>_<name>_<Front|Back>, AS_RockPrev_Lineup_<A|B|C>."""
import json, math, os
import unreal

MAP = '/Game/RPG/Maps/L_AssetReview'
LIB = '/Game/RPG/Environment/Rocks/Meshes'
PROPS = '/Game/RPG/Environment/Props'
CLASSES = ('Large', 'MediumLarge', 'Medium', 'MediumSmall', 'SmallMedium', 'Small')
PROP_ROCKS = ('MediumLarge_B', 'Medium_A', 'MediumSmall_B', 'MediumSmall_C', 'SmallMedium_A')
MANIFEST = 'C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs/Rocks_R01/rocks_manifest.json'
FOV = 40.0
FIT = 0.5625 * math.tan(math.radians(FOV / 2))      # 16:9, so height sets the framing

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les.load_level(MAP)

sun = next(a for a in eas.get_all_level_actors() if a.get_actor_label() == 'Sun')
sf = sun.get_actor_forward_vector()
fx, fy = sf.x / math.hypot(sf.x, sf.y), sf.y / math.hypot(sf.x, sf.y)
ang = math.radians(35.0)                            # same look direction as the review stage's _Lit cameras
DX, DY = fx * math.cos(ang) - fy * math.sin(ang), fx * math.sin(ang) + fy * math.cos(ang)
PX, PY = -DY, DX


def spawn(what, loc, yaw=0.0, label=None):
    rot = unreal.Rotator(roll=0, pitch=0, yaw=yaw)
    a = eas.spawn_actor_from_object(what, unreal.Vector(*loc), rot)
    if label:
        a.set_actor_label(label)
    return a


def cam(label, eye, tgt, fov=FOV):
    d = (tgt[0] - eye[0], tgt[1] - eye[1], tgt[2] - eye[2])
    rot = unreal.Rotator(roll=0, pitch=math.degrees(math.atan2(d[2], math.hypot(d[0], d[1]))), yaw=math.degrees(math.atan2(d[1], d[0])))
    c = eas.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(*eye), rot)
    c.set_actor_label(label)
    c.tags = [unreal.Name('AssetShot')]
    cc = c.get_component_by_class(unreal.CameraComponent)
    cc.set_editor_property('field_of_view', fov)
    cc.set_editor_property('constrain_aspect_ratio', False)


def along(a, b=0.0):
    """Station-local to world XY: a across the view, b forward along it. The ground plane reaches past this spot."""
    return (DX * 40000.0 + PX * a + DX * b, DY * 40000.0 + PY * a + DY * b)


entries = []
for cls in CLASSES:
    for v in 'ABC':
        entries.append(('Lib', f'Rock_{cls}_{v}', f'{LIB}/SM_Rock_{cls}_{v}'))
for v in 'ABC':
    entries.append(('Lib', f'Pebbles_{v}', f'{LIB}/SM_Pebbles_{v}'))
for n in PROP_ROCKS:
    entries.append(('Prop', f'Rock_{n}', f'{PROPS}/SM_Rock_{n}'))

# trial mode: RPG_ROCK_TEST_MAT=<material path> shoots only the first rock (Large A) with that material, same cameras as the full run
TEST_MAT = unreal.load_asset(os.environ['RPG_ROCK_TEST_MAT']) if os.environ.get('RPG_ROCK_TEST_MAT') else None
if TEST_MAT:
    entries = entries[:1]
# RPG_ROCK_TEST_MESH=<mesh path> also swaps that rock's shape; cameras stay framed on the original, so shots compare directly
TEST_MESH = unreal.load_asset(os.environ['RPG_ROCK_TEST_MESH']) if TEST_MAT and os.environ.get('RPG_ROCK_TEST_MESH') else None

manifest = json.load(open(MANIFEST)) if os.path.exists(MANIFEST) else {}
stats = []
off = -20000.0
for k, (group, name, path) in enumerate(entries):
    mesh = unreal.load_asset(path)
    if not mesh:
        unreal.log_warning(f'ROCK_PREVIEW missing {path}')
        continue
    b = mesh.get_bounds()
    e, r = b.box_extent, b.sphere_radius
    d = max(r / FIT * 1.02, 120.0)
    mat = mesh.get_material(0)
    stats.append({
        'set': group, 'name': name, 'path': path,
        'size_m': [round(e.x * 2 / 100, 2), round(e.y * 2 / 100, 2), round(e.z * 2 / 100, 2)],
        'pivot_bounds_z': round(b.origin.z, 1),
        'triangles_lod0': mesh.get_num_triangles(0),
        'triangles_source': manifest.get(name, {}).get('triangles') if group == 'Lib' else None,
        'nanite': bool(mesh.get_editor_property('nanite_settings').enabled),
        'material': mat.get_name() if mat else None,
    })
    for facing, yaw in (('Front', 0.0), ('Back', 180.0)):
        off += r + d * 0.8 + 200.0
        # at 17.3 h the row's first spot lies in a long shadow from another asset on the map, so this copy stands past the row's end
        x, y = along(33000.0 if k == 0 and facing == 'Front' else off)
        ca, sa = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        ox, oy = b.origin.x * ca - b.origin.y * sa, b.origin.x * sa + b.origin.y * ca   # centre the bounds, not the pivot
        rock = spawn(TEST_MESH or mesh, (x - ox, y - oy, 0.0), yaw, f'RP_{group}_{name}_{facing}')
        if TEST_MAT:
            rock.static_mesh_component.set_material(0, TEST_MAT)
        tz = max(b.origin.z, e.z * 0.5)
        cam(f'AS_RockPrev_{k:02d}_{group}_{name}_{facing}',
            (x - DX * d * .92 + PX * d * .38, y - DY * d * .92 + PY * d * .38, tz + d * .28), (x, y, tz))
        if TEST_MAT and k == 0 and facing == 'Front':       # looks down onto the top surfaces, for judging moss
            cam(f'AS_RockPrev_{k:02d}_{group}_{name}_TopClose', (x - DX * 190 + PX * 80, y - DY * 190 + PY * 80, b.origin.z + e.z + 200), (x, y, b.origin.z + e.z * .6), 50.0)
        off += r + 200.0

# same-scale line-ups, one per variant, with a 1.8 m marker
marker = unreal.load_asset('/Engine/BasicShapes/Cylinder')
for row, v in enumerate('' if TEST_MAT else 'ABC'):
    names = [f'Rock_{c}_{v}' for c in CLASSES] + [f'Pebbles_{v}']
    meshes = [unreal.load_asset(f'{LIB}/SM_{n}') for n in names]
    width = sum(m.get_bounds().sphere_radius * 2 + 90.0 for m in meshes) + 150.0
    a0 = -32000.0 + row * 4000.0
    a = a0 - width / 2
    pole = spawn(marker, (*along(a + 25), 90.0), 0.0, f'RP_Marker_{v}')
    pole.set_actor_scale3d(unreal.Vector(0.5, 0.5, 1.8))
    a += 150.0
    for m, n in zip(meshes, names):
        b = m.get_bounds()
        rr = b.sphere_radius
        x, y = along(a + rr)
        spawn(m, (x - b.origin.x, y - b.origin.y, 0.0), 0.0, f'RP_Line_{n}')
        a += rr * 2 + 90.0
    d = (width / 2) / math.tan(math.radians(25.0)) * 1.1
    x, y = along(a0)
    cam(f'AS_RockPrev_Lineup_{v}', (x - DX * d, y - DY * d, 290.0), (x, y, 110.0), 50.0)

if not TEST_MAT:
    out = os.path.join(unreal.Paths.project_saved_dir(), 'RockPreview')
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, 'rock_stats.json'), 'w') as f:
        json.dump(stats, f, indent=1)

les.save_current_level()
unreal.log(f'ROCK_PREVIEW_BUILT rocks={len(stats)} end_offset={off:.0f}')
unreal.SystemLibrary.quit_editor()
