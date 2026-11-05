"""Sets the Sun's forward shading priority to 1 (Moon stays 0) in the maps that have both lights, so Unreal stops warning
that multiple directional lights are competing (user 2026-11-05). Only that one property changes - atmosphere slots
(Sun 0, Moon 1), intensities, colours and the day/night cycle (RPGSkyCycle) are untouched. build_vista.py and
build_asset_review.py set the same value when they rebuild a map.
  UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/fix_light_priority.py" -unattended -RenderOffscreen"""
import unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem); eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for mp in ('/Game/RPG/Maps/L_WorldVista', '/Game/RPG/Maps/L_AssetReview'):
    les.load_level(mp)
    for a in eas.get_all_level_actors():
        if a.get_actor_label() == 'Sun':
            for c in a.get_components_by_class(unreal.DirectionalLightComponent):
                c.set_editor_property('forward_shading_priority', 1)
                unreal.log(f'LIGHTFIX {mp} Sun forward_shading_priority=1')
    les.save_current_level()
unreal.log('LIGHTFIX_DONE')
unreal.SystemLibrary.quit_editor()
