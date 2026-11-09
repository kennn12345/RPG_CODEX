"""Verify the saved terrain material and capture overhead and valley views."""
import unreal,os,json,traceback
from pathlib import Path
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
ues=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
eas=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world=ues.get_editor_world()
assert world.get_name()=='L_WorldMap_Refined'
land=next(a for a in eas.get_all_level_actors() if isinstance(a,unreal.Landscape))
mat=land.get_editor_property('landscape_material')
assert mat.get_name()=='M_WorldMap_RegionalTerrain',mat.get_path_name()
assert unreal.MaterialEditingLibrary.get_material_property_input_node(mat,unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET) is None
output=Path(os.environ['RPG_LAND_LOOK_OUTPUT'])
views=[('world-map-land-look-overhead.png',(0,0,800000),unreal.Rotator(pitch=-90,yaw=-90),2800,1800,90),
    ('world-map-land-look-valley.png',(-480000,170000,30000),unreal.MathLibrary.find_look_at_rotation(unreal.Vector(-480000,170000,30000),unreal.Vector(-451000,135000,3500)),1800,1125,70)]
ticks=0;index=0;camera=None;task=None;armed=False
def tick(delta):
    global ticks,index,camera,task,armed
    ticks+=1
    if ticks<160 or armed:return
    if task is not None:
        if not task.is_task_done():return
        eas.destroy_actor(camera);camera=None;task=None;index+=1
        if index>=len(views):
            unreal.unregister_slate_post_tick_callback(handle)
            unreal.log('WORLD_MAP_LAND_LOOK_RELOAD_VERIFIED '+mat.get_path_name())
            unreal.SystemLibrary.quit_editor();return
    armed=True
    try:
        name,loc,rot,w,h,fov=views[index]
        camera=eas.spawn_actor_from_class(unreal.CameraActor,unreal.Vector(*loc),rot)
        camera.set_actor_location(unreal.Vector(*loc),False,False);camera.set_actor_rotation(rot,False)
        cc=camera.get_component_by_class(unreal.CameraComponent);cc.set_field_of_view(fov);cc.set_aspect_ratio(w/h)
        task=unreal.AutomationLibrary.take_high_res_screenshot(w,h,str(output/name),camera=camera,delay=3.0)
    except Exception:
        unreal.log_error('WORLD_MAP_LAND_LOOK_REVIEW_FAILED '+traceback.format_exc())
        unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor()
    armed=False
handle=unreal.register_slate_post_tick_callback(tick)
