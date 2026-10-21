"""Key poses for the first prototype set. Run: python poses.py  ->  clips.json

Axes (component space): +Y forward, +X character-left, +Z up.
Rotations are (yaw, pitch, roll) degrees:
  yaw   + turns the chest to the right (left shoulder forward), - drives the right shoulder forward
  pitch + leans back, - leans forward;  for feet, - lifts the heel / + lifts the toes
  roll  + tilts toward character-left;  on a clavicle, + raises the right shoulder / - raises the left
Scalar channels are 1-element lists: reach_* (max arm extension, fraction of arm length),
fist_roll_* (degrees about the forearm, + pronates the right fist), pivot_* (1 = planted foot turns on its ball), plant_* (offset of a planted foot's ball: a step),
guard_follow_* (1 = that hand's target is authored in stance space and carried by the head, so the guard
stays on the face while the torso turns; 0 = plain world target, for strikes and counterbalance swings).
elbow_pole_* is always in stance-chest space (it turns with the chest).
hand_rel_* blends the hand target toward hand_off_* = an offset (world axes) from the shoulder's current
position, so a wind-up path can't run into the shoulder while the torso drives it forward.
loop_* is a world offset added to the hand target: animate it 0 -> peak -> 0 to arc a strike
over the top without putting a stop-and-go key in the forward path.

Each key: (time_s, params, easing into this key, lags[, per-group easing overrides]).
A key may hold only some channels (see only()); other channels flow straight past it.
Lags shift when a channel group reaches that key, so body parts arrive in sequence instead of
all on the same frame. Punch chain: legs/hips -> lower spine -> upper spine -> shoulder -> fist.
Easing: 'smooth' (gentle), 'strike'/'in' (accelerating into a strike; 'in' is sharper),
'whip' (carries the incoming speed and keeps accelerating, for strikes that follow a wind-up arc), 'out' (decelerating),
'flow' (monotone cubic through neighbouring keys, no stop at each key).
"""
import json, os, math
from protoanim import bake, with_, only, set_reference, sample, FPS, HERE

L_FOOT = [20.6, 11.0, 8.5]     # lead foot (from MM_Idle)
R_FOOT = [-10.4, -19.9, 8.1]   # rear foot

GROUPS = {
    'hips': ('pelvis_off', 'pelvis_rot'),
    'torso_lo': ('spine_lo',),
    'torso_hi': ('spine_hi',),
    'shoulder_r': ('clav_r',),
    'arm_r': ('hand_r', 'elbow_pole_r', 'reach_r', 'fist_roll_r', 'guard_follow_r', 'hand_rel_r', 'hand_off_r', 'loop_r'),
    'arm_l': ('hand_l', 'elbow_pole_l', 'reach_l', 'fist_roll_l', 'clav_l', 'guard_follow_l', 'hand_rel_l', 'hand_off_l', 'loop_l'),
    'leg_r': ('foot_r', 'foot_rot_r', 'foot_turn_r', 'knee_pole_r', 'pivot_r', 'plant_r'),
    'leg_l': ('foot_l', 'foot_rot_l', 'foot_turn_l', 'knee_pole_l', 'pivot_l', 'plant_l'),
    'head': ('head_rot',),
}

# Orthodox fighting stance: feet bladed, upper body fairly square, soft knees, chin slightly down. Guard: rear fist
# in front of the cheek (not on the jaw), lead fist held in close enough that the left punch has room to travel.
# The lead foot is turned out ~18 deg (foot_rot_l yaw -24; every foot_rot_l below includes that same -24).
STANCE = {
    'pelvis_off': [-1.5, -2.5, -2.5], 'pelvis_rot': [11, 0, 0],
    'spine_lo': [1, -2, 0], 'spine_hi': [2, -3, 0],
    'clav_l': [0, 0, -3], 'clav_r': [0, 0, 2],
    'head_rot': [-4, -5, 0],
    'hand_r': [-17, 20, 140], 'elbow_pole_r': [-0.4, -0.3, -1], 'reach_r': [0.97], 'fist_roll_r': [0], 'guard_follow_r': [1], 'hand_rel_r': [0], 'hand_off_r': [3, 22, -4], 'loop_r': [0, 0, 0],
    'hand_l': [12, 30, 142], 'elbow_pole_l': [0.5, 0, -1], 'reach_l': [0.97], 'fist_roll_l': [0], 'guard_follow_l': [1], 'hand_rel_l': [0], 'hand_off_l': [-2, 22, -4], 'loop_l': [0, 0, 0],
    'foot_l': L_FOOT, 'foot_rot_l': [-24, 0, 0], 'foot_turn_l': [0], 'knee_pole_l': [0.22, 1, 0], 'pivot_l': [1], 'plant_l': [1, 4, 0],
    'foot_r': R_FOOT, 'foot_rot_r': [0, 0, 0], 'foot_turn_r': [0], 'knee_pole_r': [-0.65, 1, 0], 'pivot_r': [1], 'plant_r': [-4, -2, 0],
}
S = lambda **kw: with_(STANCE, **kw)
set_reference(STANCE)

