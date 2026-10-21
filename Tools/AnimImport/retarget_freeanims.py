# Copies Free Animations Pack clips (on the pack's own copy of the UE5 mannequin skeleton, bone-for-bone identical to
# the project's) onto the project's SK_Mannequin, for previewing and use: /Game/RPG/Animations/Retarget/FreeAnims/*_Manny.
# Run headless with the editor closed (forward slashes in the path):
#   UnrealEditor-Cmd.exe RPG_CODEX.uproject -run=pythonscript -script="C:/.../Tools/AnimImport/retarget_freeanims.py"
import unreal

SRC_MESH = '/Game/FreeAnimationsPack/Demo/Characters/Mannequins/Meshes/SKM_Manny'
TGT_MESH = '/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple'
CLIPS = ['AS_GoForward', 'AS_FlyingUp', 'AS_AttackInLevitation2']
OUT = '/Game/RPG/Animations/Retarget'
OUT_CLIPS = OUT + '/FreeAnims'

tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
SOURCE, TARGET = unreal.RetargetSourceOrTarget.SOURCE, unreal.RetargetSourceOrTarget.TARGET


def fresh(name, cls, factory):
    path = OUT + '/' + name
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    return tools.create_asset(name, OUT, cls, factory)


def make_rig(name, mesh):
    rig = fresh(name, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    c = unreal.IKRigController.get_controller(rig)
    c.set_skeletal_mesh(mesh)
    c.apply_auto_generated_retarget_definition()
    c.apply_auto_fbik()
    eal.save_loaded_asset(rig, False)
    return rig


src_mesh = unreal.load_asset(SRC_MESH)
tgt_mesh = unreal.load_asset(TGT_MESH)
if not src_mesh or not tgt_mesh:
    raise RuntimeError('RTG missing mesh: %s %s' % (src_mesh, tgt_mesh))
src_rig = make_rig('IK_FreeAnimsManny', src_mesh)
tgt_rig = unreal.load_asset(OUT + '/IK_UE5Manny') or make_rig('IK_UE5Manny', tgt_mesh)

rtg = fresh('RTG_FreeAnimsToManny', unreal.IKRetargeter, unreal.IKRetargetFactory())
rc = unreal.IKRetargeterController.get_controller(rtg)
rc.set_ik_rig(SOURCE, src_rig)
rc.set_ik_rig(TARGET, tgt_rig)
rc.add_default_ops()
rc.assign_ik_rig_to_all_ops(TARGET, tgt_rig)
rc.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
eal.save_loaded_asset(rtg, False)

for name in CLIPS:
    target = OUT_CLIPS + '/' + name + '_Manny'
    if eal.does_asset_exist(target):
        eal.delete_asset(target)
assets = [eal.find_asset_data('/Game/FreeAnimationsPack/Animations/' + n) for n in CLIPS]
made = unreal.IKRetargetBatchOperation.duplicate_and_retarget(assets, src_mesh, tgt_mesh, rtg, '', '', '', '_Manny', OUT_CLIPS, False, False, True)
eal.save_directory(OUT, only_if_is_dirty=False, recursive=True)
for d in made:
    seq = unreal.load_asset(str(d.package_name))
    unreal.log('FREEANIMS %s: %.2fs, root motion %s' % (seq.get_name(), seq.get_play_length(), seq.get_editor_property('enable_root_motion')))
unreal.log('FREEANIMS DONE')
