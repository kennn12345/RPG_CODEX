"""Optional capture re-retarget: enable MetaHumanBodyTracker,MetaHuman for this editor session only.
Set RPG_MOCAP_BODY_JSON to the exported solve rows (frame/body/raw_body).
Creates new staging assets only; move Mocap8614Work out of Content after extracting the target data.
No recording or capture plugin is needed to rebake the checked-in target data with capture_getups.py.
"""
import unreal,json,os
from pathlib import Path
rows=json.loads(Path(os.environ['RPG_MOCAP_BODY_JSON']).read_text());tools=unreal.AssetToolsHelpers.get_asset_tools();out='/Game/RPG/Animations/Retarget/Mocap8614Work'
source=unreal.load_asset('/MetaHumanBodyTracker/SKM_Body');target=unreal.load_asset('/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin');assert source and target
factory=unreal.AnimSequenceFactory();factory.set_editor_property('target_skeleton',source.get_editor_property('skeleton'))
seq=tools.create_asset('Capture8614_Source',out,unreal.AnimSequence,factory);assert seq
c=seq.controller;c.open_bracket(unreal.Text('Captured get-up source'),False);c.set_frame_rate(unreal.FrameRate(30,1),False);c.set_number_of_frames(unreal.FrameNumber(len(rows)-1),False)
for b in rows[0]['body']:
 k=[r['body'][b] for r in rows]
 c.add_bone_track(b,False)
 c.set_bone_track_keys(b,[unreal.Vector(*x['p']) for x in k],[unreal.Quat(*x['q']) for x in k],[unreal.Vector(1,1,1) for x in k],False)
c.close_bracket(False);unreal.EditorAssetLibrary.save_loaded_asset(seq)
def rig(name,mesh):
 r=tools.create_asset(name,out,unreal.IKRigDefinition,unreal.IKRigDefinitionFactory());ctrl=unreal.IKRigController.get_controller(r);ctrl.set_skeletal_mesh(mesh);ctrl.apply_auto_generated_retarget_definition();ctrl.apply_auto_fbik();unreal.EditorAssetLibrary.save_loaded_asset(r);return r
src=rig('IK_CaptureSource',source);tgt=rig('IK_CaptureTarget',target)
rtg=tools.create_asset('RTG_Capture8614',out,unreal.IKRetargeter,unreal.IKRetargetFactory());rc=unreal.IKRetargeterController.get_controller(rtg);SOURCE,TARGET=unreal.RetargetSourceOrTarget.SOURCE,unreal.RetargetSourceOrTarget.TARGET
rc.set_ik_rig(SOURCE,src);rc.set_ik_rig(TARGET,tgt);rc.add_default_ops();rc.assign_ik_rig_to_all_ops(TARGET,tgt);rc.auto_map_chains(unreal.AutoMapChainType.EXACT,True)
print('ALIGN_DOC',rc.auto_align_all_bones.__doc__)
rc.auto_align_all_bones(SOURCE)
unreal.EditorAssetLibrary.save_loaded_asset(rtg)
made=unreal.IKRetargetBatchOperation.duplicate_and_retarget([unreal.EditorAssetLibrary.find_asset_data(seq.get_path_name())],source,target,rtg,'','','','_UEFN',out,False,False,True)
assert len(made)==1
ret=unreal.load_asset(str(made[0].package_name));print('RETARGET_CAPTURE',ret.get_path_name())
mod=unreal.SkeletonModifier();mod.set_skeletal_mesh(target);bones=[str(b) for b in mod.get_all_bone_names()]
opts=unreal.AnimPoseEvaluationOptions();poses=[]
for i in range(216):
 pose=unreal.AnimPoseExtensions.get_anim_pose_at_time(ret,i/30,opts)
 d={}
 for b in bones:
  t=unreal.AnimPoseExtensions.get_bone_pose(pose,b,unreal.AnimPoseSpaces.LOCAL);p=t.translation;q=t.rotation;d[b]=([q.x,q.y,q.z,q.w],[p.x,p.y,p.z])
 poses.append(d)
saved=Path(unreal.Paths.project_saved_dir())/'Mocap8614';saved.mkdir(parents=True,exist_ok=True)
(saved/'retargeted.json').write_text(json.dumps(dict(fps=30,bones=bones,poses=poses,frames=len(poses),name='RPG_GetUp_FaceDown_Capture8614')))
print('RETARGET_BAKE_OK',len(poses),len(bones))
