# Prints what this engine version's Python API offers for IK Rig / IK Retargeter scripting.
# Run headless (editor closed):
#   UnrealEditor-Cmd.exe RPG_CODEX.uproject -run=pythonscript -script="<path>/probe_retarget_api.py"
import unreal

def public(obj):
    return sorted(n for n in dir(obj) if not n.startswith('_'))

for name in ['IKRigController', 'IKRetargeterController', 'IKRetargetBatchOperation', 'RetargetSourceOrTarget',
             'AutoMapChainType', 'IKRigDefinitionFactory', 'IKRetargetFactory', 'AnimPoseExtensions']:
    cls = getattr(unreal, name, None)
    unreal.log('PROBE %s: %s' % (name, public(cls) if cls else 'MISSING'))

for name in ['duplicate_and_retarget']:
    fn = getattr(unreal.IKRetargetBatchOperation, name, None) if hasattr(unreal, 'IKRetargetBatchOperation') else None
    unreal.log('PROBE doc %s: %s' % (name, fn.__doc__ if fn else 'MISSING'))
for name in ['set_ik_rig', 'auto_map_chains', 'add_default_ops', 'auto_align_all_bones', 'assign_ik_rig_to_all_ops']:
    fn = getattr(unreal.IKRetargeterController, name, None)
    unreal.log('PROBE doc IKRetargeterController.%s: %s' % (name, (fn.__doc__ or '').split('\n')[0:3] if fn else 'MISSING'))
for name in ['set_skeletal_mesh', 'apply_auto_generated_retarget_definition', 'apply_auto_fbik']:
    fn = getattr(unreal.IKRigController, name, None)
    unreal.log('PROBE doc IKRigController.%s: %s' % (name, (fn.__doc__ or '').split('\n')[0:3] if fn else 'MISSING'))
unreal.log('PROBE DONE')
