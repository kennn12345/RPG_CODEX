"""Photograph L_WorldVista from arbitrary cameras inside a hidden editor (no PIE, no C++, no viewport needed): a temporary
SceneCapture2D renders each view into a render target that is exported as PNG. Works on in-memory edits, so changes can
be checked without saving.

    import ground_shots as gs
    gs.shoot([{'name': 'pond_top', 'loc': (x, y, z), 'rot': (pitch, yaw), 'fov': 70}], tag='before', then=callback)

Images: Saved/GroundFix/Shots/<tag>/<name>.png. Needs a running editor loop (run via Tools/World/run_hidden_editor.ps1)."""
import unreal
from pathlib import Path

OUT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())) / 'GroundFix' / 'Shots'

def noon():
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in eas.get_all_level_actors():
        if a.get_class().get_name() == 'RPGSkyCycle':
            a.set_editor_property('time_of_day', 12.0); a.set_time_of_day(12.0)

def shoot(shots, tag, then=None, res=(1600, 900), settle=40, exposure=0.0):
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world = unreal.EditorLevelLibrary.get_editor_world()
    d = OUT / tag; d.mkdir(parents=True, exist_ok=True)
    noon()
    rt = unreal.RenderingLibrary.create_render_target2d(world, res[0], res[1], unreal.TextureRenderTargetFormat.RTF_RGBA8_SRGB)
    cap = eas.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(0, 0, 0))
    cc = cap.get_component_by_class(unreal.SceneCaptureComponent2D)
    cc.set_editor_property('texture_target', rt)
    cc.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    cc.set_editor_property('capture_every_frame', False); cc.set_editor_property('capture_on_movement', False)
    pps = cc.get_editor_property('post_process_settings')
    pps.set_editor_property('override_auto_exposure_method', True); pps.set_editor_property('auto_exposure_method', unreal.AutoExposureMethod.AEM_MANUAL)
    pps.set_editor_property('override_auto_exposure_bias', True); pps.set_editor_property('auto_exposure_bias', 10.0 + exposure)
    pps.set_editor_property('override_auto_exposure_apply_physical_camera_exposure', True); pps.set_editor_property('auto_exposure_apply_physical_camera_exposure', False)
    cc.set_editor_property('post_process_settings', pps); cc.set_editor_property('post_process_blend_weight', 1.0)
    state = {'i': 0, 'wait': 120, 'phase': 0}
    def tick(_dt):
        if state['wait'] > 0:
            state['wait'] -= 1; return
        if state['phase'] == 1:                       # previous view rendered over a few frames: export it
            s = shots[state['i'] - 1]
            unreal.RenderingLibrary.export_render_target(world, rt, str(d), s['name'] + '.png')
            state['phase'] = 0
        if state['i'] >= len(shots):
            unreal.unregister_slate_post_tick_callback(state['h'])
            eas.destroy_actor(cap)
            unreal.log(f'GROUND_SHOTS_DONE {tag} {len(shots)}')
            if then: then()
            return
        s = shots[state['i']]; state['i'] += 1
        p, y = s['rot']
        cap.set_actor_location_and_rotation(unreal.Vector(*s['loc']), unreal.Rotator(roll=0, pitch=p, yaw=y), False, False)
        cc.set_editor_property('fov_angle', s.get('fov', 70.0))
        for _ in range(3): cc.capture_scene()         # repeat so temporal effects settle
        state['phase'] = 1; state['wait'] = settle
    state['h'] = unreal.register_slate_post_tick_callback(tick)
