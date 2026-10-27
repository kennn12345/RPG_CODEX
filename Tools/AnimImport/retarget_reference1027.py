"""Audit candidates: native RamsterZ UE4 -> UEFN, without using the old Manny cut/composition.

Creates new staging assets only. Does not switch gameplay, save levels, or edit source animations/NPCs.
Run in UnrealEditor-Cmd -run=pythonscript; source comparison must precede gameplay integration.
"""
import json
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT='/Game/RPG/Animations/Retarget/Reference1027Work'
source=unreal.load_asset('/Game/RamsterZ_FreeAnims_Volume1/Demo/Mannequin/Character/Mesh/SK_Mannequin')
target=unreal.load_asset('/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin')
assert source and target
tools=unreal.AssetToolsHelpers.get_asset_tools()

def asset(name,cls,factory):
    old=unreal.load_asset(OUT+'/'+name)
    return (old,False) if old else (tools.create_asset(name,OUT,cls,factory()),True)

def rig(name,mesh):
    value,new=asset(name,unreal.IKRigDefinition,unreal.IKRigDefinitionFactory)
    if new:
        c=unreal.IKRigController.get_controller(value);c.set_skeletal_mesh(mesh)
        c.apply_auto_generated_retarget_definition();c.apply_auto_fbik()
        unreal.EditorAssetLibrary.save_loaded_asset(value)
    return value

src=rig('IK_ReferenceUE4',source);tgt=rig('IK_ReferenceUEFN',target)
rtg,new=asset('RTG_Reference1027',unreal.IKRetargeter,unreal.IKRetargetFactory)
if new:
    c=unreal.IKRetargeterController.get_controller(rtg)
    S,T=unreal.RetargetSourceOrTarget.SOURCE,unreal.RetargetSourceOrTarget.TARGET
    c.set_ik_rig(S,src);c.set_ik_rig(T,tgt);c.add_default_ops();c.assign_ik_rig_to_all_ops(T,tgt)
    c.auto_map_chains(unreal.AutoMapChainType.EXACT,True);c.auto_align_all_bones(T)
    unreal.EditorAssetLibrary.save_loaded_asset(rtg)

names=['H2H_Idle','H2H_PunchCombo01','H2H_PunchCombo02','H2H_PunchCombo03']
missing=[unreal.EditorAssetLibrary.find_asset_data('/Game/RamsterZ_FreeAnims_Volume1/AnimationSequence/H2H/'+n)
         for n in names if not unreal.EditorAssetLibrary.does_asset_exist(OUT+'/'+n+'_UEFNReference1027')]
if missing:
    made=unreal.IKRetargetBatchOperation.duplicate_and_retarget(missing,source,target,rtg,'','','','_UEFNReference1027',OUT,False,False,True)
    assert len(made)==len(missing)

opts=unreal.AnimPoseEvaluationOptions();clips={}
for name in names:
    seq=unreal.load_asset(OUT+'/'+name+'_UEFNReference1027');assert seq
    count=unreal.AnimationLibrary.get_num_frames(seq);length=seq.get_play_length();fps=(count-1)/length
    poses=[];bones=None
    for i in range(count):
        pose=unreal.AnimPoseExtensions.get_anim_pose_at_time(seq,min(i/fps,length),opts)
        if bones is None:bones=[str(b) for b in unreal.AnimPoseExtensions.get_bone_names(pose)]
        row={}
        for b in bones:
            t=unreal.AnimPoseExtensions.get_bone_pose(pose,b,unreal.AnimPoseSpaces.LOCAL);q=t.rotation;v=t.translation
            row[b]=[[q.x,q.y,q.z,q.w],[v.x,v.y,v.z]]
        poses.append(row)
    clips[name]=dict(fps=fps,frames=count,length=length,bones=bones,poses=poses,source=seq.get_path_name())
    unreal.EditorAssetLibrary.save_loaded_asset(seq)
    print('REFERENCE_CANDIDATE_RETARGET',name,count,fps)
path=ROOT/'Tools/ProtoAnim/captures/reference1027_uefn_sources.json'
path.write_text(json.dumps(dict(source='RamsterZ native UE4 candidate audit; not yet accepted as reference match',clips=clips)))
print('REFERENCE_CANDIDATES_EXPORTED',path)
