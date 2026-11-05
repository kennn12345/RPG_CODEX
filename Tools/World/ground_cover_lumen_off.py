"""Stops the flickering dark patches on the grass in /Game/RPG/Maps/L_WorldVista (user 2026-11-05: flicker stops with
r.Lumen.DiffuseIndirect.Allow 0, so it is Lumen's bounced light, not the sun shadows).
  UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/ground_cover_lumen_off.py" -unattended -RenderOffscreen
Thin, swaying, tens of thousands of grass clumps make a noisy input for Lumen: its scene and distance fields update as the
camera moves, and the bounced light on the ground changes from frame to frame. Every scatter that casts no shadow (grass,
wheat, flowers, tufts, ferns, moss, clover, leaves, reeds, small stones, far trees) now stays OUT of Lumen and the distance
fields; it still RECEIVES light and shadows as before. Shadow casters (trees, bushes, logs, rocks, cliffs) are untouched.
No Rebuild (instances stay where they are). Also keeps water foam from forming around grass. Prints LUMENOFF lines."""
import unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem); eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les.load_level('/Game/RPG/Maps/L_WorldVista')
n = 0
for a in eas.get_all_level_actors():
    if not isinstance(a, unreal.RPGScatter): continue
    c = a.get_editor_property('instances')
    if c.get_editor_property('cast_shadow'): continue
    c.set_editor_property('affect_dynamic_indirect_lighting', False)
    c.set_editor_property('affect_distance_field_lighting', False)
    n += 1; unreal.log(f'LUMENOFF {a.get_actor_label()} ({c.get_instance_count()} instances)')
les.save_current_level()
unreal.log(f'LUMENOFF_DONE scatters={n}')
unreal.SystemLibrary.quit_editor()
