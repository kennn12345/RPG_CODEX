"""Puts the rebuilt Fab trees (Lux Art Studios A-J + JABAMI, CC BY 4.0, foliage from Tools/World/grow_fab_canopies.py) into
the game's test map /Game/RPG/Maps/L_WorldVista IN PLACE OF the round trees (user 2026-11-04: "put them in places trees
are already ... into the ground, not on top of rocks or other assets, randomize what trees go where").
  UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/place_fab_trees.py" -unattended -RenderOffscreen
- Spots = every instance of the Trees_Round / Trees_Tall / Trees_Wide scatters (saved once to vista_tree_spots.json, so
  the script stays re-runnable after those scatters are removed). Those three scatters are deleted; the ancient hero
  tree, far forests and everything else stay. Restore the old trees: git checkout the map (Content/RPG/Maps/L_WorldVista.umap).
- Which tree where: a shuffled, balanced deck of the 11 trees (seeded), random yaw, scale 0.85-1.1.
- Never on top of things: a spot must keep clear of rocks, pebbles, cliffs, solid detail pieces (logs, stumps, bushes),
  other static meshes (hero tree, NPC props), the path, water and the photo cameras' feet; if not, it moves out in rings
  of 2-8 m, else it is skipped. Trunks stay >= 5 m apart.
- Into the ground: height from 5 traces on the terrain (centre + 4 around the root flare); the tree sits at the LOWEST
  of them minus 35 cm, so no side of the roots floats on a slope; spots steeper than 1.6 m across the flare move on.
Actors: FabTree_NN (StaticMeshActor, outliner folder FabTrees). Prints FABTREES lines."""
import unreal, json, math, os, random

SRC = 'C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs'
LAYOUT = json.load(open(SRC + '/World_R01/exports/vista_layout.json'))
SPOTS_JSON = SRC + '/World_R01/exports/vista_tree_spots.json'
MAP = '/Game/RPG/Maps/L_WorldVista'
RS = '/Game/FabPacks/Restyled'
TREES = [f'{RS}/SM_Lux_Tree_{c}' for c in 'ABCDEFGHIJ'] + [f'{RS}/SM_Jabami_Tree_Ours']
TRUNK_R, SINK, MIN_GAP, STEEP = 160.0, 35.0, 500.0, 160.0
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem); eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
YS = -1.0
def U(x, y): return (x * 100.0, YS * y * 100.0)

les.load_level(MAP)
world = unreal.EditorLevelLibrary.get_editor_world()
for a in eas.get_all_level_actors():
    if a.get_actor_label().startswith('FabTree_'): eas.destroy_actor(a)
actors = eas.get_all_level_actors()

def inst(a):
    h = a.get_editor_property('instances'); m = a.get_editor_property('mesh')
    ex = m.get_bounds().box_extent if m else unreal.Vector(100, 100, 100)
    out = []
    for i in range(h.get_instance_count()):
        t = h.get_instance_transform(i, True)
        out.append((t.translation.x, t.translation.y, max(ex.x, ex.y) * t.scale3d.x, t.rotation.rotator().yaw, t.scale3d.x))
    return out

# 1 tree spots (from the round-tree scatters, or the saved list on a re-run)
old = [a for a in actors if isinstance(a, unreal.RPGScatter) and a.get_actor_label().startswith('Trees_')]
if old:
    spots = [(x, y) for a in old for x, y, _, _, _ in inst(a)]
    json.dump(spots, open(SPOTS_JSON, 'w'))
else:
    spots = [tuple(p) for p in json.load(open(SPOTS_JSON))]
unreal.log(f'FABTREES spots={len(spots)} from {"scatters" if old else "saved list"}')

# 2 things a trunk must not stand on
obst = []                                                     # (x, y, r)
for a in actors:
    lab = a.get_actor_label()
    if isinstance(a, unreal.RPGScatter):
        if lab.startswith(('Rocks_', 'ShoreRocks_', 'Pebbles_', 'Cliffs_')) or (lab.startswith('Detail_') and a.get_editor_property('collision')):
            obst += [(x, y, r, lab) for x, y, r, _, _ in inst(a)]
    elif isinstance(a, unreal.StaticMeshActor) and lab not in ('Terrain', 'FarLand') and not lab.startswith(('Water', 'Sky', 'Lake', 'River', 'Pond', 'Motes', 'DriftLeaves', 'Mist')):   # effect boxes are not solid
        o, e = a.get_actor_bounds(False)
        if max(e.x, e.y) < 5000: obst.append((o.x, o.y, max(e.x, e.y), lab))
    elif isinstance(a, unreal.CameraActor):
        p = a.get_actor_location(); obst.append((p.x, p.y, 600.0, lab))
