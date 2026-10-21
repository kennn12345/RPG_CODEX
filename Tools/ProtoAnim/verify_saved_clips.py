"""Read-only UE commandlet audit: compare saved clips against the current generator output."""
import json
import math
import os
import unreal

project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
clips = json.load(open(os.path.join(project, 'Tools', 'ProtoAnim', 'clips.json')))
report = {}
options = unreal.AnimPoseEvaluationOptions()
for name, clip in clips.items():
    sequence = unreal.load_asset('/Game/RPG/Animations/Prototype/' + name)
    if sequence is None:
        report[name] = {'missing': True}
        continue
    worst_position = 0.0
    worst_rotation = 0.0
    worst_bone = ''
    frames = sorted({0, clip['frames'] // 3, clip['frames'] // 2, 2 * clip['frames'] // 3, clip['frames'] - 1})
    for frame in frames:
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(sequence, frame / clip['fps'], options)
        for bone in ['root', 'pelvis', 'spine_05', 'clavicle_l', 'clavicle_r',
                     'upperarm_l', 'lowerarm_l', 'hand_l', 'upperarm_r', 'lowerarm_r', 'hand_r',
                     'thigh_l', 'calf_l', 'foot_l', 'thigh_r', 'calf_r', 'foot_r']:
            expected_q, expected_t = clip['poses'][frame][bone]
            actual = unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.LOCAL)
            actual_q = actual.rotation
            dot = abs(sum(a * b for a, b in zip(expected_q, [actual_q.x, actual_q.y, actual_q.z, actual_q.w])))
            angle = math.degrees(2 * math.acos(min(1.0, dot)))
            position = (actual.translation - unreal.Vector(*expected_t)).length()
            if angle > worst_rotation:
                worst_rotation, worst_bone = angle, bone
            worst_position = max(worst_position, position)
    report[name] = {'max_position_cm': round(worst_position, 4), 'max_rotation_degrees': round(worst_rotation, 4),
                    'rotation_bone': worst_bone, 'matches_within_compression_tolerance': worst_position < 0.1 and worst_rotation < 1.0}
path = os.path.join(project, 'Saved', 'Logs', 'ContactSavedClipParity.json')
json.dump(report, open(path, 'w'), indent=2)
unreal.log('Saved clip parity audit: ' + path)
