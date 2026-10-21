import unreal, json, os
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'skeleton_dump.json')

def t2d(t):
    r, p, s = t.rotation, t.translation, t.scale3d
    return {'q': [r.x, r.y, r.z, r.w], 't': [p.x, p.y, p.z], 's': [s.x, s.y, s.z]}

out = {}
mesh = unreal.load_asset('/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple') or unreal.load_asset('/Game/Characters/Mannequins/Meshes/SKM_Manny')
out['mesh'] = mesh.get_path_name() if mesh else None
mod = unreal.SkeletonModifier()
mod.set_skeletal_mesh(mesh)
names = [str(n) for n in mod.get_all_bone_names()]
out['bones'] = names
out['parents'] = {n: str(mod.get_parent_name(n)) for n in names}
out['ref_local'] = {n: t2d(mod.get_bone_transform(n, False)) for n in names}

opts = unreal.AnimPoseEvaluationOptions()
for path in ['/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle']:
    seq = unreal.load_asset(path)
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(seq, 0.0, opts)
    out['idle_local'] = {n: t2d(unreal.AnimPoseExtensions.get_bone_pose(pose, n, unreal.AnimPoseSpaces.LOCAL)) for n in names}
    out['idle_world'] = {n: t2d(unreal.AnimPoseExtensions.get_bone_pose(pose, n, unreal.AnimPoseSpaces.WORLD)) for n in names}
    out['idle_info'] = {'length': seq.get_play_length(), 'rate': str(seq.get_editor_property('target_frame_rate')) if hasattr(seq, 'target_frame_rate') else None}

skel = unreal.load_asset('/Game/Characters/Mannequins/Meshes/SK_Mannequin')
out['skeleton'] = skel.get_path_name()
out['mesh_skeleton'] = mesh.get_editor_property('skeleton').get_path_name()
with open(OUT, 'w') as f:
    json.dump(out, f, indent=1)
unreal.log('SKELETON DUMP OK: %d bones' % len(names))
