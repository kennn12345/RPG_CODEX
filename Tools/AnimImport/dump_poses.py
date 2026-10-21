# Dumps every frame of the retargeted source clips (local and component space, every bone) plus the skeleton's
# reference pose to source_poses.json, for compose.py to cut / blend / mirror outside the editor.
# Run headless with the editor closed (forward slashes in the path):
#   UnrealEditor-Cmd.exe RPG_CODEX.uproject -run=pythonscript -script="C:/.../Tools/AnimImport/dump_poses.py"
import unreal, json, os

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = '/Game/RPG/Animations/Retarget/RamsterZ/'
CLIPS = ['H2H_Idle_Manny', 'H2H_PunchCombo01_Manny', 'H2H_PunchCombo02_Manny', 'H2H_PunchCombo03_Manny', 'H2H_Kick02_Manny']
LOCAL, WORLD = unreal.AnimPoseSpaces.LOCAL, unreal.AnimPoseSpaces.WORLD
opts = unreal.AnimPoseEvaluationOptions()


def tr(t):
    q, p = t.rotation, t.translation
    return [[q.x, q.y, q.z, q.w], [p.x, p.y, p.z]]


def dump_pose(pose, bones):
    return {'local': {b: tr(unreal.AnimPoseExtensions.get_bone_pose(pose, b, LOCAL)) for b in bones},
            'comp': {b: tr(unreal.AnimPoseExtensions.get_bone_pose(pose, b, WORLD)) for b in bones}}


out = {'clips': {}}
skeleton = None
for name in CLIPS:
    seq = unreal.load_asset(SRC + name)
    if seq is None:
        raise RuntimeError('missing ' + name)
    skeleton = seq.get_editor_property('skeleton')
    frames = unreal.AnimationLibrary.get_num_frames(seq)
    length = seq.get_play_length()
    rate = (frames - 1) / length
    poses = []
    bones = None
    for f in range(frames):
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(seq, min(f / rate, length), opts)
        if bones is None:
            bones = [str(b) for b in unreal.AnimPoseExtensions.get_bone_names(pose)]
        poses.append(dump_pose(pose, bones))
    out['clips'][name] = {'fps': round(rate, 3), 'frames': frames, 'length': length, 'poses': poses}
    out['bones'] = bones
    unreal.log('DUMP %s: %d frames at %.2f fps, %d bones' % (name, frames, rate, len(bones)))

ref = unreal.AnimPoseExtensions.get_reference_pose(skeleton)
out['ref'] = dump_pose(ref, out['bones'])
out['skeleton'] = skeleton.get_path_name()
json.dump(out, open(os.path.join(HERE, 'source_poses.json'), 'w'))
unreal.log('DUMP DONE skeleton ' + skeleton.get_path_name())