# ---- 1. Right overhand cross, 0.68 s ----
# A cross thrown over the top. Every body part follows one smooth rise-and-fall of speed, in order:
# rear foot -> hips -> lower spine -> upper spine/shoulder -> arm. The hips peak early and are already
# slowing as the arm whips through, the way energy hands off up the chain.
# The fist's loop is an arm-only via point, so it never makes the body hitch.
BODY = ('leg_r', 'leg_l', 'hips', 'torso_lo', 'torso_hi', 'shoulder_r', 'head')
# Load: pressure into the rear foot and a small counter-coil. The arm stays quiet.
P_LOAD = S(pelvis_off=[-1.5, -3, -5.5], pelvis_rot=[15, 0, 0], spine_lo=[2, -2, 0], spine_hi=[4, -3, -1],
           hand_r=[-13.5, 10, 139.5], elbow_pole_r=[-0.6, -0.3, -1], guard_follow_r=[0], hand_rel_r=[1], hand_off_r=[-2, 26, -7],
           foot_rot_r=[-5, -6, 0])
# Overhand loop (arm only): an up-and-out bump that peaks mid-strike and is falling as the fist lands,
# while the elbow lifts on its way over.
P_LOOP = only(S(loop_r=[-7, 0, 16], elbow_pole_r=[-0.55, -0.95, -0.4], fist_roll_r=[8]), GROUPS, 'arm_r')
P_LOOP = {k: P_LOOP[k] for k in ('loop_r', 'elbow_pole_r')}
# Impact: level change, hips and chest turned through, fist arriving downward with the elbow high,
# right shoulder covering the chin, left hand on the face.
P_IMPACT = S(pelvis_off=[5, 16, -6.5], pelvis_rot=[-28, -5, -3],
             spine_lo=[-14, -8, 6], spine_hi=[-20, -10, 11],
             clav_r=[-28, 0, 13], clav_l=[8, 0, -2],
             hand_r=[-2, 100, 140], elbow_pole_r=[0.15, -1, 0.15], reach_r=[0.975], fist_roll_r=[-20], guard_follow_r=[0],
             hand_l=[8, 27, 147], elbow_pole_l=[0.6, 0.2, -1],
             foot_rot_r=[-55, -32, 0], knee_pole_r=[0.05, 1, -0.15],
             head_rot=[-10, -12, 5])
# Follow-through: the body finishes its turn while the fist carries on down and across.
P_FOLLOW = with_(P_IMPACT, pelvis_off=[5.6, 17, -6.8], pelvis_rot=[-31, -5, -3],
                 spine_lo=[-15, -9, 6], spine_hi=[-23, -11, 12],
                 hand_r=[1, 104, 132], reach_r=[0.985])
# Retract: the hand comes straight back to the face while hips and torso unwind behind it.
P_RETRACT = S(pelvis_off=[1.5, 5, -6], pelvis_rot=[-7, -1, 0],
              spine_lo=[-6, -3, 1], spine_hi=[-6, -4, 2],
              clav_r=[-6, 0, 5],
              hand_r=[-11, 38, 140], elbow_pole_r=[-0.18, -0.41, -1], fist_roll_r=[5], guard_follow_r=[0.6],
              hand_l=[10, 28, 143],
              foot_rot_r=[-18, -10, 0], knee_pole_r=[-0.25, 1, 0])
# Settle (body only): the weight rocks a touch past stance and comes to rest. Arms go straight home.
P_SETTLE = only(S(pelvis_off=[0.3, -1.5, -4.8], pelvis_rot=[12.5, 0, 0], spine_lo=[1.5, -2, 0], spine_hi=[2.5, -3, 0]),
                GROUPS, *BODY)
B = lambda ease: {g: ease for g in BODY}
SMOOTH_ARM = {'elbow_pole_r': 'flow', 'fist_roll_r': 'flow', 'guard_follow_r': 'flow', 'hand_rel_r': 'linear',
              'hand_off_r': 'flow', 'loop_r': 'flow'}   # only the fist path whips
PUNCH = [(0.00, STANCE, 'smooth', {}),
         (0.10, P_LOAD, 'smooth', {}),
         (0.19, P_LOOP, 'flow', {}),
         (0.26, P_IMPACT, 'whip', {'leg_r': -0.05, 'hips': -0.035, 'torso_lo': -0.02, 'torso_hi': 0.025,
                                   'shoulder_r': 0.035, 'head': 0.0, 'arm_l': -0.01, 'arm_r': 0.015}, {**B('flow'), **SMOOTH_ARM}),
         (0.32, P_FOLLOW, 'out', {'arm_r': -0.015, 'torso_hi': 0.01}, B('flow')),
         (0.44, P_RETRACT, 'flow', {'arm_r': 0.0, 'shoulder_r': -0.02, 'torso_hi': -0.01, 'leg_r': 0.03}),
         (0.56, P_SETTLE, 'flow', {}),
         (0.68, STANCE, 'flow', {'arm_r': -0.03, 'arm_l': -0.03})]

