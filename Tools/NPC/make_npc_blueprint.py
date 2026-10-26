"""Creates /Game/RPG/NPC/BP_RPGNpc: a copy of GASP's SandboxCharacter_CMC (motion matching, foot placement) reparented to
the C++ ARPGNpcCharacter (Power System hits, reactions, get-ups) and walked by ARPGNpcAIController. Then rebuilds the
dummy field with NPCs (Tools/Levels/build_dummy_field.py, which saves and closes the editor).
Run: UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="<abs path to this file>" """
import unreal, os

SRC = '/Game/Blueprints/SandboxCharacter_CMC'
DST = '/Game/RPG/NPC/BP_RPGNpc'
eal = unreal.EditorAssetLibrary
if not eal.does_asset_exist(DST):
    eal.duplicate_asset(SRC, DST)
bp = unreal.load_asset(DST)
unreal.BlueprintEditorLibrary.reparent_blueprint(bp, unreal.RPGNpcCharacter.static_class())
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
cdo = unreal.get_default_object(bp.generated_class())
cdo.set_editor_property('ai_controller_class', unreal.RPGNpcAIController.static_class())
cdo.set_editor_property('auto_possess_ai', unreal.AutoPossessAI.PLACED_IN_WORLD_OR_SPAWNED)
cdo.set_editor_property('auto_possess_player', unreal.AutoReceiveInput.DISABLED)
mesh = cdo.get_editor_property('mesh')
skm = mesh.get_editor_property('skeletal_mesh_asset')
unreal.log('NPC BP mesh %s anim %s physics %s parent %s' % (skm.get_path_name() if skm else None, mesh.get_editor_property('anim_class'),
    skm.get_editor_property('physics_asset') if skm else None, isinstance(cdo, unreal.RPGNpcCharacter)))
eal.save_asset(DST)
unreal.log('NPC BLUEPRINT OK')

here = os.path.dirname(os.path.abspath(__file__)) if '__file__' in globals() else 'C:/UNREAL PROJECTS/RPG_CODEX/Tools/NPC'
exec(open(os.path.join(os.path.dirname(here), 'Levels', 'build_dummy_field.py'), encoding='utf-8').read())
