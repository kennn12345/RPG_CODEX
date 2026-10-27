"""Our own get-ups (Docs/GETUP_PLAN.md): a body lying on the ground pushes itself up to GASP's neutral standing idle.
Run with the UEFN skeleton:  RPG_SKEL=uefn python getups.py [name-filter ...]  ->  clips_uefn.json (merged; other clips
kept), contact numbers per key, stick-figure sheets in previews/. Then check the anatomy: python getupcheck.py.

Unlike poses.py, every target here is authored directly in the UEFN mannequin's component space (cm; +Y forward,
+X character-left, +Z up, the root on the floor): get-ups are about where hands, knees and feet touch the ground, so
the ground has to be where the numbers say. In place (root never moves). The last key is a near-neutral stance and the
clip then blends, per bone, into the idle's exact pose, so the hand-off to the idle can't pop.
"""
import json, math, os, sys
import protoanim as pa
from protoanim import with_, sample_track, build_pose, FPS

# idle, for reference: pelvis (2.1, -2.5, 90.6), hands at (+-27, ~-2, 85.5), ankles at L (20, 12.5, 9.4) and
# R (-14.5, -20.4, 9.4) (the idle stands with the left foot a little forward), balls of the feet 3.2 up
IDLE_PELVIS = pa.IDLE_CS['pelvis'][1]
GROUPS = {
    'hips': ('pelvis_off', 'pelvis_rot', 'pelvis_lie', 'square'),
    'torso': ('spine_lo', 'spine_hi'),
    'head': ('head_rot',),
    'arm_l': ('hand_l', 'elbow_pole_l', 'hand_flat_l', 'hand_dir_l', 'hand_open_l', 'clav_l'),
    'arm_r': ('hand_r', 'elbow_pole_r', 'hand_flat_r', 'hand_dir_r', 'hand_open_r', 'clav_r'),
    'leg_l': ('foot_l', 'foot_rot_l', 'foot_turn_l', 'knee_pole_l'),
    'leg_r': ('foot_r', 'foot_rot_r', 'foot_turn_r', 'knee_pole_r'),
}

def unmap_hand(side, v):
    """UEFN-space hand target -> the Manny-space value build_pose expects (it maps it back)."""
    if not pa._MAP: return list(v)
    m = pa._MAP[side]; return pa.add(m['sh_m'], pa.mul(pa.sub(v, m['sh']), 1 / pa.ARM_K))
def unmap_foot(side, v):
    if not pa._MAP: return list(v)
    m = pa._MAP[side]; return pa.add(m['hip_m'], pa.mul(pa.sub(v, m['hip']), 1 / pa.LEG_K))

def _toe_yaw(side):
    toe = pa.sub(pa.IDLE_CS['ball_' + side][1], pa.IDLE_CS['foot_' + side][1])
    return math.degrees(math.atan2(-toe[0], toe[1]))
TOES_AHEAD = (-_toe_yaw('l'), -_toe_yaw('r'))

BASE = dict(square=[1], pelvis_lie=[0], balance=[0], weight_l=[0.5], pivot_l=[0], pivot_r=[0], reach_l=[0.99], reach_r=[0.99],
            fist_roll_l=[0], fist_roll_r=[0], guard_follow_l=[0], guard_follow_r=[0], wrist_out_l=[0], wrist_out_r=[0],
            clav_l=[0, 0, 0], clav_r=[0, 0, 0], foot_turn_l=[0], foot_turn_r=[0],
            hand_flat_l=[0], hand_flat_r=[0], hand_dir_l=[0, 1, 0], hand_dir_r=[0, 1, 0], hand_open_l=[1], hand_open_r=[1])