# ---- 2. Right teep (rear-leg push kick), 0.77 s ----
# Only the kicking leg and the hips snap into impact; arms, torso, head and support leg flow.
# Weight settles onto the lead leg, knee comes up toward the chest, then the hips thrust
# forward and the leg pushes out through the ball of the foot while the torso leans back.
# Combat kick: both hands stay in guard, carried with the head (guard_follow, the default) so they stay on the
# face while the torso leans back. The rear hand sways a little out and down with the kick, never behind the body.
RK_LOAD = S(pelvis_off=[6, 4, -4.5], pelvis_rot=[7, 0, -2], spine_lo=[0, -1, -1], spine_hi=[1, -2, -1],
            foot_rot_r=[0, -20, 0], hand_r=[-16, 12, 136])
RK_CHAMBER = S(pelvis_off=[5, 1, -2.5], pelvis_rot=[1, 2, -3], spine_lo=[-2, 3, -2], spine_hi=[-1, 2, -2],
               foot_r=[-6, 22, 72], foot_rot_r=[-14.5, -12, -6], foot_turn_r=[-26.5], knee_pole_r=[-0.1, 1, 1.5], pivot_r=[0],
               hand_r=[-16, 22, 136], elbow_pole_r=[-0.4, -0.2, -1], hand_l=[11, 36, 142])
# The rear foot stands turned out ~47 deg; it straightens toward the target as the knee lifts (foot_turn_r) and then
# tips up, so the sole faces the target upright instead of rolling onto its side.
RK_IMPACT = S(pelvis_off=[3, 25, 5], pelvis_rot=[-12, 28, -8], spine_lo=[0, 8, 0], spine_hi=[0, 5, 1],
              foot_r=[-4, 106, 120], foot_rot_r=[0, 92, 0], foot_turn_r=[-45], knee_pole_r=[-0.1, 1, 1.5], pivot_r=[0],
              foot_rot_l=[-28, -20, 0],
              hand_r=[-20, 34, 132], elbow_pole_r=[-0.45, -0.2, -1],
              hand_l=[12, 36, 144], head_rot=[-4, -12, 0])
RK_FOLLOW = with_(RK_IMPACT, foot_r=[-4, 110, 122], pelvis_off=[3.5, 27, 5.5], pelvis_rot=[-13, 30, -8])
RK_RETRACT = with_(RK_CHAMBER, foot_r=[-7, 18, 62], foot_rot_l=[-26, -3, 0])
RIGHT_KICK = [(0.00, STANCE, 'smooth', {}),
              (0.12, RK_LOAD, 'smooth', {'leg_l': -0.02, 'arm_r': 0.02}),
              (0.28, RK_CHAMBER, 'flow', {'hips': -0.02, 'leg_r': 0.0, 'torso_hi': 0.02, 'arm_r': 0.02}),
              (0.40, RK_IMPACT, 'in', {'hips': -0.04, 'leg_l': -0.03, 'torso_lo': -0.01, 'arm_r': 0.02, 'arm_l': 0.02, 'head': 0.02}, {**{g: 'flow' for g in ('arm_r', 'arm_l', 'torso_lo', 'torso_hi', 'head', 'leg_l')}, 'foot_rot_r': 'in', 'foot_turn_r': 'in'}),
              (0.46, RK_FOLLOW, 'out', {}),
              (0.58, RK_RETRACT, 'flow', {'leg_r': -0.02, 'hips': 0.02, 'torso_hi': 0.03, 'arm_r': 0.03}),
              (0.78, STANCE, 'flow', {'arm_r': -0.04, 'arm_l': -0.04, 'torso_hi': -0.02, 'hips': -0.02, 'foot_rot_r': -0.10, 'foot_turn_r': -0.10})]

# ---- 3. Left teep (lead-leg push kick), 0.68 s ----
# Not a mirror: the lead leg is already forward, so weight rocks back onto the rear leg,
# the chamber is quicker, the lean-back is bigger and the hips drive less. Hands stay in guard; the lead hand
# sways a little out and down with the kick.
LK_LOAD = S(pelvis_off=[-4, -6, -4.5], pelvis_rot=[13, 3, 2], spine_lo=[0, 0, 1], spine_hi=[1, -1, 1],
            foot_rot_l=[-24, -15, 0])
LK_CHAMBER = S(pelvis_off=[-5, -8, -3], pelvis_rot=[11, 8, 4], spine_lo=[-6, 4, 2], spine_hi=[-7, 3, 2],
               foot_l=[16, 36, 72], foot_rot_l=[-28, -20, 0], knee_pole_l=[0.1, 1, 1.5], pivot_l=[0],
               hand_l=[15, 42, 138.5], elbow_pole_l=[0.8, 0, -0.7],
               hand_r=[-14, 22, 140])
LK_IMPACT = S(pelvis_off=[-1, 17, 4], pelvis_rot=[8, 29, 8], spine_lo=[0, 7, 1], spine_hi=[0, 5, 1],
              foot_l=[13, 108, 119], foot_rot_l=[-32, 92, 0], knee_pole_l=[0.1, 1, 1.5], pivot_l=[0],
              foot_rot_r=[16, -18, 0],
              hand_l=[18, 42, 135], elbow_pole_l=[0.9, 0, -0.6],
              hand_r=[-16, 36, 141], head_rot=[-4, -12, 0])
