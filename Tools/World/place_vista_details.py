"""Places the R02b forest-floor detail pieces into /Game/RPG/Maps/L_WorldVista (user OK 2026-11-03: "Yes, place them").
Only ADDS actors labelled Detail_* (outliner folder Details) - the rest of the layout (terrain, water, sky, trees, rocks,
grass, NPCs) is not touched. Re-runnable: earlier Detail_* actors are replaced.
  UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/place_vista_details.py" -unattended -RenderOffscreen
Where things go (like the reference video): tufts, stones and bushes line the path edges; ferns, moss, mushrooms, clover,
fallen leaves, branches, logs and stumps gather at tree feet; moss around big rocks; reeds on the lake / pond shores;
bellflower and blue-flower drifts in the meadow. Uses ARPGScatter (NearPathMax band, IncludeCircles, bIgnoreOtherScatters).
Performance: small pieces cast no shadows and cull at 30-60 m (logs / stumps / bushes cast shadows), patches + open space."""
import unreal, json, random

SRC = 'C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs'
LAYOUT = json.load(open(SRC + '/World_R01/exports/vista_layout.json'))
MAP = '/Game/RPG/Maps/L_WorldVista'
MESHES = '/Game/RPG/Environment/World/Meshes'
EAL = unreal.EditorAssetLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem); eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
YS = -1.0
def U(x, y): return (x * 100.0, YS * y * 100.0)
def M(n): return unreal.load_asset(f'{MESHES}/{n}')

les.load_level(MAP)
for a in eas.get_all_level_actors():
    if a.get_actor_label().startswith('Detail_'): eas.destroy_actor(a)
actors = eas.get_all_level_actors()

# a just-loaded level has no physics bodies yet (traces miss): the scatters trace a fresh temporary terrain copy
terrain = next(a for a in actors if a.get_actor_label() == 'Terrain')
probe = eas.spawn_actor_from_object(terrain.static_mesh_component.static_mesh, terrain.get_actor_location(), terrain.get_actor_rotation())

def instances(label_prefix):
    """world XY of every instance in the scatter actors whose label starts with label_prefix (trees, rocks)."""
    out = []
    for a in actors:
        if a.get_actor_label().startswith(label_prefix) and isinstance(a, unreal.RPGScatter):
            h = a.get_editor_property('instances')
            for i in range(h.get_instance_count()):
                t = h.get_instance_transform(i, True); s = t.scale3d.x
                out.append((t.translation.x, t.translation.y, s))
    return out

TREES = instances('Trees_')
hero = next((a for a in actors if a.get_actor_label() == 'Tree_Hero'), None)
BIG_ROCKS = instances('Rocks_Large') + instances('Rocks_MediumLarge')
PATH = [unreal.Vector(*U(x, y), 0) for x, y in LAYOUT['path']]
lx, ly, lr = LAYOUT['lake']
WATER = [(*U(lx, ly), lr * 100)] + [(*U(px, py), pr * 100) for px, py, pr, pz in LAYOUT['ponds']]
SPAWN_CLEAR = [(*U(-4, -72), 2400)]                      # player spawn + NPC fight clearing (build_vista.py)
# keep the photo cameras' feet clear (Visual.WorldShots / SkyShots stand at eye height on the ground)
CAMS = [(c.get_actor_location().x, c.get_actor_location().y, 350.0) for c in actors if isinstance(c, unreal.CameraActor)]
unreal.log(f'DETAILS trees={len(TREES)} big_rocks={len(BIG_ROCKS)} water={len(WATER)}')

def tree_circles(r_mul, hero_r):
    c = [unreal.Vector(x, y, 330.0 * r_mul * s) for x, y, s in TREES]
    if hero:
        p = hero.get_actor_location(); c.append(unreal.Vector(p.x, p.y, hero_r))
    return c

def scatter(label, mesh, count, seed, scale=(0.8, 1.2), up=0.75, align=False, sink=0.0, path_r=0.0, near_path=0.0,
            include=(), exclude=(), patch=0.0, cover=0.5, collision=False, cull=6000.0, shadow=False, margin=10.0,
            center=(0, 0), half=(9800, 9800)):
    if not mesh: unreal.log_warning(f'DETAILS missing mesh for {label}'); return None
    a = eas.spawn_actor_from_class(unreal.RPGScatter, unreal.Vector(center[0], center[1], 0))
    a.set_actor_label('Detail_' + label); a.set_folder_path('Details')
    for k, v in (('mesh', mesh), ('count', count), ('half_extent', unreal.Vector2D(*half)), ('seed', seed), ('scale_range', unreal.Vector2D(*scale)),
                 ('min_up_normal', up), ('align_to_surface', align), ('sink_depth', sink), ('exclude_path', PATH), ('exclude_radius', path_r),
                 ('near_path_max', near_path), ('include_circles', list(include)), ('exclude_circles', [unreal.Vector(*c) for c in list(exclude) + CAMS]),
                 ('patch_size', patch), ('patch_coverage', cover), ('collision', collision), ('cull_distance', cull),
                 ('skip_water', True), ('water_margin', margin), ('ignore_other_scatters', True)):
        a.set_editor_property(k, v)
    a.get_editor_property('instances').set_editor_property('cast_shadow', shadow)
    a.rebuild(); unreal.log(f'DETAILS {label}: {a.get_editor_property("placed")} / {count}')
    return a