def pose(pelvis, rot, spine_lo, spine_hi, head, hands, feet, elbows, knees, foot_rot=([0, 0, 0], [0, 0, 0]),
         flat=(0, 0), dirs=([0, 1, 0], [0, 1, 0]), open_=(1, 1), turn=None, lie=0):
    """One key. pelvis: where the pelvis bone is (UEFN cs); rot: its (yaw, pitch, roll) on top of the idle's; hands /
    feet: (left, right) wrist / ankle targets in UEFN cs; elbows / knees: (left, right) pole directions; turn: (left,
    right) foot turn about the vertical, by default toes straight ahead (the idle's feet are turned out); lie: rolled
    this far onto the left side (- = right) about the long axis."""
    turn = turn or TOES_AHEAD
    return with_(BASE, pelvis_off=pa.sub(pelvis, IDLE_PELVIS), pelvis_rot=rot, pelvis_lie=[lie], spine_lo=spine_lo,
                 spine_hi=spine_hi, head_rot=head,
                 hand_l=unmap_hand('l', hands[0]), hand_r=unmap_hand('r', hands[1]),
                 elbow_pole_l=elbows[0], elbow_pole_r=elbows[1],
                 foot_l=unmap_foot('l', feet[0]), foot_r=unmap_foot('r', feet[1]),
                 knee_pole_l=knees[0], knee_pole_r=knees[1], foot_rot_l=foot_rot[0], foot_rot_r=foot_rot[1],
                 hand_flat_l=[flat[0]], hand_flat_r=[flat[1]], hand_dir_l=dirs[0], hand_dir_r=dirs[1],
                 hand_open_l=[open_[0]], hand_open_r=[open_[1]], foot_turn_l=[turn[0]], foot_turn_r=[turn[1]])

# The standing end: the idle's hands, feet and hips (blended to the idle's exact bones over END_BLEND).
NEUTRAL = pose(IDLE_PELVIS, [0, 0, 0], [0, 0, 0], [0, 0, 0], [0, 0, 0],
               (pa.IDLE_CS['hand_l'][1], pa.IDLE_CS['hand_r'][1]), (pa.IDLE_CS['foot_l'][1], pa.IDLE_CS['foot_r'][1]),
               ([0.4, -0.3, -1], [-0.4, -0.3, -1]), ([0.1, 1, 0], [-0.1, 1, 0]), turn=(0, 0))
NEUTRAL['square'] = [0]
END_BLEND = 0.35

# Toes tucked under (the balls of the feet on the ground behind the ankle): what a body lying face down pushes from.
TUCK = [0, -84, 0]
FLAT_HANDS = dict(flat=(1, 1), dirs=([-0.25, 1, 0], [0.25, 1, 0]), open_=(1.6, 1.6))

# ---- Face down: prone -> push-up -> hands and knees -> kneel up -> left foot forward -> rise -> stand ----
FD_HANDS = ([26, 34, 4], [-24, 34, 4])
FD_PRONE = pose([2, -16, 13], [0, -86, 0], [0, -2, 0], [0, -2, 0], [0, -70, 0], FD_HANDS,
                ([11, -100, 17], [-9, -100, 17]), ([0.5, -0.7, 0.6], [-0.5, -0.7, 0.6]), ([0, 0, -1], [0, 0, -1]),
                foot_rot=(TUCK, TUCK), **FLAT_HANDS)
FD_PUSH = pose([2, -14, 19], [0, -86, 0], [0, 12, 0], [0, 14, 0], [0, -54, 0], FD_HANDS,
               ([11, -98, 17], [-9, -98, 17]), ([0.5, -0.8, 0.2], [-0.5, -0.8, 0.2]), ([0, 0, -1], [0, 0, -1]),
               foot_rot=(TUCK, TUCK), **FLAT_HANDS)
# the hips go up and back over the knees as the legs draw in (hips first, so the knees slide rather than dig in)
FD_FOURS = pose([2, -12, 46], [0, -75, 0], [0, -16, 0], [0, -6, 0], [0, -48, 0], FD_HANDS,
                ([11, -52, 18], [-9, -52, 18]), ([0.3, -1, 0], [-0.3, -1, 0]), ([0, 1, -0.3], [0, 1, -0.3]),
                foot_rot=(TUCK, TUCK), **FLAT_HANDS)