LK_FOLLOW = with_(LK_IMPACT, foot_l=[13, 112, 121], pelvis_off=[-0.5, 18.5, 4.5], pelvis_rot=[8, 31, 8])
LK_RETRACT = with_(LK_CHAMBER, foot_l=[15, 30, 62], foot_rot_r=[6, -2, 0])
LEFT_KICK = [(0.00, STANCE, 'smooth', {}),
             (0.10, LK_LOAD, 'smooth', {'leg_r': -0.02}),
             (0.22, LK_CHAMBER, 'flow', {'hips': -0.02, 'torso_hi': 0.02, 'arm_l': 0.02}),
             (0.33, LK_IMPACT, 'in', {'hips': -0.04, 'leg_r': -0.02, 'torso_lo': -0.01, 'arm_l': 0.02, 'head': 0.02}, {**{g: 'flow' for g in ('arm_r', 'arm_l', 'torso_lo', 'torso_hi', 'head', 'leg_r')}, 'foot_rot_l': 'in'}),
             (0.38, LK_FOLLOW, 'out', {}),
             (0.50, LK_RETRACT, 'flow', {'leg_l': -0.02, 'hips': 0.02, 'torso_hi': 0.03, 'arm_l': 0.03}),
             (0.68, STANCE, 'flow', {'arm_l': -0.04, 'arm_r': -0.03, 'torso_hi': -0.02, 'hips': -0.02, 'foot_rot_l': -0.08})]

# (asset name, keys, impact time of the striking limb)
# ---- 4. Left punch (lead cross, thrown like the right cross), 0.60 s ----
# Same fighter and the same arm action as the right cross, from the other side of the stance. The ELBOW
# leads: it swings out, rises above the shoulder and sweeps across the body, and the fist follows its track,
# landing a little past the centre line and carrying on across. The chest chambers open, stays fairly square
# while the elbow is out wide, then turns late (torso_hi/clav lag) to finish as the fist lands, so the
# raised elbow stays in front of the chest. The arm stays bent until the end, then fires over two frames
# and the fist lands a touch below shoulder height, coming down. The rear leg pushes long and its knee
# tracks the toes; the rear hand stays on the jaw.
BODY_L = ('leg_r', 'leg_l', 'hips', 'torso_lo', 'torso_hi', 'head')
# Load: a quick chamber, like the start of a cross - hips and chest open about 20 deg and take the fist
# back with the lead shoulder (hand_rel); the elbow starts out and up straight away, so the turn carries it.
J_LOAD = S(pelvis_off=[-0.5, -2.5, -4.5], pelvis_rot=[2, 0, 0], spine_lo=[-5, -1.5, 0], spine_hi=[-6, -2, 0],
           clav_l=[-3, 0, -2],
           hand_l=[13, 40, 141], elbow_pole_l=[0.6, -0.2, -1], guard_follow_l=[0], hand_rel_l=[1], hand_off_l=[-2, 24, 0],
           foot_rot_r=[0, -5, 0])
# Elbow arc (arm only): the elbow swings out and up above the shoulder while the arm stays bent, then
# sweeps across with the turn; the fist rides behind it.
J_LOOP = only(S(loop_l=[3, 0, 9], elbow_pole_l=[0.45, -0.35, -0.5], fist_roll_l=[-20]), GROUPS, 'arm_l')
J_LOOP = {k: J_LOOP[k] for k in ('loop_l', 'elbow_pole_l')}
# Impact: hips and chest turned through, weight over the lead leg with the rear leg pushed long (heel up,
# rear knee kept over its toes), lead shoulder forward, across and up, elbow a little high behind the fist, fist
# turned over and landing a touch down and across the centreline, rear hand tight on the jaw, head slipped right.
J_IMPACT = S(pelvis_off=[1, 12.5, -4.8], pelvis_rot=[31, -4, 1],
             spine_lo=[12, -8, -1.5], spine_hi=[15, -10.5, -5],
             clav_l=[20, 0, -10], clav_r=[-4, 0, 2],
             hand_l=[-9, 106, 131], elbow_pole_l=[0.3, -0.6, 0.1], reach_l=[0.997], fist_roll_l=[10], guard_follow_l=[0],
             hand_r=[-16, 20, 141],
             foot_rot_l=[-20, 0, 0], foot_rot_r=[0, -10, 0], knee_pole_l=[0.25, 1, 0], knee_pole_r=[-0.75, 1, 0],
             head_rot=[3, -9, -6])
# Follow-through: the body finishes its turn while the fist carries on a few cm down and across.
J_FOLLOW = with_(J_IMPACT, pelvis_off=[1, 13.5, -4.9], pelvis_rot=[33, -4, 1],
                 spine_lo=[13, -8, -1.5], spine_hi=[16.5, -11, -5],
                 hand_l=[-13, 108, 128], reach_l=[0.999])
