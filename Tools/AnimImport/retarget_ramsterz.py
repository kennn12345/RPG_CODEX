# Retargets the RamsterZ Free Anims Volume 1 hand-to-hand clips (UE4 mannequin) onto the project's UE5 Manny, then
# measures every frame: hand / foot speeds and positions, so strikes can be found and cut into single attacks.
#
# Run headless with the editor closed:
#   UnrealEditor-Cmd.exe RPG_CODEX.uproject -run=pythonscript -script="<path>/retarget_ramsterz.py"
# Output: /Game/RPG/Animations/Retarget/ (IK rigs, retargeter, RamsterZ/*_Manny clips) and ramsterz_analysis.json here.
import unreal, json, os

HERE = os.path.dirname(os.path.abspath(__file__))
SRC_MESH = '/Game/RamsterZ_FreeAnims_Volume1/Demo/Mannequin/Character/Mesh/SK_Mannequin'
TGT_MESH = '/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple'
SRC_CLIPS = '/Game/RamsterZ_FreeAnims_Volume1/AnimationSequence/H2H'
OUT = '/Game/RPG/Animations/Retarget'
OUT_CLIPS = OUT + '/RamsterZ'

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
    chains = [str(ch.get_editor_property('chain_name')) for ch in c.get_retarget_chains()]
    unreal.log('RTG rig %s: root %s, chains %s' % (name, c.get_retarget_root(), chains))
    eal.save_loaded_asset(rig, False)
    return rig


def try_call(label, fn, *variants):
    for args in variants:
        try:
            fn(*args)
            unreal.log('RTG %s ok %s' % (label, [str(a) for a in args]))
            return True
        except Exception as e:
            unreal.log_warning('RTG %s failed %s: %s' % (label, [str(a) for a in args], e))
    return False


src_mesh = unreal.load_asset(SRC_MESH)
tgt_mesh = unreal.load_asset(TGT_MESH)
if not src_mesh or not tgt_mesh:
    raise RuntimeError('RTG missing mesh: %s %s' % (src_mesh, tgt_mesh))

src_rig = make_rig('IK_UE4Mannequin', src_mesh)
tgt_rig = make_rig('IK_UE5Manny', tgt_mesh)

rtg = fresh('RTG_UE4ToManny', unreal.IKRetargeter, unreal.IKRetargetFactory())
rc = unreal.IKRetargeterController.get_controller(rtg)
rc.set_ik_rig(SOURCE, src_rig)
rc.set_ik_rig(TARGET, tgt_rig)
rc.add_default_ops()
try_call('assign_ik_rig_to_all_ops', rc.assign_ik_rig_to_all_ops, (TARGET, tgt_rig), (tgt_rig,))
rc.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
try_call('auto_align_all_bones', rc.auto_align_all_bones, (TARGET,), ())
eal.save_loaded_asset(rtg, False)

# Retarget every clip in the hand-to-hand folder.
clip_paths = [p.split('.')[0] for p in eal.list_assets(SRC_CLIPS, recursive=False) if eal.find_asset_data(p).asset_class_path.asset_name == 'AnimSequence']
for p in clip_paths:
    target = OUT_CLIPS + '/' + p.split('/')[-1] + '_Manny'
    if eal.does_asset_exist(target):
        eal.delete_asset(target)
assets = [eal.find_asset_data(p) for p in clip_paths]
made = unreal.IKRetargetBatchOperation.duplicate_and_retarget(assets, src_mesh, tgt_mesh, rtg, '', '', '', '_Manny', OUT_CLIPS, False, False, True)
unreal.log('RTG retargeted %d clips' % len(made))
eal.save_directory(OUT, only_if_is_dirty=False, recursive=True)

# Measure: per frame, component-space positions and speeds of the striking limbs.
opts = unreal.AnimPoseEvaluationOptions()
BONES = ['hand_r', 'hand_l', 'foot_r', 'foot_l', 'pelvis', 'head', 'root']
report = {}
for data in made:
    seq = unreal.load_asset(str(data.package_name))
    if not isinstance(seq, unreal.AnimSequence):
        continue
    frames = unreal.AnimationLibrary.get_num_frames(seq)
    length = seq.get_play_length()
    rate = (frames - 1) / length if length > 0 else 30.0
    rows = []
    prev = None
    for f in range(frames):
        t = min(f / rate, length)
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(seq, t, opts)
        pos = {b: unreal.AnimPoseExtensions.get_bone_pose(pose, b, unreal.AnimPoseSpaces.WORLD).translation for b in BONES}
        row = {'f': f, 't': round(t, 3)}
        for b in BONES:
            p = pos[b]
            row[b] = [round(p.x, 1), round(p.y, 1), round(p.z, 1)]
            row[b + '_v'] = round((p - prev[b]).length() * rate, 0) if prev else 0
        rows.append(row)
        prev = pos
    report[seq.get_name()] = {'frames': frames, 'length': round(length, 3), 'rate': round(rate, 2),
                              'root_motion': seq.get_editor_property('enable_root_motion'), 'rows': rows}
    # Strike peaks: local speed maxima of each hand / foot above 400 cm/s.
    peaks = []
    for limb in ['hand_r', 'hand_l', 'foot_r', 'foot_l']:
        v = [r[limb + '_v'] for r in rows]
        for i in range(2, len(v) - 2):
            if v[i] > 400 and v[i] == max(v[i - 2:i + 3]):
                peaks.append('%s@%.2fs(%.0f)' % (limb, rows[i]['t'], v[i]))
    unreal.log('RTG CLIP %s: %d frames, %.2fs, root motion %s | peaks %s' % (seq.get_name(), frames, length,
               seq.get_editor_property('enable_root_motion'), ' '.join(sorted(peaks, key=lambda s: float(s.split('@')[1].split('s')[0])))))

json.dump(report, open(os.path.join(HERE, 'ramsterz_analysis.json'), 'w'), indent=0)
unreal.log('RTG DONE')