FD_FOURS['knee_pole_l'] = FD_FOURS['knee_pole_r'] = [0, 1, -0.1]
# up onto both knees, torso upright, hands off the floor: the thigh can only come forward under an upright body (from
# hands and knees it would drive into the chest, far past the hip's range)
FD_KNEEL_UP = pose([1, -11, 50], [0, -19, 0], [0, -4, 0], [0, -4, 0], [0, -18, 0], ([22, 7, 62], [-20, 6, 63]),
                   ([11, -52, 18], [-9, -52, 18]), ([0.6, -0.6, -0.3], [-0.6, -0.6, -0.3]), ([0, 1, -0.2], [0, 1, -0.2]),
                   foot_rot=(TUCK, TUCK), open_=(1.2, 1.2))
# weight onto the right knee, the left foot swings forward under the hip, off the floor
FD_STEP_LIFT = {'foot_l': unmap_foot('l', [14, -10, 24]), 'foot_rot_l': [0, -45, 0],
                'pelvis_off': pa.sub([-2, -10.5, 51], IDLE_PELVIS)}
FD_KNEEL = pose([-3, -7, 52], [0, -22, 0], [0, -8, 0], [0, -8, 0], [0, -20, 0], ([18, 10, 69], [-24, 6, 65]),
                ([15, 8, 9.4], [-10, -52, 18]), ([1, -0.6, 0.3], [-0.6, -0.5, -0.5]), ([0.15, 1, 0.2], [0, 1, -0.3]),
                foot_rot=([0, 0, 0], TUCK), flat=(0.6, 0), dirs=([-0.3, 1, 0], [0, 1, 0]), open_=(1.4, 1.1))
FD_RISE = pose([1, 2, 77], [0, -28, 0], [0, -6, 0], [0, -6, 0], [0, -14, 0], ([19, 16, 79], [-24, 4, 78]),
               ([17, 13, 9.4], [-13, -34, 16.5]), ([0.8, -0.5, -0.4], [-0.6, -0.4, -0.7]), ([0.1, 1, 0], [-0.1, 1, 0.2]),
               foot_rot=([0, 0, 0], [0, -30, 0]), open_=(1.2, 1.1))
FD_SETTLE_STEP = {'foot_r': unmap_foot('r', [-14, -27, 16])}   # the rear foot's small step in under the hips

# From the hands-and-knees on, every get-up finishes the same way (seconds after the hands-and-knees key).
def from_fours(t0):
    return [(t0 + 0.45, FD_KNEEL_UP, 'flow', {'hips': -0.04, 'arm_l': 0.04, 'arm_r': 0.04, 'head': 0.04}),
            (t0 + 0.72, FD_STEP_LIFT, 'flow', {}),
            (t0 + 0.95, FD_KNEEL, 'flow', {'arm_l': 0.08, 'head': 0.04}),
            (t0 + 1.45, FD_RISE, 'flow', {'leg_r': 0.06, 'head': 0.04}),
            (t0 + 1.65, FD_SETTLE_STEP, 'flow', {}),
            (t0 + 1.85, NEUTRAL, 'smooth', {'arm_l': 0.04, 'arm_r': 0.04, 'head': 0.04})]
FACE_DOWN = [(0.00, FD_PRONE, 'smooth', {}),
             (0.55, FD_PUSH, 'flow', {'torso': 0.03, 'head': 0.05}),
             (1.10, FD_FOURS, 'flow', {'hips': -0.09, 'torso': -0.04, 'head': 0.03, 'leg_l': 0.08, 'leg_r': 0.10})
             ] + from_fours(1.10)

