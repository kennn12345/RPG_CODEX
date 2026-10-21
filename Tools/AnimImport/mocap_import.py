"""Writes the clips composed by compose.py (mocap_clips.json) as AnimSequences in /Game/RPG/Animations/Mocap, then
checks each against the file by evaluating it through the engine.
Run headless with the editor closed (forward slashes in the path):
  UnrealEditor-Cmd.exe RPG_CODEX.uproject -run=pythonscript -script="C:/.../Tools/AnimImport/mocap_import.py"
"""
import unreal, json, os

HERE = os.path.dirname(os.path.abspath(__file__))
FOLDER = '/Game/RPG/Animations/Mocap'
clips = json.load(open(os.path.join(HERE, 'mocap_clips.json')))
skel = unreal.load_asset('/Game/Characters/Mannequins/Meshes/SK_Mannequin')
tools = unreal.AssetToolsHelpers.get_asset_tools()
opts = unreal.AnimPoseEvaluationOptions()

for name, clip in clips.items():
    path = FOLDER + '/' + name
    seq = unreal.load_asset(path)
    if seq is None:
        factory = unreal.AnimSequenceFactory()
        factory.set_editor_property('target_skeleton', skel)
        seq = tools.create_asset(name, FOLDER, unreal.AnimSequence, factory)
    if seq is None or seq.get_editor_property('skeleton') != skel:
        raise RuntimeError('Could not create animation on SK_Mannequin: ' + path)

    ctrl = seq.controller
    ctrl.open_bracket(unreal.Text('Build mocap ' + name), False)
    ctrl.remove_all_bone_tracks(False)
    ctrl.set_frame_rate(unreal.FrameRate(clip['fps'], 1), False)
    ctrl.set_number_of_frames(unreal.FrameNumber(clip['frames'] - 1), False)
    for bone in clip['bones']:
        keys = [p[bone] for p in clip['poses']]
        ctrl.add_bone_track(bone, False)
        ctrl.set_bone_track_keys(bone, [unreal.Vector(*t) for _, t in keys], [unreal.Quat(*q) for q, _ in keys],
                                 [unreal.Vector(1, 1, 1) for _ in keys], False)
    ctrl.close_bracket(False)

    # Attacks step into the strike: their root travel is root motion (moves the capsule). The stance stays in place.
    stepping = not name.endswith('_Stance')
    seq.set_editor_property('enable_root_motion', stepping)
    seq.set_editor_property('force_root_lock', not stepping)
    seq.set_editor_property('root_motion_root_lock', unreal.RootMotionRootLock.REF_POSE)
    unreal.RPGPrototypeAnimationComponent.set_clip_sample_rate(seq, clip['fps'])
    unreal.AnimationLibrary.remove_all_animation_notify_tracks(seq)
    if not unreal.EditorAssetLibrary.save_loaded_asset(seq, False):
        raise RuntimeError('Failed to save ' + path)

    # Check: the engine's evaluated hand / foot positions match the file at a few frames.
    worst = 0.0
    for f in [0, clip['frames'] // 2, clip['frames'] - 1]:
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_frame(seq, f, opts)
        for bone in ['hand_r', 'foot_l']:
            got = unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.LOCAL).translation
            want = clip['poses'][f][bone][1]
            worst = max(worst, abs(got.x - want[0]), abs(got.y - want[1]), abs(got.z - want[2]))
    unreal.log('MOCAP IMPORT %s: %d frames, %.2fs, worst check error %.3f cm' % (name, clip['frames'], seq.get_play_length(), worst))
unreal.log('MOCAP IMPORT DONE')