# Retract: the fist comes back to the face on a slightly higher line than it went out; hips and chest unwind behind it.
J_RETRACT = S(pelvis_off=[1, 3, -4.3], pelvis_rot=[17, -1, 0],
              spine_lo=[3, -3, 0], spine_hi=[4, -4, -2],
              clav_l=[4, 0, -5],
              hand_l=[10, 40, 142], elbow_pole_l=[0.45, -0.3, -0.6], fist_roll_l=[-15], guard_follow_l=[0.6],
              foot_rot_r=[0, -5, 0])
# Settle (body only): the weight rocks a touch back past stance and comes to rest.
J_SETTLE = only(S(pelvis_off=[0.2, -1.2, -4.3], pelvis_rot=[9.5, 0, 0], spine_lo=[0.5, -2, 0], spine_hi=[1.5, -3, 0]),
                GROUPS, *BODY_L)
SMOOTH_ARM_L = {'elbow_pole_l': 'flow', 'fist_roll_l': 'flow', 'guard_follow_l': 'flow', 'clav_l': 'flow', 'loop_l': 'flow',
                'hand_rel_l': 'linear', 'hand_off_l': 'flow'}   # only the fist path whips, as on the cross
BL = lambda ease: {g: ease for g in BODY_L}
JAB = [(0.00, STANCE, 'smooth', {}),
       (0.06, J_LOAD, 'smooth', {'arm_l': 0.0}),
       (0.16, J_LOOP, 'flow', {}),
       (0.20, J_IMPACT, 'whip', {'leg_r': -0.06, 'hips': -0.05, 'torso_lo': 0.04, 'torso_hi': 0.08,
                                 'clav_l': 0.07, 'head': 0.0, 'arm_r': -0.01, 'arm_l': 0.035}, {**BL('flow'), **SMOOTH_ARM_L}),
       (0.26, J_FOLLOW, 'out', {'arm_l': -0.015, 'torso_hi': 0.01}, BL('flow')),
       (0.38, J_RETRACT, 'flow', {'arm_l': 0.0, 'hips': 0.02, 'torso_hi': -0.01, 'leg_r': 0.03}),
       (0.49, J_SETTLE, 'flow', {}),
       (0.60, STANCE, 'flow', {'arm_l': -0.03, 'arm_r': -0.03})]

# ---- 5. Unarmed guard (held while RT + LT), 1.0 s seamless loop ----
# Practical high guard: both fists up at the cheekbones a few cm in front of the face, elbows down
# and in front of the ribs (not flared), chin tucked, shoulders slightly raised, knees a little deeper,
# hips squared up a touch. The game blends into it from whatever is playing (montage blend-in) and
# back out on release; the clip itself is the held pose with a slight breathing settle so it isn't frozen.
# The combat stance's own guard, just tighter and higher: same legs, hips, torso, head and elbows; both fists come up
# 7 cm and close in about 1.3 cm each toward the other (the gap between them shrinks by about an inch).
GUARD_UP, GUARD_IN = 7, 1.3
_gl, _gr = STANCE['hand_l'], STANCE['hand_r']
_gd = [(_gr[i] - _gl[i]) for i in (0, 1)]; _gn = math.hypot(*_gd); _gd = [x / _gn * GUARD_IN for x in _gd]
GUARD = S(hand_l=[_gl[0] + _gd[0], _gl[1] + _gd[1], _gl[2] + GUARD_UP],
          hand_r=[_gr[0] - _gd[0], _gr[1] - _gd[1], _gr[2] + GUARD_UP])
# The same slow breath as the combat stance.
GUARD_BREATH = with_(GUARD, pelvis_off=[-1.5, -2.5, -3], spine_hi=[2, -3.8, 0],
                     hand_l=[GUARD['hand_l'][0], GUARD['hand_l'][1], GUARD['hand_l'][2] - 0.6],
                     hand_r=[GUARD['hand_r'][0], GUARD['hand_r'][1], GUARD['hand_r'][2] - 0.6])
# ---- 6. Combat stance (between attacks), 2.0 s seamless loop ----
# The fighting stance itself with a slow breath, held between attacks instead of the relaxed idle (upper body only),
# so going between guarding and punching never changes the guard.
STANCE_BREATH = with_(STANCE, pelvis_off=[-1.5, -2.5, -3], spine_hi=[2, -3.8, 0],
                      hand_l=[12, 30, 141.4], hand_r=[-17, 20, 139.4])
COMBAT_STANCE = [(0.00, STANCE, 'smooth', {}),
                 (1.00, STANCE_BREATH, 'smooth', {'arm_l': 0.06, 'arm_r': 0.06}),
                 (2.00, STANCE, 'smooth', {})]

UNARMED_GUARD = [(0.00, GUARD, 'smooth', {}),
                 (0.50, GUARD_BREATH, 'smooth', {'arm_l': 0.04, 'arm_r': 0.04}),
                 (1.00, GUARD, 'smooth', {})]

# Both punches play 10% faster than their authored timing (key times and lags scale together).
PUNCH_SPEED = 1.1
def faster(keys, k):
    return [(k_[0] / k, k_[1], k_[2], {g: v / k for g, v in k_[3].items()}, *k_[4:]) for k_ in keys]