# ---- On a side (s = 1: lying on the left side, -1: the right): the lower arm stretched out ahead along the floor, the
# upper hand in front of the chest -> prop on the lower forearm and push with the upper hand -> roll the hips over onto
# hands and knees -> as face down. Lying on the left side the front faces -X. ----
def side_keys(s, t0=0.0):
    X = lambda v: [v[0] * s, v[1], v[2]]
    by = lambda lower, upper: (X(lower), X(upper)) if s > 0 else (X(upper), X(lower))   # (left, right)
    low = 'l' if s > 0 else 'r'
    lying = pose([2, -16, 17], [0, -86, 0], [0, 0, 0], [0, 0, 0], [0, -40, 5 * s], by([-16, 58, 5], [-26, 18, 4]),
                 by([2, -98, 17], [-12, -84, 18]), by([0, 0, 1], [0.3, -0.6, 1]), by([-1, 0, 0], [-1, 0.3, 0]),
                 foot_rot=([0, -120, 0], [0, -120, 0]), flat=(1, 1), dirs=by([0, 1, 0], [-0.3, 1, 0]),
                 open_=(1.5, 1.5), lie=82 * s)
    prop = pose([2, -15, 20], [0, -80, 0], [0, 6, 0], [0, 8, 0], [0, -35, 5 * s], by([-12, 52, 4], [-24, 20, 4]),
                by([2, -94, 17], [-12, -78, 18]), by([-0.2, -0.3, 1], [0.3, -0.8, 0.6]), by([-1, 0, -0.3], [-1, 0.3, -0.3]),
                foot_rot=([0, -105, 0], [0, -105, 0]), flat=(1, 1), dirs=by([0, 1, 0], [-0.3, 1, 0]),
                open_=(1.5, 1.5), lie=50 * s)
    # the lower hand comes back off the floor to its push spot
    lift = {'hand_' + low: unmap_hand(low, X([20, 54, 8])), 'hand_flat_' + low: [0.3],
            'elbow_pole_' + low: X([0.4, -0.8, 0.6])}
    up = 'r' if s > 0 else 'l'   # the upper foot rides a little clear of the lower leg as the hips roll over
    clear = {'foot_' + up: unmap_foot(up, X([-16, -64, 26]))}
    return [(t0, lying, 'smooth' if t0 == 0 else 'flow', {}),
            (t0 + 0.55, prop, 'flow', {'torso': 0.03, 'head': 0.05, 'hips': 0.04}),
            (t0 + 0.80, clear, 'flow', {}),
            (t0 + 0.85, lift, 'flow', {}),
            (t0 + 1.10, FD_FOURS, 'flow', {'hips': -0.02, 'head': 0.03, 'leg_l': 0.08, 'leg_r': 0.10, 'arm_l': 0.03, 'arm_r': 0.03})
            ] + from_fours(t0 + 1.10)
SIDE_L, SIDE_R = side_keys(1), side_keys(-1)

# ---- On its back: roll over onto the left side about the long axis (the left arm goes out ahead along the floor, the
# right arm and leg come over the top), then as from the left side. Lying face up is face down rolled half over:
# body-left is -X, the front faces up. ----
BK_SUPINE = pose([2, -16, 12], [0, -86, 0], [0, 0, 0], [0, 0, 0], [0, -80, 0], ([-26, -12, 4], [26, -12, 4]),
                 ([-11, -100, 10], [9, -100, 10]), ([-1, 0, 0.3], [1, 0, 0.3]), ([0, 0, 1], [0, 0, 1]),
                 foot_rot=([0, -120, 0], [0, -120, 0]), flat=(1, 1), dirs=([0, -1, 0], [0, -1, 0]), open_=(1.4, 1.4),
                 lie=180)
# half way over: the left arm sweeping out ahead along the floor, the right hand and the right foot over the top
BK_OVER = {'pelvis_lie': [130], 'hand_r': unmap_hand('r', [4, 12, 46]), 'hand_flat_r': [0.2], 'hand_flat_l': [0.5], 'hand_dir_l': [-1, 0, 0],
           'hand_l': unmap_hand('l', [-34, 22, 6]), 'elbow_pole_l': [-0.7, 0, 0.7], 'foot_r': unmap_foot('r', [-4, -90, 32]),
           'knee_pole_r': [-0.5, 0, 1], 'head_rot': [0, -60, 0]}
FACE_UP = [(0.00, BK_SUPINE, 'smooth', {}),
           (0.30, BK_OVER, 'flow', {'arm_r': 0.04, 'arm_l': 0.08, 'leg_r': 0.04}),
           ] + side_keys(1, 0.60)

# ---- Start variants: the same get-ups from other ways a body lands (arms by the sides, stretched overhead, flung out,
# a knee drawn up, curled). The stand-up matches the ragdoll against the first 0.6 s of every clip, so a closer start
# means less of a blend from the ragdoll (and no arm whipping round to a far-off pose), and the get-ups vary. Each is
# its own first pose, a short gathering move (partial keys), then the base clip from its second key, later by `lead`.
def variant(keys, start, gather=(), lead=0.35):
    return ([(0.0, start, 'smooth', {})] + list(gather)
            + [(k[0] + lead,) + tuple(k[1:]) for k in keys[1:]])
