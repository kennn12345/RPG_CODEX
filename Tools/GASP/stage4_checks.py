import unreal,json,time,traceback
from pathlib import Path
out=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())) / 'GASPIntegration' / 'Stage4'
out.mkdir(parents=True,exist_ok=True)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
result={'passed':False}
state={'started':time.monotonic(),'start':None,'events':set(),'samples':{},'flags':{},'montages':{},'move':False,'hold':False,'proxy_yaw':0,'proxy_pitch':0,'feet':[],'distance':0,'last':None}
def action_event(info):
    result['actions'].append(str(info.action))

def pose(mesh):
    return {name:mesh.get_socket_transform(name,unreal.RelativeTransformSpace.RTS_COMPONENT).translation for name in ['pelvis','spine_05','hand_r','foot_l','thigh_l']}
def distance(a,b):return ((a.x-b.x)**2+(a.y-b.y)**2+(a.z-b.z)**2)**.5
def average(samples,name):
    values=[s[name] for s in samples]
    return unreal.Vector(x=sum(v.x for v in values)/len(values),y=sum(v.y for v in values)/len(values),z=sum(v.z for v in values)/len(values))
def play(slot):
    montage=state['anim'].play_slot_animation_as_dynamic_montage(state['clip'],slot,.1,.1,1.0,3,-1.0,0.0)
    assert montage,'Montage could not start in '+slot
    state['montages'][slot]=montage
    state['flags'][slot+'_started']=True
    return montage
def stop(slot):
    state['anim'].montage_stop(.1,state['montages'][slot])
def pulse(name):
    action=unreal.find_object(state['input'],name)
    assert action
    state['subsystem'].inject_input_vector_for_action(action,unreal.Vector(1,0,0),[],[])
def finish(error=None):
    if error:result['error']=error
    result['flags']=state['flags']
    (out/'AnimationResult.json').write_text(json.dumps(result,indent=2))
    unreal.log('GASP_STAGE4_ANIMATION_RESULT '+json.dumps(result))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()