PUNCH, JAB = faster(PUNCH, PUNCH_SPEED), faster(JAB, PUNCH_SPEED)

# ---- Charged punches (RT / LT held): charged from the guard, rearing back ----
# The tap punch with a charge in it. Identical up to the punch's load key (its first small load from the guard; the
# game swaps clips there once the press is longer than a tap). Then, while the button is held, the body slowly rears
# back over CHARGE_REAR seconds (hips coil, weight eases back, the striking shoulder draws back with the fist riding
# along), reaching full coil about when the charge is full; then it holds there in a breathing loop
# [coil, coil + 2 * CHARGE_BREATH). A faint tremor runs through the arms the whole time. On release the strike fires
# from the coil, accelerating out of rest, with more torso rotation through the target and a longer, straighter arm
# than the tap punch, then the tap punch's recovery.
CHARGE_REAR = 0.75     # seconds from the load key to full coil (engages ~0.25 s after the press -> full at ~1 s)
CHARGE_BREATH = 0.45   # each half of the hold loop at full coil
CHARGE_EXTRA = 0.03    # a little more strike time for the longer turn from the coil
CHARGE_RECOVER = 1.3   # the recovery after a power strike is this much slower than the tap punch's, so the bigger
                       # turn unwinds back to guard without whipping
HOLD_SAMPLES = 28      # keys across the hold loop (every ~32 ms)
REAR_SAMPLES = 40      # keys across the rear-back
HOLD_SWAY, HOLD_TREMOR, HOLD_TREMOR_CYCLES = 1.0, 0.25, 7

def lerp_pose(a, b, t):
    return {k: [x + (y - x) * t for x, y in zip(a[k], b[k])] for k in a}

def shake(P, side, d):
    """Offset the striking fist (it rides with its shoulder) by d and the guard hand by half of it, out of phase."""
    g = 'l' if side == 'r' else 'r'
    off, gh = P['hand_off_' + side], P['hand_' + g]
    return with_(P, **{'hand_off_' + side: [off[i] + d[i] for i in range(3)], 'hand_' + g: [gh[i] - 0.5 * d[i] for i in range(3)]})

def tremor(u, cycles, amp):
    return [amp * math.sin(2 * math.pi * cycles * u), 0.0, amp * math.sin(2 * math.pi * cycles * u + 2)]

def hold_pose(coil, u, side):
    env = 0.5 - 0.5 * math.cos(2 * math.pi * u)                      # 0 at both ends of the loop, 1 mid-loop
    sway = [HOLD_SWAY * math.sin(2 * math.pi * u) * env, 0.0, 0.6 * HOLD_SWAY * math.sin(4 * math.pi * u + 1) * env]
    t = tremor(u, HOLD_TREMOR_CYCLES, HOLD_TREMOR * env)
    P = shake(coil, side, [sway[i] + t[i] for i in range(3)])
    return with_(P, pelvis_off=[coil['pelvis_off'][0], coil['pelvis_off'][1], coil['pelvis_off'][2] - 0.6 * env],
                 spine_hi=[coil['spine_hi'][0], coil['spine_hi'][1] - 0.6 * env, coil['spine_hi'][2]])

def rear_pose(load, coil, u, side):
    k = u * u * (3 - 2 * u)                                          # slow start, slow arrival
    return shake(lerp_pose(load, coil, k), side, tremor(u, round(CHARGE_REAR * 8), HOLD_TREMOR * math.sin(math.pi * u)))

def charged_keys(keys, side, coil, over, impact, follow, body, snap=()):
    t_load = keys[1][0]; load = keys[1][1]
    t_coil = t_load + CHARGE_REAR
    t_out = t_coil + 2 * CHARGE_BREATH
    shift = t_out - t_load + CHARGE_EXTRA
    rear = [(t_load + CHARGE_REAR * i / REAR_SAMPLES, rear_pose(load, coil, i / REAR_SAMPLES, side), 'flow', {})
            for i in range(1, REAR_SAMPLES)] + [(t_coil, coil, 'flow', {})]
    hold = [(t_coil + (t_out - t_coil) * i / HOLD_SAMPLES, hold_pose(coil, i / HOLD_SAMPLES, side), 'flow', {})
            for i in range(1, HOLD_SAMPLES)] + [(t_out, coil, 'flow', {})]
    strike = []
    for i, k in enumerate(keys[2:]):
        k = list(k); k[0] += shift
        if i == 0:      # the arm's arc: a big overhand, up and over
            k[1] = over
        if i == 1:      # impact: stronger pose; the body turns through it into the follow-through without stopping,
            # except the `snap` groups, which accelerate into it
            k[1] = impact; t_imp, imp_lags = k[0], k[3]
            k[4] = {**k[4], **{g: 'in' if g in snap else 'flow' for g in body}}
        if i == 2:      # follow-through: carries further round; every part reaches it a little after its impact
            k[1] = follow; t_follow = k[0]
            k[3] = {**k[3], **{g: max(k[3].get(g, 0.0), lag - (t_follow - t_imp) + 0.03) for g, lag in imp_lags.items()}}
        if i >= 3:      # recovery, slowed
            k[0] = t_follow + (k[0] - t_follow) * CHARGE_RECOVER; k[3] = {g: v * CHARGE_RECOVER for g, v in k[3].items()}
        strike.append(tuple(k))
    return keys[:2] + rear + hold + strike, (t_load, t_coil, t_out, shift)

