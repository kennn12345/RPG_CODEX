"""Run inside UnrealEditor-Cmd -run=pythonscript. Creates/updates the prototype AnimSequences from clips.json."""
import unreal, json, os, sys
try:
    HERE = os.path.dirname(os.path.abspath(__file__))
except NameError:  # some editor execution paths don't define __file__
    HERE = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), 'Tools', 'ProtoAnim')
sys.path.insert(0, HERE)
import skel_profile as SP
FOLDER = SP.P['folder']
clips = json.load(open(SP.path('clips', '.json')))
skel = unreal.load_asset(SP.P['skeleton'])
tools = unreal.AssetToolsHelpers.get_asset_tools()
report = {}

# RPG_IMPORT_ONLY=<prefix> imports only the clips whose names start with it (e.g. RPG_FP_ to leave the approved
# third-person assets untouched).
ONLY = os.environ.get('RPG_IMPORT_ONLY', '')
for name, clip in clips.items():
    if ONLY and not name.startswith(ONLY):
        continue
    path = FOLDER + '/' + name
    seq = unreal.load_asset(path)
    if seq is None:
        factory = unreal.AnimSequenceFactory()
        factory.set_editor_property('target_skeleton', skel)
        seq = tools.create_asset(name, FOLDER, unreal.AnimSequence, factory)
    if seq is None or seq.get_editor_property('skeleton') != skel:
        raise RuntimeError('Could not create animation on SK_Mannequin: ' + path)

    ctrl = seq.controller
    ctrl.open_bracket(unreal.Text('Build prototype ' + name), False)
    ctrl.remove_all_bone_tracks(False)
    ctrl.set_frame_rate(unreal.FrameRate(clip['fps'], 1), False)
    ctrl.set_number_of_frames(unreal.FrameNumber(clip['frames'] - 1), False)
    for bone in clip['bones']:
        keys = [p[bone] for p in clip['poses']]
        ctrl.add_bone_track(bone, False)
        ctrl.set_bone_track_keys(bone,
                                 [unreal.Vector(*t) for _, t in keys],
                                 [unreal.Quat(*q) for q, _ in keys],
                                 [unreal.Vector(1, 1, 1) for _ in keys], False)
    ctrl.close_bracket(False)

    # In-place clips: movement code owns displacement, root never moves or rotates.
    seq.set_editor_property('enable_root_motion', False)
    seq.set_editor_property('force_root_lock', True)
    seq.set_editor_property('root_motion_root_lock', unreal.RootMotionRootLock.REF_POSE)
    # Compress at the rate the clip was baked at, not the project default (30), so fast snaps keep their keys.
    # (read-only to Python, so a small C++ editor helper sets it)
    if not unreal.RPGPrototypeAnimationComponent.set_clip_sample_rate(seq, clip['fps']):
        raise RuntimeError('Could not set the sample rate of ' + path)

    unreal.AnimationLibrary.remove_all_animation_notify_tracks(seq)

    if not unreal.EditorAssetLibrary.save_loaded_asset(seq, False):
        raise RuntimeError('Failed to save ' + path)

    # Verify by evaluating the saved asset through the engine.
    opts = unreal.AnimPoseEvaluationOptions()
    rows = []
    for t in [0.0] + [k for k in clip['keys'][1:]]:
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(seq, min(t, seq.get_play_length()), opts)
        g = lambda b: unreal.AnimPoseExtensions.get_bone_pose(pose, b, unreal.AnimPoseSpaces.WORLD)
        r, h, pl, n = g('root'), g('head'), g('pelvis'), g('neck_01')
        up = n.translation - pl.translation
        rows.append({'t': round(t, 3), 'root_q': [round(v, 4) for v in (r.rotation.x, r.rotation.y, r.rotation.z, r.rotation.w)],
                     'head_z': round(h.translation.z, 1), 'torso_up_cos': round(up.z / up.length(), 3)})
    report[name] = {'length': round(seq.get_play_length(), 3), 'frames': unreal.AnimationLibrary.get_num_frames(seq),
                    'root_lock': str(seq.get_editor_property('root_motion_root_lock')),
                    'root_motion': seq.get_editor_property('enable_root_motion'), 'target_rate': seq.get_editor_property('platform_target_frame_rate').export_text(), 'samples': rows}

json.dump(report, open(SP.path('ue_import_report', '.json'), 'w'), indent=1)
unreal.log('PROTOTYPE IMPORT OK: ' + ', '.join(report))
