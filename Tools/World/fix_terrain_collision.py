"""Make the vista terrain's collision match what you see.

The terrain is Nanite with complex-as-simple collision; Nanite builds collision from its fallback mesh, which was
simplified to 939 triangles (fallback relative error 1.0) against 125,000 visible ones - up to 2.9 m off, so feet sank
into or floated over the ground. Fallback error 0 keeps every triangle in the fallback (and therefore in collision).

Stage of Tools/World/apply_ground_fixes.py (which saves only with RPG_GROUND_SAVE=1). Close the editor before saving:
an open editor keeps the terrain asset file locked."""
import unreal

def fix_terrain_collision(save=False):
    sm = None
    for path in ('/Game/RPG/Environment/World/Meshes/SM_Vista_Far', '/Game/RPG/Environment/World/Meshes/SM_Vista_Terrain'):
        sm = _fix(path, save)          # far land had the same flaw: 49,152 visible vs 3,065 collision triangles
    return sm

def _fix(path, save):
    sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    sm = unreal.load_asset(path)
    ns = sm.get_editor_property('nanite_settings')
    # fallback_target AUTO ignores both values below and picks its own (939-triangle) simplification.
    ns.set_editor_property('fallback_target', unreal.NaniteFallbackTarget.PERCENT_TRIANGLES)
    ns.set_editor_property('fallback_percent_triangles', 1.0)
    ns.set_editor_property('fallback_relative_error', 0.0)
    ns.set_editor_property('generate_fallback', unreal.NaniteGenerateFallback.ENABLED)
    sub.set_nanite_settings(sm, ns, apply_changes=True)
    if save: assert unreal.EditorAssetLibrary.save_loaded_asset(sm, only_if_is_dirty=False), f'{path} save failed (editor open?)'
    unreal.log(f'TERRAIN_COLLISION_FIXED {path} saved={save} fallback_error={sm.get_editor_property("nanite_settings").get_editor_property("fallback_relative_error")}')
    return sm