# The charged strike is an overhand: the same arc as the tap punch, but much bigger - the fist rises well above the
# shoulder with the elbow high behind it, then comes down onto the target and carries on down and across.
P_OVER_C = {'loop_r': [-9, 0, 28], 'elbow_pole_r': [-0.4, -0.9, 0.55]}
J_OVER_C = {'loop_l': [6, 0, 24], 'elbow_pole_l': [0.5, -0.55, -0.08]}
# Right: hips and shoulders coil back, weight eases onto the rear leg, the right shoulder and fist draw back.
P_COIL = with_(P_LOAD, pelvis_off=[-2.5, -5, -6], pelvis_rot=[23, 0, 0], spine_lo=[7, -1.5, 0], spine_hi=[11, -2.5, -1],
               clav_r=[8, 0, 4], hand_off_r=[-12, 20, -2], foot_rot_r=[-2, -3, 0])
# Power drive: the torso sways forward into the punch and the right shoulder rolls forward and in along its path,
# so the chest and shoulder push the fist through; the hips carry further forward, the lead knee takes the weight
# and the rear leg drives long behind it.
P_IMPACT_C = with_(P_IMPACT, pelvis_off=[6, 17, -7.2], pelvis_rot=[-33, -9, -3], spine_lo=[-17, -13, 6], spine_hi=[-25, -17, 12],
                   clav_r=[-38, 0, 15], hand_r=[-2, 112, 134], elbow_pole_r=[0.05, -0.85, 0.55], reach_r=[0.997], fist_roll_r=[-50],
                   foot_rot_r=[-55, -36, 0])
P_FOLLOW_C = with_(P_IMPACT_C, pelvis_off=[6.4, 18, -7.5], pelvis_rot=[-37, -9, -3], spine_lo=[-19, -13 - 1, 6], spine_hi=[-29, -17 - 2, 13],
                   hand_r=[2, 112 + 4, 122], reach_r=[0.999])
# Left: the hips and chest open, weight eases back, the left shoulder and fist draw back; rear hand stays on guard.
J_COIL = with_(J_LOAD, pelvis_off=[-3, -5.5, -5.5], pelvis_rot=[-8, 0, 0], spine_lo=[-13, -1.5, 0], spine_hi=[-16, -2, 0],
               clav_l=[-8, 0, -4], hand_off_l=[3, 20, 2], foot_rot_r=[0, -3, 0])
# Power drive, as on the right: torso sways forward, the left shoulder rolls forward and in along the punch.
J_IMPACT_C = with_(J_IMPACT, pelvis_off=[1.5, 16, -6], pelvis_rot=[36, -8, 1], spine_lo=[15, -12, -1.5], spine_hi=[19, -16, -5],
                   clav_l=[30, 0, -12], hand_l=[-22, 114, 126], elbow_pole_l=[0.3, -0.7, 0.05], reach_l=[0.998], fist_roll_l=[10])
J_FOLLOW_C = with_(J_IMPACT_C, pelvis_off=[2, 17, -6.3], pelvis_rot=[40, -8, 1], spine_lo=[17, -12 - 1, -1.5], spine_hi=[22, -16 - 2, -5],
                   hand_l=[-22 - 5, 116, 118], reach_l=[0.999])
# (right: the chest and shoulder snap in - easing them lets the arm roll over at the top of the overhand)
PUNCH_CHARGED, PUNCH_CHARGED_T = charged_keys(PUNCH, 'r', P_COIL, P_OVER_C, P_IMPACT_C, P_FOLLOW_C, BODY, snap=('torso_hi', 'shoulder_r'))
JAB_CHARGED, JAB_CHARGED_T = charged_keys(JAB, 'l', J_COIL, J_OVER_C, J_IMPACT_C, J_FOLLOW_C, BODY_L)


# ---- Charged kicks (Y held) ----
# The tap kick with a hold in it at its hold frame (right kick: on the ground, weight shifted onto the lead leg; left
# kick: chambered). The game swaps clips there once the press is longer than a tap. While held, a breathing loop
# [hold, hold + 2 * CHARGE_BREATH) keeps the locked stance alive: a slow breath, a slight sway in the guard hands and,
# for the chambered leg, a small balance hover in the raised foot. All of it fades to zero at the loop's ends;
# release continues the tap kick from the hold frame.
def kick_hold_pose(P, u, raised=None):
    env = 0.5 - 0.5 * math.cos(2 * math.pi * u)
    sway = [0.8 * math.sin(2 * math.pi * u) * env, 0.0, 0.5 * math.sin(4 * math.pi * u + 1) * env]
    t = [0.15 * math.sin(2 * math.pi * 7 * u) * env, 0.0, 0.15 * math.sin(2 * math.pi * 7 * u + 2) * env]
    d = [sway[i] + t[i] for i in range(3)]
    kw = dict(pelvis_off=[P['pelvis_off'][0] + 0.4 * math.sin(2 * math.pi * u) * env, P['pelvis_off'][1], P['pelvis_off'][2] - 0.5 * env],
              spine_hi=[P['spine_hi'][0], P['spine_hi'][1] - 0.6 * env, P['spine_hi'][2]],
              hand_l=[P['hand_l'][i] + d[i] for i in range(3)], hand_r=[P['hand_r'][i] - d[i] for i in range(3)])
    if raised:
        f = P['foot_' + raised]
        kw['foot_' + raised] = [f[0] + 0.6 * math.sin(2 * math.pi * u + 0.5) * env, f[1], f[2] + 0.8 * math.sin(4 * math.pi * u) * env]
    return with_(P, **kw)