def hands(l, r, **extra):
    return dict({'hand_l': unmap_hand('l', l), 'hand_r': unmap_hand('r', r)}, **extra)

# face down, arms along the sides: the hands come up beside the chest, off the floor, then plant to push
FD_ARMS_SIDE = with_(FD_PRONE, **hands([24, -20, 5], [-22, -20, 5]), elbow_pole_l=[0.3, 0, 1], elbow_pole_r=[-0.3, 0, 1],
                     hand_flat_l=[0.4], hand_flat_r=[0.4], hand_dir_l=[1, -0.3, 0], hand_dir_r=[-1, -0.3, 0])
FD_ARMS_SIDE_GATHER = hands([34, 36, 9], [-32, 36, 9], elbow_pole_l=[0.5, -0.7, 0.6], elbow_pole_r=[-0.5, -0.7, 0.6],
                            hand_flat_l=[0.7], hand_flat_r=[0.7], hand_dir_l=[-0.25, 1, 0], hand_dir_r=[0.25, 1, 0])
# face down, arms stretched out ahead: drawn back in under the shoulders
FD_ARMS_UP = with_(FD_PRONE, **hands([30, 64, 4], [-28, 64, 4]), elbow_pole_l=[1, 0, -0.2], elbow_pole_r=[-1, 0, -0.2])
FD_ARMS_UP_GATHER = hands([30, 46, 8], [-28, 46, 8], elbow_pole_l=[1, -0.3, 0], elbow_pole_r=[-1, -0.3, 0],
                          hand_flat_l=[0.6], hand_flat_r=[0.6])
# face down, the left knee drawn up out to the side (a common way a body lands): it slides back straight first
FD_FROG = with_(FD_PRONE, foot_l=unmap_foot('l', [20, -78, 14]), knee_pole_l=[1, 0.3, 0.2], foot_rot_l=[0, -100, 0])
FD_FROG_GATHER = {'foot_l': unmap_foot('l', [16, -90, 17]), 'knee_pole_l': [0.4, 0, 0.2]}
# face up, arms flung out wide
FU_ARMS_OUT = with_(BK_SUPINE, **hands([-62, 6, 4], [60, 6, 4]), hand_dir_l=[-1, 0, 0], hand_dir_r=[1, 0, 0],
                    elbow_pole_l=[0, -0.3, 1], elbow_pole_r=[0, -0.3, 1])
# face up, arms up past the head
FU_ARMS_UP = with_(BK_SUPINE, **hands([-24, 62, 4], [22, 62, 4]), hand_dir_l=[0, 1, 0], hand_dir_r=[0, 1, 0],
                   elbow_pole_l=[-0.6, 0, 1], elbow_pole_r=[0.6, 0, 1])
# on a side, curled up (knees drawn up in front)
def side_curled(s):
    X = lambda v: [v[0] * s, v[1], v[2]]
    lower, upper = ('l', 'r') if s > 0 else ('r', 'l')
    base = (SIDE_L if s > 0 else SIDE_R)[0][1]
    return with_(base, **{'foot_' + lower: unmap_foot(lower, X([-6, -70, 17])), 'foot_' + upper: unmap_foot(upper, X([-22, -62, 31])),
                          'knee_pole_' + lower: X([-1, 0.4, 0]), 'knee_pole_' + upper: X([-1, 0.5, 0])})

# From recorded landings no start was close to (Saved/GetUpMisses.csv, read with getupmisses.py):
# face down sprawled: one arm flung out ahead to the left, the other out to the right at the shoulder, both knees
# fallen out to the sides
FD_SPRAWL = with_(FD_PRONE, **hands([44, 60, 4], [-56, 6, 4]), elbow_pole_l=[1, -0.2, -0.2], elbow_pole_r=[-0.3, -1, 0.5],
                  hand_dir_l=[0.4, 1, 0], hand_dir_r=[-1, 0.2, 0],
                  foot_l=unmap_foot('l', [20, -78, 14]), knee_pole_l=[1, 0.3, 0.2], foot_rot_l=[0, -100, 0],
                  foot_r=unmap_foot('r', [-18, -78, 14]), knee_pole_r=[-1, 0.3, 0.2], foot_rot_r=[0, -100, 0])
