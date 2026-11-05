"""Removes ground-cover / detail instances that ended up IN THE AIR in /Game/RPG/Maps/L_WorldVista (user 2026-11-05: "green
globes in the leaves [of the ancient tree] that almost look like clumps of grass, remove those").
  UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/drop_floating_cover.py" -unattended -RenderOffscreen
Why: ARPGScatter drops each instance on the FIRST thing its down-trace hits that is not a scatter - a tree crown counts.
Moss, clover, ferns, grass placed at the ancient tree's feet landed on top of its old dense canopy (hidden inside it);
with the airier rebuilt clumps they show as green balls in the leaves. Spots under the old round trees (replaced by the
Fab trees) can hold the same leftovers, now floating in mid-air.
Rule: an instance of a ground scatter whose base sits > 1.5 m above the terrain is removed, unless it rests on a rock /
cliff / pebble instance (moss on rocks is intended). Rocks, cliffs, trees, far forests and effects are never touched.
Instances are stored in the map (ARPGScatter only clears them on Rebuild), so the removal persists. Prints FLOAT lines."""
import unreal, math

MAP = '/Game/RPG/Maps/L_WorldVista'
ABOVE = 150.0
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem); eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les.load_level(MAP)
world = unreal.EditorLevelLibrary.get_editor_world()
actors = eas.get_all_level_actors()
terrain = next(a for a in actors if a.get_actor_label() == 'Terrain')
probe = eas.spawn_actor_from_object(terrain.static_mesh_component.static_mesh, terrain.get_actor_location(), terrain.get_actor_rotation())
OTHERS = [a for a in actors if a != terrain]
def ground(x, y):
    hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, 30000), unreal.Vector(x, y, -30000),
                                                 unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, OTHERS, unreal.DrawDebugTrace.NONE, True)
    t = hit.to_tuple() if hit else None
    if not t or not t[0]: return None
    for v in t:
        if isinstance(v, unreal.Vector) and abs(v.z) < 29999: return v.z
    return None

SOLID = ('Rocks_', 'ShoreRocks_', 'Pebbles_', 'Cliffs_')
SKIP = SOLID + ('FarTrees_', 'Trees_')
scat = [a for a in actors if isinstance(a, unreal.RPGScatter)]
rocks = []                                                    # (x, y, r, top z) of everything cover may legitimately rest on
for a in scat:
    if a.get_actor_label().startswith(SOLID):
        h = a.get_editor_property('instances'); m = a.get_editor_property('mesh')
        ex = m.get_bounds().box_extent if m else unreal.Vector(100, 100, 100)
        for i in range(h.get_instance_count()):
            t = h.get_instance_transform(i, True); s = t.scale3d.x
            rocks.append((t.translation.x, t.translation.y, max(ex.x, ex.y) * s + 50, t.translation.z + ex.z * 2 * s + 50))
def on_rock(x, y, z): return any(math.hypot(x - rx, y - ry) < rr and z < rz for rx, ry, rr, rz in rocks)

total = 0
for a in scat:
    lab = a.get_actor_label()
    if lab.startswith(SKIP): continue
    h = a.get_editor_property('instances'); bad = []
    for i in range(h.get_instance_count()):
        p = h.get_instance_transform(i, True).translation
        g = ground(p.x, p.y)
        if g is not None and p.z > g + ABOVE and not on_rock(p.x, p.y, p.z): bad.append((i, p.z - g))
    if bad:
        for i, _ in sorted(bad, reverse=True): h.remove_instance(i)
        total += len(bad)
        unreal.log(f'FLOAT {lab}: removed {len(bad)} (heights above terrain {min(d for _, d in bad) / 100:.1f}-{max(d for _, d in bad) / 100:.1f} m)')
eas.destroy_actor(probe)
les.save_current_level()
unreal.log(f'FLOAT_DONE removed={total}')
unreal.SystemLibrary.quit_editor()
