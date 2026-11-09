"""Pose an orthodox boxing stance on the UEFN skeleton and save Blender/RPG_FightStance.blend.
Run: blender -b Blender/RPG_Player.blend --python <this file>

Works in Blender world space (armature at identity): character faces -Y, its left is +X.
Every pose is a world-space rotation delta applied to the bone's rest orientation, so
bone rolls from the Unreal import never matter. Limbs use analytic two-bone IK."""
import math
import bpy
from mathutils import Matrix, Vector

OUT = "C:/UNREAL PROJECTS/RPG_CODEX/Blender/RPG_FightStance.blend"
ACTION = "FightStance"
FRAMES = (1, 30)

# --- Stance tuning (cm / degrees, world space) ---------------------------------------------
PELVIS_POS = Vector((-1.0, 2.0, 83.0))  # dropped ~10 cm, weight centred between the feet
PELVIS_YAW, PELVIS_TILT = -30.0, 4.0     # right hip back (bladed); slight forward tilt
SPINE = {                                # world yaw / forward lean per spine bone
    "spine_01": (-31, 5), "spine_02": (-33, 9), "spine_03": (-35, 13),
    "spine_04": (-37, 16), "spine_05": (-38, 18),
}
CLAVICLE_RAISE = 8.0                     # shoulders shrugged up to protect the chin
NECK = {"neck_01": (-20, 14), "neck_02": (-6, 6)}  # head turned back to face the opponent, chin down

FOOT_L = (Vector((13.0, -19.0, 9.4)), -12.0)   # lead foot forward, toes slightly in
FOOT_R = (Vector((-15.0, 21.0, 9.4)), -45.0)   # rear foot back, turned out
KNEE_POLE_L = Vector((0.25, -1.0, 0.0))
KNEE_POLE_R = Vector((-0.3, -0.9, 0.0))

WRIST_L = Vector((10.0, -40.0, 130.0))  # lead fist out in front of the face
WRIST_R = Vector((-8.0, -15.0, 127.0))  # rear fist at the right cheek/chin
ELBOW_POLE_L = Vector((0.35, 0.1, -1.0))  # elbows down, tucked over the ribs
ELBOW_POLE_R = Vector((-0.35, 0.1, -1.0))
HAND_TWIST_L, HAND_TWIST_R = 0.0, 0.0     # extra roll about the forearm
WRIST_BEND_L, WRIST_BEND_R = 0.0, 0.0
FIST_CURL = (85.0, 100.0)                 # finger _01, _02 curl (metacarpal adds 10)
THUMB_CURL = (40.0, 60.0)
# -------------------------------------------------------------------------------------------

arm = bpy.data.objects["UEFN_Armature"]
pose, bones = arm.pose, arm.data.bones
scene = bpy.context.scene


def update():
    bpy.context.view_layer.update()


def Rz(deg):
    return Matrix.Rotation(math.radians(deg), 3, 'Z')


def Rx(deg):
    return Matrix.Rotation(math.radians(deg), 3, 'X')


def rest_head(name):
    return bones[name].head_local.copy()


def posed_head(name):
    b = bones[name]
    if b.parent is None:
        return b.head_local.copy()
    rel = b.parent.matrix_local.inverted() @ b.matrix_local
    return (pose.bones[b.parent.name].matrix @ rel).translation


def set_world(name, R, head=None):
    """Give a bone the world rotation R relative to its rest orientation."""
    m = (R @ bones[name].matrix_local.to_3x3()).to_4x4()
    m.translation = head if head is not None else posed_head(name)
    pose.bones[name].matrix = m
    update()


def world_delta(name):
    return pose.bones[name].matrix.to_3x3() @ bones[name].matrix_local.to_3x3().inverted()


def frame(d, pole):
    x = d.normalized()
    z = (pole - x * pole.dot(x)).normalized()
    return Matrix((x, z.cross(x), z)).transposed()


def aim(rest_d, rest_pole, d, pole):
    return frame(d, pole) @ frame(rest_d, rest_pole).transposed()


def perp(v, axis):
    a = axis.normalized()
    return v - a * v.dot(a)


def limb(upper, lower, end, target, pole, rest_pole=None):
    A, B, C = rest_head(upper), rest_head(lower), rest_head(end)
    l1, l2 = (B - A).length, (C - B).length
    if rest_pole is None:
        rest_pole = perp(B - A, C - A)
    root = posed_head(upper)
    d = target - root
    dist = min(max(d.length, abs(l1 - l2) + 0.5), l1 + l2 - 0.01)
    dn = d.normalized()
    a = (l1 * l1 + dist * dist - l2 * l2) / (2 * dist)
    h = math.sqrt(max(l1 * l1 - a * a, 0.0))
    p = perp(pole, dn).normalized()
    mid = root + dn * a + p * h
    tip = root + dn * dist
    R_up = aim(B - A, rest_pole, mid - root, perp(mid - root, tip - root) if h > 1e-3 else p)
    set_world(upper, R_up)
    R_lo = aim(C - B, rest_pole, tip - mid, perp(mid - root, tip - root) if h > 1e-3 else p)
    set_world(lower, R_lo)
    bend = 180.0 - math.degrees((root - mid).angle(tip - mid))
    return R_lo, bend


