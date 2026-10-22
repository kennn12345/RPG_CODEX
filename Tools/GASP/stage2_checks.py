import unreal,json,time,traceback
from pathlib import Path
_gasp_stage2_out=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())) / 'GASPIntegration' / 'Stage2'
_gasp_stage2_out.mkdir(parents=True,exist_ok=True)
_gasp_stage2_result={'passed':False,'saved':True,'phase':'testing'}
_gasp_stage2_state={'phase':'after','world_start':None,'ended_at':None,'started':time.monotonic(),'shots':set(),'events':set(),'subsystem':None,'max_speed':0.0,'jump_seen':False,'sprint_seen':False,'crouch_seen':False}
_gasp_stage2_level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
_gasp_stage2_editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
_gasp_stage2_bp=unreal.load_asset('/Game/RPG/Player/BP_RPGPlayer')
_gasp_stage2_gm=unreal.load_asset('/Game/ThirdPerson/Blueprints/BP_ThirdPersonGameMode')
_gasp_stage2_gm_cdo=unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class('/Game/ThirdPerson/Blueprints/BP_ThirdPersonGameMode'))
_gasp_stage2_player_class=unreal.EditorAssetLibrary.load_blueprint_class('/Game/RPG/Player/BP_RPGPlayer')
assert _gasp_stage2_bp and _gasp_stage2_player_class
assert _gasp_stage2_gm_cdo.get_editor_property('default_pawn_class') == _gasp_stage2_player_class
assert _gasp_stage2_level.load_level('/Game/ThirdPerson/Lvl_ThirdPerson')

def _gasp_stage2_prop(obj, *names):
    for name in names:
        try: return obj.get_editor_property(name)
        except Exception: pass
    raise RuntimeError('Could not read '+str(names)+' on '+str(obj))

def _gasp_stage2_shot(label):
    pass

def _gasp_stage2_action(name):
    return unreal.find_object(_gasp_stage2_state['input'],name)

def _gasp_stage2_press(name):
    action=_gasp_stage2_action(name)
    assert action, 'Missing RPG action '+name
    _gasp_stage2_state['subsystem'].inject_input_vector_for_action(action,unreal.Vector(1,0,0),[],[])

def _gasp_stage2_move(value):
    _gasp_stage2_state['move_value']=value
def _gasp_stage2_finish(error=None):
    if error: _gasp_stage2_result['error'] = error
    _gasp_stage2_result['phase'] = 'functional_test_complete'
    (_gasp_stage2_out/'ReloadFunctionalResult.json').write_text(json.dumps(_gasp_stage2_result,indent=2))
    unreal.log('GASP_STAGE2_FUNCTIONAL_RESULT '+json.dumps(_gasp_stage2_result))
    _gasp_stage2_level.editor_request_end_play()
    unreal.unregister_slate_post_tick_callback(_gasp_stage2_handle)
    unreal.SystemLibrary.quit_editor()
    # This test reads the saved player and closes the editor after writing its result.