def tick(dt):
    try:
        if time.monotonic()-state['started']>150:finish('Animation check timed out');return
        world=editor.get_game_world()
        if not world:return
        pc=unreal.GameplayStatics.get_player_controller(world,0)
        pawn=pc.get_controlled_pawn() if pc else None
        now=unreal.GameplayStatics.get_time_seconds(world)
        if not pawn or now<1:return
        if state['start'] is None:
            mesh=pawn.get_component_by_class(unreal.SkeletalMeshComponent)
            anim=mesh.get_anim_instance()
            result['actual_anim_class']=anim.get_class().get_path_name()
            assert anim.get_class().get_name()=='SandboxCharacter_CMC_ABP_C','The player did not retain the integrated CMC AnimBP'
            assert isinstance(anim,unreal.RPGAnimInstance),'The RPG animation parent is missing'
            mesh.visibility_based_anim_tick_option=unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES
            state.update(start=now,pawn=pawn,pc=pc,mesh=mesh,anim=anim,input=pawn.get_component_by_class(unreal.RPGControllerInputComponent),camera=pawn.get_component_by_class(unreal.RPGCameraModeComponent),subsystem=next(s for s in unreal.ObjectIterator(unreal.EnhancedInputLocalPlayerSubsystem) if not s.get_name().startswith('Default__')),clip=unreal.load_asset('/Game/Characters/UEFN_Mannequin/Animations/Idle/M_Neutral_Crouch_Idle_Loop'))
            assert state['clip'].get_editor_property('skeleton')==mesh.get_skeletal_mesh_asset().get_editor_property('skeleton')
            result['rpg_parent']=True
            result['actions']=[]
            pawn.get_component_by_class(unreal.RPGCombatStateComponent).on_player_action.add_callable(action_event)
            unreal.SystemLibrary.execute_console_command(world,'rpg.Arena')
            prototype=pawn.get_component_by_class(unreal.RPGPrototypeAnimationComponent)
            state['clip']=unreal.RPGGASPAssetLibrary.create_guard_test_fixture(prototype,state['clip'])
            assert state['clip'],'Could not create PIE-only fixture'
            result['temporary_uefn_guard_fixture']=True
            result['temporary_in_place_fixture']=True
        t=now-state['start'];mesh=state['mesh'];anim=state['anim']
        if state['move']:state['subsystem'].inject_input_vector_for_action(unreal.load_asset('/Game/Input/IA_Move'),unreal.Vector(0,1,0),[],[])
        if state['hold']:pulse('IA_RightAttack');pulse('IA_Defense')
        if state.get('attack_hold',False):pulse('IA_RightAttack')
        for label,start,end in [('baseline',.6,1.5),('full_body',2.6,3.8),('restored',5.0,5.7),('upper_body',6.8,8.2)]:
            if start<t<end:state['samples'].setdefault(label,[]).append(pose(mesh))
        for slot,montage in state['montages'].items():
            if anim.montage_is_playing(montage):state['flags'][slot+'_playing']=True
        if 10.5<t<12.4:
            foot=mesh.get_socket_transform('foot_l',unreal.RelativeTransformSpace.RTS_COMPONENT).translation
            state['feet'].append([foot.x,foot.y,foot.z])
            result['movement_diagnostics']=dict(velocity=str(pawn.get_velocity()),last_input=str(pawn.get_last_movement_input_vector()),movement_mode=str(pawn.get_movement_component().get_editor_property('movement_mode')),subsystem=state['subsystem'].get_path_name(),ignore_move=pc.is_move_input_ignored(),input_ready=bool(pawn.get_component_by_class(unreal.RPGGASPInputBridgeComponent).input_ready))
            pos=pawn.get_actor_location()
            if state['last'] is not None:state['distance']+=distance(pos,state['last'])
            state['last']=pos
        if 13.5<t<20.2:
            state['proxy_yaw']=max(state['proxy_yaw'],abs(anim.get_editor_property('aim_yaw')))
            state['proxy_pitch']=max(state['proxy_pitch'],abs(anim.get_editor_property('aim_pitch')))
        if 16.8<t<17.6:
            state['flags']['first_person_proxy']=bool(state['camera'].is_first_person and anim.get_editor_property('pitch_about_pivot'))
            shadow=unreal.find_object(pawn,'FirstPersonShadowBody')
            state['flags']['rpg_shadow_parent']=bool(shadow and isinstance(shadow.get_anim_instance(),unreal.RPGAnimInstance))
        events=[('full',2,lambda:play('DefaultSlot')),('full_stop',4.2,lambda:stop('DefaultSlot')),('upper',6.2,lambda:play('UpperBody')),('upper_stop',8.7,lambda:stop('UpperBody')),('walking_upper',10,lambda:play('UpperBody')),('move',10,lambda:state.update(move=True)),('move_end',12.5,lambda:state.update(move=False)),('upper_end',12.5,lambda:stop('UpperBody')),('guard',13,lambda:state.update(hold=True)),('aim',13.5,lambda:pc.set_control_rotation(unreal.Rotator(pitch=15,yaw=35,roll=0))),('first_person',16,lambda:pulse('IA_CameraButton')),('guard_release',18,lambda:state.update(hold=False)),('attack_aim',18.5,lambda:state.update(attack_hold=True)),('attack_release',19.5,lambda:state.update(attack_hold=False))]
        for key,when,fn in events:
            if t>=when and key not in state['events']:state['events'].add(key);fn()
        if t>=20.5:
            poses={label:{name:average(samples,name) for name in samples[0]} for label,samples in state['samples'].items()}
            assert len(poses)==4,'Pose sampling did not complete'
            full_pelvis=distance(poses['baseline']['pelvis'],poses['full_body']['pelvis'])
            upper_pelvis=distance(poses['restored']['pelvis'],poses['upper_body']['pelvis'])
            upper_hand=distance(poses['restored']['hand_r'],poses['upper_body']['hand_r'])
            spans=[max(v[i] for v in state['feet'])-min(v[i] for v in state['feet']) for i in range(3)]
            result.update(full_body_pelvis_change_cm=full_pelvis,upper_body_pelvis_change_cm=upper_pelvis,upper_body_hand_change_cm=upper_hand,walking_distance_cm=state['distance'],walking_foot_span_cm=spans,proxy_yaw=state['proxy_yaw'],proxy_pitch=state['proxy_pitch'])
            result['passed']=all(state['flags'].get(k,False) for k in ['DefaultSlot_started','DefaultSlot_playing','UpperBody_started','UpperBody_playing','rpg_shadow_parent','first_person_proxy']) and full_pelvis>10 and upper_pelvis<10 and upper_hand>3 and state['distance']>75 and max(spans)>10 and state['proxy_yaw']>1 and state['proxy_pitch']>3
            finish(None if result['passed'] else 'A slot, layering, locomotion or RPG proxy check failed')
    except Exception:finish(traceback.format_exc())
assert level.load_level('/Game/ThirdPerson/Lvl_ThirdPerson')
handle=unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()






