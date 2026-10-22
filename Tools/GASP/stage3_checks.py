import unreal,json,time,traceback
from pathlib import Path
out=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())) / 'GASPIntegration' / 'Stage3'
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
out.mkdir(parents=True,exist_ok=True)
result={'passed':False,'actions':[]}
state={'started':time.monotonic(),'start':None,'events':set(),'hold':set(),'move':False,'flags':{},'max_attack_charge':0,'max_kick_charge':0,'max_fly_speed':0}
def action_event(info):
    result['actions'].append(str(info.action))
def pulse(name):
    action=unreal.find_object(state['input'],name)
    assert action,'Missing RPG input '+name
    state['subsystem'].inject_input_vector_for_action(action,unreal.Vector(1,0,0),[],[])
def hold(name,on):
    if on:state['hold'].add(name)
    else:state['hold'].discard(name)
def command(text):
    unreal.SystemLibrary.execute_console_command(editor.get_game_world(),text)
def finish(error=None):
    if error:result['error']=error
    result.update(flags=state['flags'],max_attack_charge=state['max_attack_charge'],max_kick_charge=state['max_kick_charge'],max_fly_speed=state['max_fly_speed'])
    (out/'GameplayResult.json').write_text(json.dumps(result,indent=2))
    unreal.log('GASP_STAGE3_RESULT '+json.dumps(result))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()
def tick(dt):
    try:
        if time.monotonic()-state['started']>160:finish('Gameplay checks timed out');return
        world=editor.get_game_world()
        if not world:return
        pc=unreal.GameplayStatics.get_player_controller(world,0)
        pawn=pc.get_controlled_pawn() if pc else None
        now=unreal.GameplayStatics.get_time_seconds(world)
        if not pawn or now<1:return
        if state['start'] is None:
            assert pawn.get_class().get_name()=='BP_RPGPlayer_C'
            state.update(start=now,pawn=pawn,pc=pc,input=pawn.get_component_by_class(unreal.RPGControllerInputComponent),combat=pawn.get_component_by_class(unreal.RPGCombatStateComponent),camera=pawn.get_component_by_class(unreal.RPGCameraModeComponent),movement=pawn.get_movement_component(),subsystem=next(s for s in unreal.ObjectIterator(unreal.EnhancedInputLocalPlayerSubsystem) if not s.get_name().startswith('Default__')))
            state['combat'].on_player_action.add_callable(action_event)
            command('rpg.Arena')
            result['player']=pawn.get_class().get_name()
        t=now-state['start'];combat=state['combat'];movement=state['movement'];flags=state['flags']
        for name in list(state['hold']):pulse(name)
        if state['move']:state['subsystem'].inject_input_vector_for_action(unreal.load_asset('/Game/Input/IA_Move'),unreal.Vector(1,1,0),[],[])
        state['max_attack_charge']=max(state['max_attack_charge'],combat.attack_charge_percent)
        state['max_kick_charge']=max(state['max_kick_charge'],combat.kick_charge_percent)
        state['max_fly_speed']=max(state['max_fly_speed'],movement.get_editor_property('max_fly_speed'))
        if combat.is_flying:
            flags['flight_started']=True
            if 1.7<t<2.5 and pawn.get_velocity().z>10:flags['flight_rise']=True
            if 2.7<t<5.0 and pawn.get_velocity().length()>50:flags['flight_movement']=True
            if 4.1<t<5.1:
                result['dive_diagnostics']=dict(pitch=pc.get_control_rotation().pitch,velocity=str(pawn.get_velocity()),input_vector=str(pawn.get_last_movement_input_vector()))
                if pawn.get_velocity().z<-10:flags['flight_dive']=True
        if 6.2<t<8.5:
            if not combat.is_flying and movement.is_falling():flags['flight_cancelled']=True
            if movement.is_moving_on_ground():flags['landed']=True
        if combat.is_blocking:flags['blocking']=True
        if state['camera'].is_first_person:flags['first_person']=True
        events=[('jump',1.0,lambda:pulse('IA_JumpInteractFlight')),('flight',1.3,lambda:pulse('IA_JumpInteractFlight')),('rise',1.6,lambda:hold('IA_JumpInteractFlight',True)),('rise_end',2.4,lambda:hold('IA_JumpInteractFlight',False)),('move',2.5,lambda:state.update(move=True)),('boost',3.6,lambda:pulse('IA_Sprint')),('dive',4.0,lambda:pc.set_control_rotation(unreal.Rotator(pitch=-35.0,yaw=0.0,roll=0.0))),('level',5.2,lambda:pc.set_control_rotation(unreal.Rotator(pitch=0.0,yaw=0.0,roll=0.0))),('move_end',5.2,lambda:state.update(move=False)),('exit_flight',5.5,lambda:hold('IA_CrouchStealth',True)),('exit_release',6.5,lambda:hold('IA_CrouchStealth',False)),('tap_attack',8.5,lambda:pulse('IA_RightAttack')),('charge_attack',9.0,lambda:hold('IA_RightAttack',True)),('release_attack',10.3,lambda:hold('IA_RightAttack',False)),('charge_kick',11.0,lambda:hold('IA_Kick',True)),('release_kick',12.3,lambda:hold('IA_Kick',False)),('guard_rt',13.0,lambda:hold('IA_RightAttack',True)),('guard_lt',13.05,lambda:hold('IA_Defense',True)),('guard_end_r',13.8,lambda:hold('IA_RightAttack',False)),('guard_end_l',13.8,lambda:hold('IA_Defense',False)),('first_person',14.3,lambda:pulse('IA_CameraButton')),('third_person',16.0,lambda:pulse('IA_CameraButton')),('stats',17.0,lambda:command('rpg.Stats Player')),('set_stat',17.2,lambda:command('rpg.SetStat Player Strength 12')),('stats_after',17.4,lambda:command('rpg.Stats Player'))]
        for key,when,fn in events:
            if t>=when and key not in state['events']:state['events'].add(key);fn()
        if 15.0<t<15.5:
            mesh=pawn.get_component_by_class(unreal.SkeletalMeshComponent)
            shadow=unreal.find_object(pawn,'FirstPersonShadowBody')
            flags['shadow_exists']=bool(shadow)
            flags['shadow_class_matches']=bool(shadow and shadow.get_anim_instance().get_class()==mesh.get_anim_instance().get_class())
            flags['shadow_no_collision']=bool(shadow and shadow.get_collision_enabled()==unreal.CollisionEnabled.NO_COLLISION)
        if 16.4<t<17.0 and not state['camera'].is_first_person:flags['third_person_restored']=True
        if t>=19:
            result['passed']=all(flags.get(k,False) for k in ['flight_started','flight_rise','flight_movement','flight_dive','flight_cancelled','landed','blocking','first_person','shadow_exists','shadow_class_matches','shadow_no_collision','third_person_restored']) and state['max_attack_charge']>.9 and state['max_kick_charge']>.9
            finish(None if result['passed'] else 'One or more gameplay checks failed; inspect flags and action log')
    except Exception:finish(traceback.format_exc())
assert level.load_level('/Game/ThirdPerson/Lvl_ThirdPerson')
handle=unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()