def _gasp_stage2_tick(dt):
    try:
        state = _gasp_stage2_state
        if time.monotonic()-state['started'] > 150:
            _gasp_stage2_finish('Functional test timed out')
            return
        world = _gasp_stage2_editor.get_game_world()
        if state['phase'] == 'switching':
            if world: return
            if time.monotonic()-state['ended_at'] < 1.0: return
            _gasp_stage2_gm_cdo.modify()
            _gasp_stage2_gm.modify()
            _gasp_stage2_gm_cdo.set_editor_property('default_pawn_class',_gasp_stage2_player_class)
            state['phase'] = 'after'
            state['world_start'] = None
            _gasp_stage2_level.editor_request_begin_play()
            return
        if not world: return
        pc = unreal.GameplayStatics.get_player_controller(world,0)
        pawn = pc.get_controlled_pawn() if pc else None
        if not pawn: return
        now = unreal.GameplayStatics.get_time_seconds(world)
        if now < 1.0: return
        if state['world_start'] is None:
            state['world_start'] = now
            if state['phase'] == 'after':
                assert pawn.get_class().get_name() == 'BP_RPGPlayer_C', 'Game mode did not spawn BP_RPGPlayer: '+pawn.get_class().get_name()
                state['input'] = pawn.get_component_by_class(unreal.load_class(None,'/Script/RPG_CODEX.RPGControllerInputComponent'))
                state['bridge'] = pawn.get_component_by_class(unreal.load_class(None,'/Script/RPG_CODEX.RPGGASPInputBridgeComponent'))
                assert state['input'] and state['bridge'], 'RPG input or bridge component is missing'
                _gasp_stage2_result['input_diagnostics'] = dict(subsystems=[str(s) for s in unreal.ObjectIterator(unreal.EnhancedInputLocalPlayerSubsystem)],clone=str(unreal.find_object(state['bridge'],'IMC_RPGGASPLocomotion')),input_ready=bool(state['bridge'].input_ready),state_ready=bool(state['bridge'].state_ready),iterator_doc=unreal.ObjectIterator.__doc__)
                state['subsystem'] = next(s for s in unreal.ObjectIterator(unreal.EnhancedInputLocalPlayerSubsystem) if not s.get_name().startswith('Default__'))
                _gasp_stage2_result['selected_subsystem']=state['subsystem'].get_path_name()
                _gasp_stage2_result['value_doc']=unreal.InputActionValue.__doc__
                mesh = pawn.get_component_by_class(unreal.SkeletalMeshComponent)
                asset = mesh.get_skeletal_mesh_asset()
                _gasp_stage2_result.update(pawn=pawn.get_class().get_name(),mesh=asset.get_path_name(),skeleton=asset.get_editor_property('skeleton').get_path_name(),anim_instance=mesh.get_anim_instance().get_class().get_name(),controller=pc.get_class().get_name())
        t = now-state['world_start']
        if state['phase'] == 'before':
            if 0.7 < t < 2.4: pawn.add_movement_input(unreal.Vector(1,0,0),1.0,False)
            if t >= 1.8: _gasp_stage2_shot('before')
            if t >= 2.6:
                _gasp_stage2_level.editor_request_end_play()
                state['phase'] = 'switching'
                state['ended_at'] = time.monotonic()
            return
        if state.get('move_value',0):
            state['subsystem'].inject_input_vector_for_action(unreal.load_asset('/Game/Input/IA_Move'),unreal.Vector(0,state['move_value'],0),[],[])
        velocity = pawn.get_velocity()
        speed = (velocity.x**2 + velocity.y**2)**0.5
        state['max_speed'] = max(state['max_speed'],speed)
        combat = pawn.get_component_by_class(unreal.load_class(None,'/Script/RPG_CODEX.RPGCombatStateComponent'))
        if combat and _gasp_stage2_prop(combat,'is_sprinting','bIsSprinting'):
            state['sprint_seen'] = True
        if _gasp_stage2_prop(pawn,'is_crouched','bIsCrouched'):
            state['crouch_seen'] = True
        if t >= 8.7 and pawn.get_velocity().z > 50:
            state['jump_seen'] = True
        for key,when,fn in [('walk',1.0,lambda:_gasp_stage2_move(0.35)),('run',2.7,lambda:_gasp_stage2_move(1.0)),('sprint',4.4,lambda:_gasp_stage2_press('IA_Sprint')),('stop_sprint',6.0,lambda:_gasp_stage2_press('IA_Sprint')),('stop',6.3,lambda:_gasp_stage2_move(0)),('crouch',7.0,lambda:_gasp_stage2_press('IA_CrouchStealth')),('uncrouch',7.8,lambda:_gasp_stage2_press('IA_CrouchStealth')),('jump',8.7,lambda:_gasp_stage2_press('IA_JumpInteractFlight'))]:
            if t >= when and key not in state['events']:
                state['events'].add(key)
                fn()
        for label,when in [('idle',0.7),('walk',2.3),('run',3.8),('sprint',5.4),('stop',6.8),('crouch',7.5),('jump',8.95)]:
            if t >= when: _gasp_stage2_shot(label)
        if t >= 12.0:
            subsystem=state['subsystem']
            action_names=['IA_Sprint','IA_CrouchStealth','IA_JumpInteractFlight','IA_RightAttack','IA_Defense','IA_Kick','IA_CameraButton']
            keys={name:[str(key) for key in subsystem.query_keys_mapped_to_action(_gasp_stage2_action(name))] for name in action_names}
            for name in ['IA_Move','IA_Look','IA_Look_Gamepad']:
                keys[name]=[str(key) for key in subsystem.query_keys_mapped_to_action(unreal.load_asset('/Game/Input/'+name))]
            _gasp_stage2_result['mapped_keys']=keys
            assert all(keys.values()), 'A required action has no key mappings'
            _gasp_stage2_result['context_priorities']={name:str(subsystem.has_mapping_context(unreal.load_asset('/Game/Input/'+name))) for name in ['IMC_Default','IMC_MouseLook','IMC_Sandbox']}
            _gasp_stage2_result.update(input_ready=bool(state['bridge'].input_ready),state_ready=bool(state['bridge'].state_ready),max_speed=state['max_speed'],sprint_seen=state['sprint_seen'],crouch_seen=state['crouch_seen'],jump_seen=state['jump_seen'])
            _gasp_stage2_result['passed'] = all([_gasp_stage2_result['input_ready'],_gasp_stage2_result['state_ready'],state['sprint_seen'],state['crouch_seen'],state['jump_seen'],state['max_speed']>300])
            _gasp_stage2_finish(None if _gasp_stage2_result['passed'] else 'One or more movement/input checks failed')
    except Exception:
        _gasp_stage2_finish(traceback.format_exc())
_gasp_stage2_handle = unreal.register_slate_post_tick_callback(_gasp_stage2_tick)
_gasp_stage2_level.editor_request_begin_play()
unreal.log('GASP_STAGE2_FUNCTIONAL_CHECKS_STARTED')