FD_SPRAWL_GATHER = hands([34, 44, 8], [-34, 26, 10], elbow_pole_l=[1, -0.3, 0], elbow_pole_r=[-0.5, -0.7, 0.6],
                         hand_flat_l=[0.6], hand_flat_r=[0.6], hand_dir_l=[-0.25, 1, 0], hand_dir_r=[0.25, 1, 0],
                         foot_l=unmap_foot('l', [14, -90, 17]), foot_r=unmap_foot('r', [-12, -90, 17]),
                         knee_pole_l=[0.4, 0, 0.2], knee_pole_r=[-0.4, 0, 0.2])
# face up, arms thrown up and out in a V past the head
FU_ARMS_V = with_(BK_SUPINE, **hands([-52, 50, 4], [50, 52, 4]), hand_dir_l=[-0.6, 1, 0], hand_dir_r=[0.6, 1, 0],
                  elbow_pole_l=[-0.4, -0.3, 1], elbow_pole_r=[0.4, -0.3, 1])
# face up, the right knee fallen out to the side (the sole by the left knee)
FU_KNEE_OUT = with_(BK_SUPINE, foot_r=unmap_foot('r', [4, -62, 10]), knee_pole_r=[1, 0.2, 0.3])

FACE_DOWN_ARMS_SIDE = variant(FACE_DOWN, FD_ARMS_SIDE, [(0.35, FD_ARMS_SIDE_GATHER, 'flow', {})])
FACE_DOWN_ARMS_UP = variant(FACE_DOWN, FD_ARMS_UP, [(0.35, FD_ARMS_UP_GATHER, 'flow', {})])
FACE_DOWN_FROG = variant(FACE_DOWN, FD_FROG, [(0.30, FD_FROG_GATHER, 'flow', {})], lead=0.25)
FACE_UP_ARMS_OUT = variant(FACE_UP, FU_ARMS_OUT, [(0.15, {'hand_r': unmap_hand('r', [46, 10, 20]), 'hand_flat_r': [0.0],
                                                           'hand_dir_r': [0.7, 0.7, 0]}, 'flow', {})], lead=0.15)
FACE_UP_ARMS_UP = variant(FACE_UP, FU_ARMS_UP, lead=0.15)
# (from overhead the left arm only has to come round a little: it stays out ahead instead of first sweeping down)
FACE_UP_ARMS_UP[1] = (FACE_UP_ARMS_UP[1][0], dict(FACE_UP_ARMS_UP[1][1], hand_l=unmap_hand('l', [-30, 50, 6])),) + tuple(FACE_UP_ARMS_UP[1][2:])
FACE_DOWN_SPRAWL = variant(FACE_DOWN, FD_SPRAWL, [(0.40, FD_SPRAWL_GATHER, 'flow', {})], lead=0.4)
FACE_UP_ARMS_V = variant(FACE_UP, FU_ARMS_V, lead=0.15)
FACE_UP_ARMS_V[1] = (FACE_UP_ARMS_V[1][0], dict(FACE_UP_ARMS_V[1][1], hand_l=unmap_hand('l', [-36, 46, 6])),) + tuple(FACE_UP_ARMS_V[1][2:])
FACE_UP_KNEE_OUT = variant(FACE_UP, FU_KNEE_OUT, lead=0.15)
SIDE_L_CURLED = variant(SIDE_L, side_curled(1), lead=0.1)
SIDE_R_CURLED = variant(SIDE_R, side_curled(-1), lead=0.1)