def curl_finger(chain, angles, side):
    """Curl a finger chain toward the palm about the knuckle line (local rotations)."""
    across = rest_head("pinky_01_" + side) - rest_head("index_01_" + side)
    # Rest A-pose: palms face the thighs, i.e. toward the body midline.
    palm = Vector((-1.0, 0.0, 0.0)) if side == "l" else Vector((1.0, 0.0, 0.0))
    # Bone tails follow Unreal's axes, not the finger, so use the chain's joint positions.
    along = (rest_head(chain[-1]) - rest_head(chain[0])).normalized()
    bend_axis = along.cross(palm).normalized() if chain[0].startswith("thumb") else across.normalized()
    for name, ang in zip(chain, angles):
        b = bones[name]
        for sign in (1, -1):
            q_axis = bend_axis * sign
            moved = Matrix.Rotation(math.radians(ang), 3, q_axis) @ along
            if moved.dot(palm.normalized()) > along.dot(palm.normalized()):
                break
        local_axis = b.matrix_local.to_3x3().inverted() @ q_axis
        pb = pose.bones[name]
        pb.rotation_mode = 'QUATERNION'
        pb.rotation_quaternion = Matrix.Rotation(math.radians(ang), 3, local_axis).to_quaternion()


def copy_ik(ik, src):
    rel = bones[src].matrix_local.inverted() @ bones[ik].matrix_local
    pose.bones[ik].matrix = pose.bones[src].matrix @ rel
    update()


for pb in pose.bones:
    pb.matrix_basis = Matrix.Identity(4)
update()

# Body
set_world("pelvis", Rz(PELVIS_YAW) @ Rx(PELVIS_TILT), PELVIS_POS)
for name, (yaw, lean) in SPINE.items():
    set_world(name, Rz(yaw) @ Rx(lean))
spine_top = world_delta("spine_05")
for side, s in (("l", 1), ("r", -1)):
    fwd_axis = spine_top @ Vector((0, -1, 0))  # raise the clavicle about the torso's forward axis
    set_world("clavicle_" + side, Matrix.Rotation(math.radians(CLAVICLE_RAISE * s), 3, fwd_axis) @ spine_top)
for name, (yaw, pitch) in NECK.items():
    set_world(name, Rz(yaw) @ Rx(pitch))

# Legs
report = {}
for side, (pos, yaw), pole in (("l", FOOT_L, KNEE_POLE_L), ("r", FOOT_R, KNEE_POLE_R)):
    _, report["knee_" + side] = limb("thigh_" + side, "calf_" + side, "foot_" + side, pos, pole,
                                     rest_pole=Vector((0, -1, 0)))
    set_world("foot_" + side, Rz(yaw))

# Arms
for side, target, pole, twist, bend in (("l", WRIST_L, ELBOW_POLE_L, HAND_TWIST_L, WRIST_BEND_L),
                                        ("r", WRIST_R, ELBOW_POLE_R, HAND_TWIST_R, WRIST_BEND_R)):
    R_lo, report["elbow_" + side] = limb("upperarm_" + side, "lowerarm_" + side, "hand_" + side, target, pole)
    fore = (posed_head("hand_" + side) - posed_head("lowerarm_" + side)).normalized()
    R = Matrix.Rotation(math.radians(twist), 3, fore) @ R_lo
    side_axis = (R @ (rest_head("pinky_01_" + side) - rest_head("index_01_" + side))).normalized()
    R = Matrix.Rotation(math.radians(bend), 3, side_axis) @ R
    set_world("hand_" + side, R)

# Fists
for side in ("l", "r"):
    for f in ("index", "middle", "ring", "pinky"):
        curl_finger(["%s_metacarpal_%s" % (f, side), "%s_01_%s" % (f, side), "%s_02_%s" % (f, side)],
                    (10.0,) + FIST_CURL[:2], side)
    curl_finger(["thumb_01_" + side, "thumb_02_" + side], THUMB_CURL, side)
update()

# Unreal IK helper bones follow the real feet/hands
for ik, src in (("ik_foot_l", "foot_l"), ("ik_foot_r", "foot_r"),
                ("ik_hand_gun", "hand_r"), ("ik_hand_l", "hand_l"), ("ik_hand_r", "hand_r")):
    copy_ik(ik, src)

# Key a still, looping clip
action = bpy.data.actions.get(ACTION) or bpy.data.actions.new(ACTION)
arm.animation_data_create()
arm.animation_data.action = action
for f in FRAMES:
    for pb in pose.bones:
        pb.rotation_mode = 'QUATERNION'
        pb.keyframe_insert("location", frame=f, group=pb.name)
        pb.keyframe_insert("rotation_quaternion", frame=f, group=pb.name)
action.use_fake_user = True
scene.frame_start, scene.frame_end = FRAMES
scene.frame_set(FRAMES[0])

bpy.ops.wm.save_as_mainfile(filepath=OUT)
heads = {n: pose.bones[n].head for n in ("hand_l", "hand_r", "neck_02", "pelvis", "upperarm_l", "upperarm_r")}
print("RPG_STANCE_RESULT", {k: round(v, 1) for k, v in report.items()},
      {k: tuple(round(c, 1) for c in v) for k, v in heads.items()})