def kick_charged(keys, t_hold, raised=None, power=None):
    P = sample(keys, t_hold, GROUPS)
    t_out = t_hold + 2 * CHARGE_BREATH
    hold = [(t_hold + (t_out - t_hold) * i / HOLD_SAMPLES, kick_hold_pose(P, i / HOLD_SAMPLES, raised), 'flow', {})
            for i in range(1, HOLD_SAMPLES)]
    out = ([k for k in keys if k[0] < t_hold - 1e-6] + [(t_hold, P, 'flow', {})] + hold + [(t_out, P, 'flow', {})]
           + [(k[0] + (t_out - t_hold), (power or {}).get(id(k[1]), k[1])) + tuple(k[2:]) for k in keys if k[0] > t_hold + 1e-6])
    return out, (t_hold, t_out, t_out - t_hold)
# Power kicks: after the hold the hips thrust further through the kick and the torso tilts back to counter it
# (chin stays down so the eyes stay on the target); the foot reaches a little further and the support leg gives a
# touch to absorb it.
RK_IMPACT_P = with_(RK_IMPACT, pelvis_off=[3, 28, 2], pelvis_rot=[-14, 32, -8], spine_lo=[0, 10, 0], spine_hi=[0, 8, 1],
                    foot_r=[-4, 110, 121], head_rot=[-4, -12, 0])
RK_FOLLOW_P = with_(RK_IMPACT_P, foot_r=[-4, 110 + 4, 123], pelvis_off=[3.5, 30, 2.5], pelvis_rot=[-15, 32 + 2, -8])
LK_IMPACT_P = with_(LK_IMPACT, pelvis_off=[-1, 20, 1.5], pelvis_rot=[8, 33, 8], spine_lo=[0, 9, 1], spine_hi=[0, 8, 1],
                    foot_l=[13, 112, 120], head_rot=[-4, -12, 0], hand_r=[-19, 44, 140])
LK_FOLLOW_P = with_(LK_IMPACT_P, foot_l=[13, 116, 122], pelvis_off=[-0.5, 22, 2], pelvis_rot=[8, 33 + 2, 8])
RIGHT_KICK_CHARGED, RIGHT_KICK_CHARGED_T = kick_charged(RIGHT_KICK, 0.1167, power={id(RK_IMPACT): RK_IMPACT_P, id(RK_FOLLOW): RK_FOLLOW_P})
LEFT_KICK_CHARGED, LEFT_KICK_CHARGED_T = kick_charged(LEFT_KICK, 0.2333, raised='l', power={id(LK_IMPACT): LK_IMPACT_P, id(LK_FOLLOW): LK_FOLLOW_P})

CLIPS = [('RPG_Proto_Punch_R', PUNCH, 0.275 / PUNCH_SPEED),
         ('RPG_Proto_FrontKick_R', RIGHT_KICK, 0.40),
         ('RPG_Proto_FrontKick_L', LEFT_KICK, 0.33),
         ('RPG_Proto_Punch_L', JAB, 0.235 / PUNCH_SPEED),
         ('RPG_Proto_Punch_R_Charged', PUNCH_CHARGED, 0.275 / PUNCH_SPEED + PUNCH_CHARGED_T[3]),
         ('RPG_Proto_Punch_L_Charged', JAB_CHARGED, 0.235 / PUNCH_SPEED + JAB_CHARGED_T[3]),
         ('RPG_Proto_FrontKick_R_Charged', RIGHT_KICK_CHARGED, 0.40 + RIGHT_KICK_CHARGED_T[2]),
         ('RPG_Proto_FrontKick_L_Charged', LEFT_KICK_CHARGED, 0.33 + LEFT_KICK_CHARGED_T[2]),
         ('RPG_Proto_UnarmedGuard', UNARMED_GUARD, None),
         ('RPG_Proto_CombatStance', COMBAT_STANCE, None)]

if __name__ == '__main__':
    out = {}
    for name, keys, impact in CLIPS:
        clip = bake(name, keys, GROUPS); clip['impact'] = impact
        clip['keys'] = [k[0] for k in keys]
        out[name] = clip
        print(name, clip['frames'], 'frames', round((clip['frames'] - 1) / FPS, 3), 's')
    json.dump(out, open(os.path.join(HERE, 'clips.json'), 'w'))