FEET, FEET_WIDE = tree_circles(1.0, 1300.0), tree_circles(1.6, 1800.0)
ROCK_RINGS = [unreal.Vector(x, y, 260.0 * s) for x, y, s in BIG_ROCKS]
SHORES = [unreal.Vector(x, y, r * 1.22 + 250) for x, y, r in WATER]

# path edges: grass tufts overlapping the edge, small stones, bushes a little further out
scatter('PathTufts', M('SM_Vista_Tuft'), 1100, 301, (0.8, 1.25), path_r=110, near_path=260, cull=5000)
scatter('PathStones', M('SM_Vista_PathStones'), 150, 302, (0.8, 1.4), 0.7, True, 2, path_r=120, near_path=210, cull=3500)
scatter('PathBushA', M('SM_Vista_Bush_A'), 45, 303, (0.8, 1.25), path_r=320, near_path=700, patch=2200, cover=0.55, cull=12000, shadow=True,
        exclude=SPAWN_CLEAR)
scatter('PathBushB', M('SM_Vista_Bush_B'), 60, 304, (0.8, 1.3), path_r=300, near_path=650, patch=2000, cover=0.55, cull=10000, shadow=True,
        exclude=SPAWN_CLEAR)
# tree feet: the forest floor
scatter('Ferns', M('SM_Vista_Fern'), 260, 311, (0.8, 1.3), path_r=200, include=FEET_WIDE, cull=6000)
scatter('MossTrees', M('SM_Vista_MossPatch'), 160, 312, (0.7, 1.4), 0.7, True, 4, path_r=150, include=FEET, cull=5000)
scatter('Mushrooms', M('SM_Vista_Mushrooms'), 90, 313, (0.7, 1.5), path_r=150, include=FEET, cull=3000)
scatter('Clover', M('SM_Vista_Clover'), 420, 314, (0.8, 1.3), 0.7, True, path_r=130, include=FEET_WIDE, cull=4000)
scatter('FallenLeaves', M('SM_Vista_FallenLeaves'), 200, 315, (0.8, 1.3), 0.7, True, path_r=0, include=FEET_WIDE, cull=3500)
scatter('Branches', M('SM_Vista_Branches'), 60, 316, (0.8, 1.3), 0.7, True, path_r=180, include=FEET_WIDE, cull=3500)
scatter('BushTrees', M('SM_Vista_Bush_B'), 50, 317, (0.8, 1.2), path_r=350, include=FEET, cull=10000, shadow=True, exclude=SPAWN_CLEAR)
scatter('Logs', M('SM_Vista_Log'), 9, 318, (0.8, 1.15), 0.85, True, 6, path_r=450, include=FEET_WIDE, collision=True, cull=15000, shadow=True,
        exclude=SPAWN_CLEAR)
scatter('Stumps', M('SM_Vista_Stump'), 10, 319, (0.8, 1.2), 0.85, True, 8, path_r=450, include=FEET_WIDE, collision=True, cull=15000,
        shadow=True, exclude=SPAWN_CLEAR)
# rocks: moss cushions and ferns at their feet
scatter('MossRocks', M('SM_Vista_MossPatch'), 90, 321, (0.8, 1.5), 0.6, True, 4, path_r=150, include=ROCK_RINGS, cull=5000)
scatter('FernsRocks', M('SM_Vista_Fern'), 60, 322, (0.8, 1.2), path_r=180, include=ROCK_RINGS, cull=6000)
# shores: reeds on the wet edge of the lake and ponds
scatter('Reeds', M('SM_Vista_Reeds'), 420, 331, (0.8, 1.35), 0.55, include=SHORES, margin=-25.0, cull=7000)
# meadow drifts of wildflowers (open space between patches)
scatter('Bellflowers', M('SM_Vista_Bellflowers'), 650, 341, (0.9, 1.4), 0.8, path_r=160, patch=1300, cover=0.28, cull=5000)
scatter('BlueFlowers', M('SM_Vista_BlueFlowers'), 800, 342, (0.9, 1.4), 0.8, path_r=160, patch=1100, cover=0.3, cull=5000)

eas.destroy_actor(probe)
les.save_current_level()
unreal.log('VISTA_DETAILS_PLACED')
unreal.SystemLibrary.quit_editor()