PATH = [U(x, y) for x, y in LAYOUT['path']]
lx, ly, lr = LAYOUT['lake']
WATER = [(*U(lx, ly), lr * 100 + 300)] + [(*U(px, py), pr * 100 + 300) for px, py, pr, pz in LAYOUT['ponds']]
CLEAR = [(*U(-4, -72), 2400)]                                 # player spawn + NPC clearing (build_vista.py)
unreal.log(f'FABTREES obstacles={len(obst)}')

def seg_d(px, py, a, b):
    ax, ay = a; bx, by = b; dx, dy = bx - ax, by - ay; L = dx * dx + dy * dy
    t = 0.0 if L == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L))
    return math.hypot(px - ax - t * dx, py - ay - t * dy)

# terrain traces: a just-loaded level has no physics yet -> trace a fresh temporary copy of the terrain
terrain = next(a for a in actors if a.get_actor_label() == 'Terrain')
probe = eas.spawn_actor_from_object(terrain.static_mesh_component.static_mesh, terrain.get_actor_location(), terrain.get_actor_rotation())
def ground(x, y):
    hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, 30000), unreal.Vector(x, y, -30000),
                                                 unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [], unreal.DrawDebugTrace.NONE, True)
    t = hit.to_tuple() if hit else None
    if not t or not t[0]: return None
    for v in t:
        if isinstance(v, unreal.Vector) and abs(v.z) < 29999: return v.z
    return None

placed = []                                                   # (x, y, z)
def ok(x, y):
    hit = next((o for o in obst if math.hypot(x - o[0], y - o[1]) < o[2] + TRUNK_R), None)
    if hit: return None, f'{hit[3]} r{hit[2] / 100:.0f}m'
    if PATH and min(seg_d(x, y, a, b) for a, b in zip(PATH, PATH[1:])) < 450: return None, 'path'
    if any(math.hypot(x - cx, y - cy) < r for cx, cy, r in WATER + CLEAR): return None, 'water/clearing'
    if any(math.hypot(x - p[0], y - p[1]) < MIN_GAP for p in placed): return None, 'too close'
    zs = [ground(x, y)] + [ground(x + math.cos(a) * 120, y + math.sin(a) * 120) for a in (0, 1.57, 3.14, 4.71)]
    if any(z is None for z in zs): return None, 'no ground'
    if max(zs) - min(zs) > STEEP: return None, 'steep'
    return min(zs) - SINK, ''

rnd = random.Random(20261104)
deck = []
meshes = [unreal.load_asset(p) for p in TREES]
meshes = [m for m in meshes if m]
moved = skipped = 0; why = {}
for k, (x0, y0) in enumerate(spots):
    z, reason = ok(x0, y0); x, y = x0, y0
    if z is None:
        for ring in (200, 400, 600, 800):
            for j in range(8):
                a = j * math.pi / 4 + rnd.uniform(-.3, .3)
                z, _ = ok(x0 + math.cos(a) * ring, y0 + math.sin(a) * ring)
                if z is not None: x, y = x0 + math.cos(a) * ring, y0 + math.sin(a) * ring; break
            if z is not None: break
        if z is None: skipped += 1; why[reason] = why.get(reason, 0) + 1; continue
        moved += 1
    if not deck: deck = list(range(len(meshes))); rnd.shuffle(deck)
    placed.append((x, y, z, deck.pop(), rnd.uniform(0, 360), rnd.uniform(.85, 1.1)))

eas.destroy_actor(probe)                                      # traces done before any tree gets a physics body
counts = {}
for n, (x, y, z, mi, yaw, s) in enumerate(placed):
    m = meshes[mi]
    a = eas.spawn_actor_from_object(m, unreal.Vector(x, y, z), unreal.Rotator(0, 0, yaw))
    a.set_actor_scale3d(unreal.Vector(s, s, s)); a.set_actor_label(f'FabTree_{n:02d}'); a.set_folder_path('FabTrees')
    counts[m.get_name()] = counts.get(m.get_name(), 0) + 1
for a in old: eas.destroy_actor(a)
les.save_current_level()
unreal.log(f'FABTREES placed={len(placed)} moved={moved} skipped={skipped} {why} removed_scatters={len(old)} per_tree={counts}')
unreal.log('FABTREES_DONE')
unreal.SystemLibrary.quit_editor()