SPRINGS = {'head': (4.0, 0.8), 'torso': (7.0, 0.8)}   # the head and chest trail the hips a little and settle
SPECS = {
    'RPG_GetUp_FaceDown': FACE_DOWN,
    'RPG_GetUp_FaceUp': FACE_UP,
    'RPG_GetUp_SideL': SIDE_L,
    'RPG_GetUp_SideR': SIDE_R,
    'RPG_GetUp_FaceDownArmsSide': FACE_DOWN_ARMS_SIDE,
    'RPG_GetUp_FaceDownArmsUp': FACE_DOWN_ARMS_UP,
    'RPG_GetUp_FaceDownFrog': FACE_DOWN_FROG,
    'RPG_GetUp_FaceUpArmsOut': FACE_UP_ARMS_OUT,
    'RPG_GetUp_FaceUpArmsUp': FACE_UP_ARMS_UP,
    'RPG_GetUp_SideLCurled': SIDE_L_CURLED,
    'RPG_GetUp_SideRCurled': SIDE_R_CURLED,
    'RPG_GetUp_FaceDownSprawl': FACE_DOWN_SPRAWL,
    'RPG_GetUp_FaceUpArmsV': FACE_UP_ARMS_V,
    'RPG_GetUp_FaceUpKneeOut': FACE_UP_KNEE_OUT,
}

def bake_getup(name, keys):
    T = keys[-1][0]; end = T + END_BLEND
    n = int(round(end * FPS)) + 1; n += (-(n - 1)) % (FPS // 30)
    times = [i / FPS for i in range(n)]
    track = sample_track(keys, times, GROUPS, SPRINGS)
    frames = []
    for t, p in zip(times, track):
        local = build_pose(p)
        k = min(max((t - T) / END_BLEND, 0.0), 1.0); k = k * k * (3 - 2 * k)
        if k > 0:
            local = {b: (pa._nlerp(q, pa.IDLE[b][0], k), pa.lerp(tr, pa.IDLE[b][1], k)) for b, (q, tr) in local.items()}
            local['root'] = ([0.0, 0.0, 0.0, 1.0], [0.0, 0.0, 0.0])
        frames.append(local)
    return {'name': name, 'fps': FPS, 'frames': n, 'impact': None, 'bones': pa.BONES, 'poses': frames,
            'keys': [k[0] for k in keys] + [end]}

# ---- checks ----
# how far above the floor each bone sits when that part rests on the ground (bone to skin), and what to report
CONTACT = [('pelvis', 10), ('spine_05', 11), ('head', 9), ('hand_l', 2.5), ('hand_r', 2.5), ('lowerarm_l', 3),
           ('lowerarm_r', 3), ('calf_l', 5.5), ('calf_r', 5.5), ('foot_l', 4.5), ('foot_r', 4.5), ('ball_l', 2.5),
           ('ball_r', 2.5)]

def report(name, clip):
    print(name, clip['frames'], 'frames', round((clip['frames'] - 1) / FPS, 2), 's')
    print('   t   ' + ' '.join(f'{b[:9]:>9}' for b, _ in CONTACT))
    worst = {b: (1e9, 0) for b, _ in CONTACT}
    keyset = {round(t * FPS) for t in clip['keys']}
    for i, local in enumerate(clip['poses']):
        cs = pa.to_cs(local)
        for b, skin in CONTACT:
            z = cs[b][1][2] - skin
            if z < worst[b][0]: worst[b] = (z, i / FPS)
        if i in keyset or i == clip['frames'] - 1:
            print(f'{i / FPS:5.2f} ' + ' '.join(f'{cs[b][1][2]:9.1f}' for b, _ in CONTACT))
    under = [f'{b} {z:.1f} cm at {t:.2f}s' for b, (z, t) in worst.items() if z < -0.5]
    print('   into the floor:', ', '.join(under) if under else 'nothing')

if __name__ == '__main__':
    raise SystemExit('Procedural get-ups are retired. Use capture_getups.py or capture8611_getups.py for the two mocap recoveries.')
    from preview import render
    want = sys.argv[1:]
    path = pa.SP.path('clips', '.json')
    out = json.load(open(path)) if os.path.exists(path) else {}
    for name, keys in SPECS.items():
        if want and not any(w in name for w in want): continue
        clip = bake_getup(name, keys); out[name] = clip
        report(name, clip)
        render(clip, os.path.join(pa.HERE, 'previews', name + '.png'), clip['keys'])
    json.dump(out, open(path, 'w'))
