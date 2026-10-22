import unreal, json, os, sys
try:
    _HERE = os.path.dirname(os.path.abspath(__file__))
except NameError:
    _HERE = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), 'Tools', 'ProtoAnim')
sys.path.insert(0, _HERE)
import skel_profile as SP
OUT = SP.path('skeleton_dump', '.json')

def t2d(t):
    r, p, s = t.rotation, t.translation, t.scale3d
    return {'q': [r.x, r.y, r.z, r.w], 't': [p.x, p.y, p.z], 's': [s.x, s.y, s.z]}

out = {}
mesh = unreal.load_asset(SP.P['mesh'])
out['mesh'] = mesh.get_path_name() if mesh else None
mod = unreal.SkeletonModifier()
mod.set_skeletal_mesh(mesh)
names = [str(n) for n in mod.get_all_bone_names()]
out['bones'] = names
out['parents'] = {n: str(mod.get_parent_name(n)) for n in names}
out['ref_local'] = {n: t2d(mod.get_bone_transform(n, False)) for n in names}

opts = unreal.AnimPoseEvaluationOptions()
for path in [SP.P['idle']]:
    seq = unreal.load_asset(path)
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(seq, 0.0, opts)
    out['idle_local'] = {n: t2d(unreal.AnimPoseExtensions.get_bone_pose(pose, n, unreal.AnimPoseSpaces.LOCAL)) for n in names}
    out['idle_world'] = {n: t2d(unreal.AnimPoseExtensions.get_bone_pose(pose, n, unreal.AnimPoseSpaces.WORLD)) for n in names}
    out['idle_info'] = {'length': seq.get_play_length(), 'rate': str(seq.get_editor_property('target_frame_rate')) if hasattr(seq, 'target_frame_rate') else None}

skel = unreal.load_asset(SP.P['skeleton'])
out['skeleton'] = skel.get_path_name()
out['mesh_skeleton'] = mesh.get_editor_property('skeleton').get_path_name()
with open(OUT, 'w') as f:
    json.dump(out, f, indent=1)
unreal.log('SKELETON DUMP OK: %d bones' % len(names))
