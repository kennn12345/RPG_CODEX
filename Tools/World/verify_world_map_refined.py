"""Reload refined terrain, check river channels/collision, and capture its overview."""
import unreal
import json,struct,traceback,os
from pathlib import Path
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
ROOT=Path(unreal.Paths.project_dir())
SOURCE=ROOT/'Tools/World/WorldMapSourceV2'
REPORT=Path(unreal.Paths.project_saved_dir())/'WorldMapRefined'
meta=json.loads((SOURCE/'refinement.json').read_text())
raw=(SOURCE/'heightmap.r16').read_bytes()
eas=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ues=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world=ues.get_editor_world()
assert world.get_name()=='L_WorldMap_Refined'
count=0
camera=None
output=os.environ.get('RPG_REFINED_OVERVIEW','')
def run(delta):
    global count,camera,task
    count+=1
    if count<140:return
    if count==140:
        try:
            actors=eas.get_all_level_actors()
            land=next(a for a in actors if isinstance(a,unreal.Landscape))
            center,extent=land.get_actor_bounds(False)
            assert abs(extent.x*2-1400000)<2 and abs(extent.y*2-900000)<2
            rivers=[a for a in actors if isinstance(a,unreal.RPGStylizedRiver)]
            assert len(rivers)==9
            pieces=0
            for river in rivers:
                components=river.get_components_by_class(unreal.SplineMeshComponent)
                assert len(components)>0,river.get_actor_label()
                expected_pieces=max(1,round(river.get_editor_property('spline').get_spline_length()/river.get_editor_property('piece_length')))
                assert len(components)==expected_pieces,('duplicate river pieces',river.get_actor_label(),len(components),expected_pieces)
                river.rebuild()
                components=river.get_components_by_class(unreal.SplineMeshComponent)
                assert len(components)==expected_pieces,('rebuild added river pieces',river.get_actor_label())
                for comp in components:
                    assert comp.get_collision_enabled()==unreal.CollisionEnabled.NO_COLLISION
                pieces+=len(components)
            checks=[]
            for probe in meta['river_checks']:
                px,py=probe['vertex']
                x=-700000+px*347.22222222222223
                y=-450000+py*357.14285714285717
                expected=(struct.unpack_from('<H',raw,(py*4033+px)*2)[0]-32768)/128*100
                hit=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(x,y,20000),unreal.Vector(x,y,-20000),
                    unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,False,[],unreal.DrawDebugTrace.NONE,True)
                if not hit:
                    # Chaos may reject a ray landing exactly on a shared grid vertex.
                    hit=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(x+0.37,y+0.61,20000),unreal.Vector(x+0.37,y+0.61,-20000),
                        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,False,[],unreal.DrawDebugTrace.NONE,True)
                assert hit,('no terrain hit',probe,x,y,expected,land.get_actor_location())
                result=hit.to_tuple()
                assert result[0],('missing collision',probe)
                actual=result[4].z
                assert abs(actual-expected)<5,(probe,expected,actual)
                assert actual<probe['water_z_cm']-10,('dry river center',probe,actual)
                checks.append(dict(river=probe['river'],bed_cm=actual,water_cm=probe['water_z_cm'],collision_error_cm=abs(actual-expected)))
            report=dict(map=meta['map'],reopened=True,extent_m=[extent.x/50,extent.y/50],rivers=len(rivers),
                river_mesh_pieces=pieces,channel_probes=len(checks),maximum_collision_error_cm=max(c['collision_error_cm'] for c in checks),
                minimum_channel_depth_cm=min(c['water_cm']-c['bed_cm'] for c in checks),passed=True)
            (REPORT/'reload_verification.json').write_text(json.dumps(report,indent=2))
            unreal.log('WORLD_MAP_REFINED_VERIFIED '+json.dumps(report))
            if not output:
                unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor();return
            camera=eas.spawn_actor_from_class(unreal.CameraActor,unreal.Vector(0,0,800000),unreal.Rotator(pitch=-90,yaw=-90))
            camera.set_actor_location(unreal.Vector(0,0,800000),False,False)
            camera.set_actor_rotation(unreal.Rotator(pitch=-90,yaw=-90),False)
            cc=camera.get_component_by_class(unreal.CameraComponent)
            cc.set_field_of_view(90);cc.set_aspect_ratio(14/9)
            task=unreal.AutomationLibrary.take_high_res_screenshot(2800,1800,output,camera=camera,delay=2.0)
        except Exception:
            unreal.log_error('WORLD_MAP_REFINED_VERIFY_FAILED '+traceback.format_exc())
            unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor()
    if count>220 and output and Path(output).exists() and task.is_task_done():
        unreal.unregister_slate_post_tick_callback(handle)
        eas.destroy_actor(camera)
        unreal.log('WORLD_MAP_REFINED_OVERVIEW '+output)
        unreal.SystemLibrary.quit_editor()
handle=unreal.register_slate_post_tick_callback(run)
