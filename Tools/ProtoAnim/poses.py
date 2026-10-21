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
    'hips': ('pelvis_off', 'pelvis_rot', 'balance', 'weight_l'),
    'torso_lo': ('spine_lo',),
    'torso_hi': ('spine_hi',),
    'shoulder_r': ('clav_r',),
    'arm_r': ('hand_r', 'elbow_pole_r', 'reach_r', 'fist_roll_r', 'guard_follow_r', 'guard_raise_r', 'hand_rel_r', 'hand_off_r', 'loop_r'),
    'arm_l': ('hand_l', 'elbow_pole_l', 'reach_l', 'fist_roll_l', 'clav_l', 'guard_follow_l', 'guard_raise_l', 'hand_rel_l', 'hand_off_l', 'loop_l'),
    'leg_r': ('foot_r', 'foot_rot_r', 'foot_turn_r', 'knee_pole_r', 'pivot_r', 'plant_r'),
    'leg_l': ('foot_l', 'foot_rot_l', 'foot_turn_l', 'knee_pole_l', 'pivot_l', 'plant_l'),
    'head': ('head_rot',),
}

import protoanim as _pa
# Compact stance (2026-10-20 rework): the rear foot stands turned out ~27 deg (was ~47), applied under every pose by the
# generator's base foot turn, so each pose's own rear-foot pivot keeps its relative motion.
_pa.FOOT_BASE_TURN['r'] = -20.0
# General-purpose stance (2026-10-21, for every NPC from peasant to expert; power comes from the Power System, not
# from perfect technique): eyes forward instead of at the floor (the face pointed 17 deg down, now ~5), guard fists up
# at cheek / jaw height and a little apart (they sat at the collarbones), feet a little wider (see STANCE plant_*).
# Torso turn unchanged. Applied under every pose, so attacks keep their shapes and nothing snaps mid-move.
_pa.HEAD_PITCH_LIFT = 12.0
_pa.GUARD_OFFSET['l'] = [3.0, 0.0, 6.0]
_pa.GUARD_OFFSET['r'] = [-3.0, 0.0, 9.0]
# Orthodox fighting stance: feet bladed, upper body fairly square, soft knees, chin slightly down. Guard: rear fist
# in front of the cheek (not on the jaw), lead fist held in close enough that the left punch has room to travel.
# The lead foot is turned out ~18 deg (foot_rot_l yaw -24; every foot_rot_l below includes that same -24).
STANCE = {
    'pelvis_off': [-1.5, -2.5, -2.5], 'pelvis_rot': [11, 0, 0], 'balance': [0], 'weight_l': [0.5],
    'spine_lo': [1, -2, 0], 'spine_hi': [2, -3, 0],
    'clav_l': [0, 0, -3], 'clav_r': [0, 0, 2],
    'head_rot': [-4, -5, 0],
    'hand_r': [-17, 20, 140], 'elbow_pole_r': [-0.4, -0.3, -1], 'reach_r': [0.97], 'fist_roll_r': [0], 'guard_follow_r': [1], 'guard_raise_r': [1], 'hand_rel_r': [0], 'hand_off_r': [3, 22, -4], 'loop_r': [0, 0, 0],
    'hand_l': [12, 30, 142], 'elbow_pole_l': [0.5, 0, -1], 'reach_l': [0.97], 'fist_roll_l': [0], 'guard_follow_l': [1], 'guard_raise_l': [1], 'hand_rel_l': [0], 'hand_off_l': [-2, 22, -4], 'loop_l': [0, 0, 0],
    'foot_l': L_FOOT, 'foot_rot_l': [-24, 0, 0], 'foot_turn_l': [0], 'knee_pole_l': [0.22, 1, 0], 'pivot_l': [1], 'plant_l': [3.5, 3.5, 0],
    'foot_r': R_FOOT, 'foot_rot_r': [0, 0, 0], 'foot_turn_r': [0], 'knee_pole_r': [-0.65, 1, 0], 'pivot_r': [1], 'plant_r': [-3.5, -1, 0],
}
def planted_ankle(P, side):
    """Where the ankle of a planted foot actually is in pose P (pivoting on its ball), so a foot can leave from / land on
    exactly that spot: straight up and straight down, no scuff."""
    from protoanim import build_pose
    return build_pose(P, return_cs=True)['foot_' + side][1]
# A planted foot's position comes from its ball (pivot_*); foot_* is the free-foot target. Authoring foot_* exactly where
# the planted foot already is means a foot changing between planted and free never drifts between two spots.
STANCE['foot_l'], STANCE['foot_r'] = list(planted_ankle(STANCE, 'l')), list(planted_ankle(STANCE, 'r'))
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
           hand_r=[-17.5, 17, 136], elbow_pole_r=[-0.5, -0.3, -1], guard_follow_r=[0], hand_rel_r=[0], hand_off_r=[-2, 26, -7],
           foot_rot_r=[-5, -6, 0])
# Overhand loop (arm only): an up-and-out bump that peaks mid-strike and is falling as the fist lands,
# while the elbow lifts on its way over.
# The fist's path is authored directly (component space): from the chin it rises and starts forward as the shoulder
# begins to turn, tops its arc above shoulder height about two thirds of the way out, and comes down onto the target.
# Its speed builds the whole way (fastest just before contact) and the arm extends progressively with the turn, rather
# than the body turning first and the arm firing alone at the end.
P_ARC_A = {'hand_r': [-17, 34, 141], 'elbow_pole_r': [-0.6, -0.6, -0.8]}
P_LOOP = {'hand_r': [-10, 66, 148], 'elbow_pole_r': [-0.3, -1, -0.15]}
# Impact: level change, hips and chest turned through, fist arriving downward with the elbow high,
# right shoulder covering the chin, left hand on the face.
P_IMPACT = S(pelvis_off=[5, 16, -6.5], pelvis_rot=[-28, -5, -3],
             spine_lo=[-14, -8, 6], spine_hi=[-20, -10, 11],
             clav_r=[-28, 0, 13], clav_l=[8, 0, -2],
             hand_r=[-2, 100, 140], elbow_pole_r=[0.15, -1, 0.15], reach_r=[0.995], fist_roll_r=[-20], guard_follow_r=[0],
             hand_l=[8, 27, 147], elbow_pole_l=[0.6, 0.2, -1],
             foot_rot_r=[-35, -20, 0], knee_pole_r=[0.05, 1, -0.15],
             head_rot=[-10, -12, 5])
# Follow-through: the body finishes its turn while the fist carries on down and across.
P_FOLLOW = with_(P_IMPACT, pelvis_off=[5.6, 17, -6.8], pelvis_rot=[-31, -5, -3],
                 spine_lo=[-15, -9, 6], spine_hi=[-23, -11, 12],
                 hand_r=[4, 111, 130], reach_r=[0.998])
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
              'hand_off_r': 'flow', 'loop_r': 'flow'}   # only the fist path accelerates into the target
# Kinetic chain. Nothing leaves the load before the switch frame (the game can swap to the Charged clip there), then
# each link starts, peaks and arrives a little after the one below it:
#   rear foot / leg  arrive 0.215   (heel turns out and lifts, knee drives in over the toes)
#   hips             arrive 0.225, then keep turning slowly into the follow-through
#   lower / upper torso 0.245 / 0.265, held back early by the stretch key so the hips lead (hip-shoulder separation)
#   right shoulder   0.29 (the shoulder roll finishes just after contact, covering the chin)
#   fist             0.275 (impact), arriving fast and braking over the follow-through
# The stretch key: at 0.165 the hips are ~19 deg into their turn while the chest has only turned ~6 deg; the
# stored-up twist then releases into the strike.
P_STRETCH = only(S(spine_lo=[5, -4, 2], spine_hi=[7, -5, 3]), GROUPS, 'torso_lo', 'torso_hi')
PUNCH_IMPACT_LAGS = {'leg_r': -0.06, 'hips': -0.04, 'torso_lo': 0.0, 'torso_hi': 0.02, 'shoulder_r': 0.03,
                     'head': 0.015, 'arm_l': -0.03, 'arm_r': 0.0}
PUNCH = [(0.00, STANCE, 'smooth', {}),
         (0.10, P_LOAD, 'smooth', {}),
         (0.16, P_STRETCH, 'flow', {}),
         (0.18, P_ARC_A, 'flow', {}),
         (0.23, P_LOOP, 'flow', {}),
         (0.275, P_IMPACT, 1.0, PUNCH_IMPACT_LAGS, {**B('flow'), **SMOOTH_ARM, 'arm_l': 'flow'}),
         # follow-through: the body comes to the end of its turn link by link, the arm at full extension
         (0.34, P_FOLLOW, 'out', {'leg_r': -0.05, 'hips': -0.04, 'torso_lo': -0.005, 'torso_hi': 0.02, 'shoulder_r': 0.03,
                                  'head': 0.04, 'arm_r': -0.01}),
         # the fist comes home first and fast; the body unwinds behind it, the rear heel sets down last
         (0.46, P_RETRACT, 'flow', {'arm_r': 0.04, 'shoulder_r': 0.04, 'hips': 0.0, 'torso_lo': 0.03, 'torso_hi': 0.05,
                                    'leg_r': 0.06, 'head': 0.05}),
         (0.59, P_SETTLE, 'flow', {'hips': 0.0, 'torso_lo': 0.01, 'torso_hi': 0.02, 'head': 0.02}),
         (0.68, STANCE, 'smooth', {'arm_r': -0.03, 'arm_l': -0.03})]

# ---- 2. Right teep (rear-leg push kick), 0.77 s ----
# Only the kicking leg and the hips snap into impact; arms, torso, head and support leg flow.
# Weight settles onto the lead leg, knee comes up toward the chest, then the hips thrust
# forward and the leg pushes out through the ball of the foot while the torso leans back.
# Combat kick: both hands stay in guard, carried with the head (guard_follow, the default) so they stay on the
# face while the torso leans back. The rear hand sways a little out and down with the kick, never behind the body.
# Balance: from the load on, the balance solver holds the centre of mass over the lead (left) foot (weight_l 1): the
# weight is transferred BEFORE the rear foot leaves the floor and stays over the standing foot until the kicking foot
# is back down. The poses below only author posture (lean, hip drive); the solver places the hips over the base.
RK_LOAD = S(pelvis_off=[6, 4, -4.5], pelvis_rot=[7, 0, -2], spine_lo=[0, -1, 1], spine_hi=[1, -2, 2], balance=[0.8], weight_l=[0.65],
            foot_rot_r=[0, -20, 0], hand_r=[-16, 12, 136])
# Lift-off (kicking leg only): the heel has peeled, now the foot rises nearly straight up, toes hanging down, so the
# toe never scuffs forward along the floor.
_ra = planted_ankle(RK_LOAD, 'r'); RK_LOAD['foot_r'] = list(_ra)
RK_LIFT = only(S(foot_r=[_ra[0], _ra[1] + 1.0, _ra[2] + 9.0], foot_rot_r=[0, -24, 0], foot_turn_r=[0], pivot_r=[0], knee_pole_r=[-0.1, 1, 0.6]),
               GROUPS, 'leg_r')
# Knee-up (kicking leg only): the thigh lifts first, the foot tucked under the knee (below and a little ahead of the hip),
# before the shin swings forward into the chamber - the knee leads, the foot follows.
RK_KNEEUP = only(S(foot_r=[2, 34, 58], foot_rot_r=[-8, -22, -3], foot_turn_r=[-4], pivot_r=[0], knee_pole_r=[0, 1, 1.0]), GROUPS, 'leg_r')
RK_CHAMBER = S(pelvis_off=[5, 1, -2.5], pelvis_rot=[1, 4, -3], spine_lo=[-2, 3, 2], spine_hi=[-1, 2, 3], balance=[1], weight_l=[1],
               foot_r=[2, 44, 92], foot_rot_r=[-14.5, -12, -6], foot_turn_r=[-6.5], knee_pole_r=[0, 1, 1.5], pivot_r=[0],
               hand_r=[-16, 22, 136], elbow_pole_r=[-0.4, -0.2, -1], hand_l=[11, 36, 142])
# Chamber: the foot comes in under its own hip as it rises and the knee lifts straight up and forward in front of that
# hip (no swing out and across). Knee lifted high with the foot well up (ankle ~90 cm), so the extension drives FORWARD into the target, not
# up into it - a push, not a flick. The target is the midsection (ankle ~106 cm), level with the chamber. The foot finishes turning sole-to-target ~40 ms before the impact frame (foot_rot lag), so it
# the target, so it
# lands set and pushes through the sole instead of still flicking up at contact.
# The rear foot stands turned out ~27 deg; it straightens toward the target as the knee lifts (foot_turn_r) and then
# tips up, so the sole faces the target upright instead of rolling onto its side.
# Impact: the hips drive forward and tilt back, the upper body counter-leans so the weight stays over the standing
# foot (which turns out a little more on its ball), the support knee straightens a touch (hips rise ~1 cm, not 5),
# and the leg arrives with the knee just short of locked, pushing through the ball of the foot.
RK_IMPACT = S(pelvis_off=[3, 18, 1], pelvis_rot=[-12, 22, -8], spine_lo=[0, 7, 3], spine_hi=[0, 5, 4], balance=[1], weight_l=[1],
              foot_r=[1, 106, 107], foot_rot_r=[0, 92, 0], foot_turn_r=[-25], knee_pole_r=[0, 1, 1.5], pivot_r=[0],
              foot_rot_l=[-34, -6, 0],
              hand_r=[-20, 34, 132], elbow_pole_r=[-0.45, -0.2, -1],
              hand_l=[12, 36, 144], head_rot=[-4, -12, 0])
RK_FOLLOW = with_(RK_IMPACT, foot_r=[1, 114, 109], pelvis_off=[3.5, 20.5, 1.2], pelvis_rot=[-13, 24, -8])
# Recoil: the knee snaps back to the chamber with the body coming upright over the standing foot.
RK_RETRACT = with_(RK_CHAMBER, foot_r=[1, 32, 72], foot_rot_l=[-30, -3, 0])
# Set-down (kicking leg + weight): the foot comes down onto its spot from above, then takes weight again.
_rs = planted_ankle(STANCE, 'r')
RK_DOWN = only(S(foot_r=[_rs[0], _rs[1], _rs[2] + 7.0], foot_rot_r=[0, 0, 0], foot_turn_r=[0], pivot_r=[0], knee_pole_r=[-0.15, 1, 0.1]),
               GROUPS, 'leg_r')
RK_DOWN['balance'], RK_DOWN['weight_l'] = [1], [0.95]
# Touchdown (kicking leg only): planted on its stance spot, so the hips settle over a pinned foot.
RK_TOUCH = only(S(foot_r=list(_rs)), GROUPS, 'leg_r')
KICK_FLOW = ('arm_r', 'arm_l', 'torso_lo', 'torso_hi', 'head', 'leg_l')
# the weight finishes moving over the standing foot as the kicking foot leaves the floor
RK_SHIFT = {'balance': [1], 'weight_l': [1]}
RIGHT_KICK = [(0.00, STANCE, 'smooth', {}),
              (0.12, RK_LOAD, 'smooth', {'leg_l': -0.02, 'arm_r': 0.02}),
              (0.175, RK_LIFT, 'flow', {}),
              (0.20, RK_SHIFT, 'flow', {}),
              (0.232, RK_KNEEUP, 'flow', {}),
              (0.28, RK_CHAMBER, 'flow', {'hips': -0.02, 'leg_r': 0.0, 'torso_hi': 0.02, 'arm_r': 0.02}),
              (0.40, RK_IMPACT, 'strike', {'hips': -0.03, 'leg_l': -0.03, 'torso_lo': 0.0, 'torso_hi': 0.015, 'arm_r': 0.02, 'arm_l': 0.02, 'head': 0.02,
                                         'foot_rot_r': -0.04, 'foot_turn_r': -0.04},
               {**{g: 'flow' for g in KICK_FLOW}, 'hips': 'flow'}),
              (0.47, RK_FOLLOW, 'out', {'hips': -0.01, 'torso_hi': 0.02, 'head': 0.025}),
              (0.64, RK_RETRACT, 'flow', {'leg_r': 0.0, 'hips': 0.02, 'torso_hi': 0.03, 'arm_r': 0.03, 'head': 0.03}),
              (0.75, RK_DOWN, 'flow', {}),
              (0.80, RK_TOUCH, 'smooth', {}),
              (0.86, STANCE, 'smooth', {'arm_r': -0.03, 'arm_l': -0.03, 'torso_hi': -0.02, 'hips': 0.0, 'foot_rot_r': -0.06, 'foot_turn_r': -0.06})]

# ---- 3. Left teep (lead-leg push kick), 0.68 s ----
# Not a mirror: the lead leg is already forward, so weight rocks back onto the rear leg,
# the chamber is quicker, the lean-back is bigger and the hips drive less. Hands stay in guard; the lead hand
# sways a little out and down with the kick.
# Balance: the weight rocks back over the rear (right) foot before the lead foot leaves the floor and stays there until
# it is back down (balance solver, weight_l 0).
LK_LOAD = S(pelvis_off=[-4, -6, -4.5], pelvis_rot=[13, 3, 2], spine_lo=[0, 0, -1], spine_hi=[1, -1, -2], balance=[0.8], weight_l=[0.3],
            foot_rot_l=[-24, -15, 0])
_la = planted_ankle(LK_LOAD, 'l'); LK_LOAD['foot_l'] = list(_la)
# Lift-off (kicking leg only): straight up off its spot, heel first.
LK_LIFT = only(S(foot_l=[_la[0], _la[1] + 1.0, _la[2] + 9.0], foot_rot_l=[-24, -18, 0], pivot_l=[0], knee_pole_l=[0.2, 1, 0.6]), GROUPS, 'leg_l')
LK_SHIFT = {'balance': [1], 'weight_l': [0], 'pivot_l': [1]}   # (the foot stays planted until the weight is off it)
# Knee-up (kicking leg only): the thigh lifts first with the foot under the knee, then the shin comes up to the chamber.
LK_KNEEUP = only(S(foot_l=[6, 38, 60], foot_rot_l=[-26, -18, 0], pivot_l=[0], knee_pole_l=[0, 1, 1.0]), GROUPS, 'leg_l')
# (the kick travels along the line from the lead hip, which sits over the rear foot now, to the target's centre)
LK_CHAMBER = S(pelvis_off=[-5, -8, -3], pelvis_rot=[11, 8, 4], spine_lo=[-6, 4, -2], spine_hi=[-7, 3, -3], balance=[1], weight_l=[0],
               foot_l=[6, 44, 95], foot_rot_l=[-28, -20, 0], knee_pole_l=[0.1, 1, 1.5], pivot_l=[0],
               hand_l=[15, 42, 138.5], elbow_pole_l=[0.8, 0, -0.7],
               hand_r=[-14, 22, 140])
# Impact: hips push forward and tilt back, the chest counter-leans, the hips rise ~1 cm as the support knee straightens a
# touch, the leg arrives just short of locked. The lead leg can't reach the target with the weight fully over the rear
# foot, so at contact the fighter commits forward onto the kick (balance 0.6): the target pushing back on the foot is
# part of the support for that moment, as in a real push kick that lands; balanced again in the recoil.
LK_IMPACT = S(pelvis_off=[-1, 15, 1], pelvis_rot=[8, 22, 8], spine_lo=[0, 10, -3], spine_hi=[0, 8, -4], balance=[0.6], weight_l=[0],
              foot_l=[1, 97, 106], foot_rot_l=[-32, 92, 0], knee_pole_l=[0.1, 1, 1.5], pivot_l=[0],
              foot_rot_r=[12, -6, 0],
              hand_l=[18, 42, 135], elbow_pole_l=[0.9, 0, -0.6],
              hand_r=[-16, 36, 141], head_rot=[-4, -12, 0])
LK_FOLLOW = with_(LK_IMPACT, foot_l=[0, 102, 108], pelvis_off=[-0.5, 16.5, 1.2], pelvis_rot=[8, 23, 8])
LK_RETRACT = with_(LK_CHAMBER, foot_l=[8, 32, 76], foot_rot_r=[6, -2, 0])
_ls = planted_ankle(STANCE, 'l')
LK_DOWN = only(S(foot_l=[_ls[0], _ls[1], _ls[2] + 7.0], foot_rot_l=[-24, 0, 0], pivot_l=[0], knee_pole_l=[0.1, 1, 0.1]), GROUPS, 'leg_l')
LK_TOUCH = only(S(foot_l=list(_ls)), GROUPS, 'leg_l')
LEFT_KICK = [(0.00, STANCE, 'smooth', {}),
             (0.10, LK_LOAD, 'smooth', {'leg_r': -0.02}),
             (0.14, LK_SHIFT, 'flow', {}),
             (0.158, LK_LIFT, 'flow', {}),
             (0.19, LK_KNEEUP, 'flow', {}),
             (0.22, LK_CHAMBER, 'flow', {'hips': -0.02, 'torso_hi': 0.02, 'arm_l': 0.02}),
             (0.33, LK_IMPACT, 'strike', {'hips': -0.03, 'leg_r': -0.02, 'torso_lo': 0.0, 'torso_hi': 0.015, 'arm_l': 0.02, 'head': 0.02,
                                        'foot_rot_l': -0.035},
              {**{g: 'flow' for g in ('arm_r', 'arm_l', 'torso_lo', 'torso_hi', 'head', 'leg_r')}, 'hips': 'flow'}),
             (0.395, LK_FOLLOW, 'out', {'hips': -0.01, 'torso_hi': 0.02, 'head': 0.025}),
             (0.53, LK_RETRACT, 'flow', {'leg_l': 0.0, 'hips': 0.02, 'torso_hi': 0.03, 'arm_l': 0.03, 'head': 0.03}),
             (0.63, LK_DOWN, 'flow', {}),
             (0.67, LK_TOUCH, 'smooth', {}),
             (0.73, STANCE, 'smooth', {'arm_l': -0.03, 'arm_r': -0.03, 'torso_hi': -0.02, 'hips': 0.0, 'foot_rot_l': -0.05})]

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
J_LOAD = S(pelvis_off=[-0.8, -2.8, -4.0], pelvis_rot=[8, 0, 0], spine_lo=[-1, -1.8, 0], spine_hi=[-2, -2.6, 0],
           clav_l=[-3, 0, -2],
           hand_l=[15, 31, 139], elbow_pole_l=[0.6, -0.2, -1], guard_follow_l=[0], hand_rel_l=[0], hand_off_l=[-2, 24, 0],
           foot_rot_r=[0, -5, 0])
# Elbow arc (arm only): the elbow swings out and up above the shoulder while the arm stays bent, then
# sweeps across with the turn; the fist rides behind it.
# The fist's path, authored directly: it travels out behind the elbow, which swings wide and rises above the shoulder;
# the arc tops out about two thirds of the way, then the fist sweeps across and down onto the target past the centre.
J_ARC_A = {'hand_l': [15, 46, 142], 'elbow_pole_l': [0.6, -0.4, -0.8]}
J_LOOP = {'hand_l': [7, 68, 143], 'elbow_pole_l': [0.45, -0.45, -0.4]}
# Stretch: the hips have started their turn while the chest is still held open (the lead side's hip-shoulder separation).
J_STRETCH = only(S(spine_lo=[-8, -3, 0], spine_hi=[-10, -4, -1]), GROUPS, 'torso_lo', 'torso_hi')
# Impact: hips and chest turned through, weight over the lead leg with the rear leg pushed long (heel up,
# rear knee kept over its toes), lead shoulder forward, across and up, elbow a little high behind the fist, fist
# turned over and landing a touch down and across the centreline, rear hand tight on the jaw, head slipped right.
J_IMPACT = S(pelvis_off=[1, 10, -4.8], pelvis_rot=[31, -3, 1],
             spine_lo=[12, -6.5, -1.5], spine_hi=[15, -8.5, -5],
             clav_l=[20, 0, -10], clav_r=[-4, 0, 2],
             hand_l=[-9, 99, 132], elbow_pole_l=[0.3, -0.6, 0.1], reach_l=[0.995], fist_roll_l=[10], guard_follow_l=[0],
             hand_r=[-16, 20, 141],
             foot_rot_l=[-20, 0, 0], foot_rot_r=[0, -10, 0], knee_pole_l=[0.25, 1, 0], knee_pole_r=[-0.75, 1, 0],
             head_rot=[3, -9, -6])
# Follow-through: the body finishes its turn while the fist carries on a few cm down and across.
J_FOLLOW = with_(J_IMPACT, pelvis_off=[1, 11, -4.9], pelvis_rot=[33, -3, 1],
                 spine_lo=[13, -6.5, -1.5], spine_hi=[16.5, -9, -5],
                 hand_l=[-15, 108, 126], reach_l=[0.998])
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
# Chain (authored times; impact 0.235): rear leg 0.16, hips 0.17, lower torso 0.22, upper torso 0.255 and lead
# shoulder 0.26 (the chest arrives late, so the raised elbow stays in front of it), fist 0.235.
JAB_IMPACT_LAGS = {'leg_r': -0.075, 'hips': -0.065, 'torso_lo': -0.015, 'torso_hi': 0.02, 'clav_l': 0.025, 'head': -0.01,
                   'arm_r': -0.03, 'arm_l': 0.0}
JAB = [(0.00, STANCE, 'smooth', {}),
       (0.06, J_LOAD, 'smooth', {}),
       (0.13, J_STRETCH, 'flow', {}),
       (0.12, J_ARC_A, 'flow', {}),
       (0.175, J_LOOP, 'flow', {}),
       (0.235, J_IMPACT, 1.0, JAB_IMPACT_LAGS, {**BL('flow'), **SMOOTH_ARM_L, 'arm_r': 'flow'}),
       (0.30, J_FOLLOW, 'out', {'leg_r': -0.05, 'hips': -0.045, 'torso_lo': -0.005, 'torso_hi': 0.02, 'clav_l': 0.03, 'head': 0.03,
                                'arm_l': -0.005}),
       (0.41, J_RETRACT, 'flow', {'arm_l': 0.08, 'clav_l': 0.05, 'hips': 0.0, 'torso_lo': 0.03, 'torso_hi': 0.04, 'leg_r': 0.05, 'head': 0.04}),
       (0.505, J_SETTLE, 'flow', {'torso_hi': 0.015, 'head': 0.015}),
       (0.60, STANCE, 'smooth', {'arm_l': -0.03, 'arm_r': -0.03})]

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
# (guard_raise 0: the held block keeps its own hands-at-the-face layout instead of adding the stance's guard raise)
GUARD = S(hand_l=[_gl[0] + _gd[0], _gl[1] + _gd[1], _gl[2] + GUARD_UP],
          hand_r=[_gr[0] - _gd[0], _gr[1] - _gd[1], _gr[2] + GUARD_UP], guard_raise_l=[0], guard_raise_r=[0])
# ---- 6. Combat stance (between attacks) and 5. guard: 2.0 s seamless loops ----
# A living stance instead of a single breathing sine: layered, low-amplitude motions at different rhythms, each a whole
# number of cycles per loop (cosine shaped, so the loop wraps with no jump in position or speed):
#   breath (1 cycle): chest rises and tips back a touch, shoulders lift slightly, hips settle as the breath goes out
#   weight shift (1 cycle, a quarter loop behind the breath): a ~1 cm sway toward the lead foot and back, the head
#     leaning a fraction the other way to stay level over it
#   hands (2 and 3 cycles, the two hands out of step): a few mm of drift, as a held guard is never dead still
# Every component starts at 0, so the loop begins exactly on the stance / guard pose the attacks blend from.
LOOP_KEYS = 32
def living_loop(base, T=2.0, scale=1.0):
    def c(k, t, phase=0.0):   # 0 at the loop start, 1 at its peak; whole cycles, zero speed at both ends of the loop
        return 0.5 - 0.5 * math.cos(2 * math.pi * (k * t / T + phase))
    keys = []
    for i in range(LOOP_KEYS + 1):
        t = T * i / LOOP_KEYS
        br, sh = c(1, t), c(1, t, 0.25) - c(1, 0, 0.25)            # breath; weight shift (shifted, re-zeroed)
        h2, h3 = c(2, t), c(3, t)
        d = {k: list(v) for k, v in base.items()}
        po, sp, cl, cr, hr = base['pelvis_off'], base['spine_hi'], base['clav_l'], base['clav_r'], base['head_rot']
        d['pelvis_off'] = [po[0] + 0.9 * scale * sh, po[1] + 0.35 * scale * sh, po[2] - 0.45 * scale * br]
        d['spine_hi'] = [sp[0], sp[1] + 0.9 * scale * br, sp[2] - 0.5 * scale * sh]
        d['clav_l'] = [cl[0], cl[1], cl[2] - 0.7 * scale * br]
        d['clav_r'] = [cr[0], cr[1], cr[2] + 0.7 * scale * br]
        d['head_rot'] = [hr[0], hr[1], hr[2] - 0.8 * scale * sh]
        hl, hrr = base['hand_l'], base['hand_r']
        d['hand_l'] = [hl[0] + 0.35 * scale * h3, hl[1] + 0.25 * scale * h2, hl[2] - 0.5 * scale * br + 0.3 * scale * h2]
        d['hand_r'] = [hrr[0] - 0.3 * scale * h2, hrr[1] + 0.3 * scale * h3, hrr[2] - 0.5 * scale * br + 0.25 * scale * h3]
        keys.append((t, d, 'smooth' if i in (0, LOOP_KEYS) else 'flow', {}))
    return keys
COMBAT_STANCE = living_loop(STANCE)
UNARMED_GUARD = living_loop(GUARD, scale=0.8)   # a held guard is tighter: a little less of everything

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

def charged_keys(keys, side, coil, swap, body, snap=(), roles=None):
    """swap: {id(tap key params): charged key params} - the strike's own arc, stretch, impact and follow-through.
    roles: (impact key params, follow key params) of the tap punch (found by identity, for the lag handling)."""
    imp_p, fol_p = roles
    t_load = keys[1][0]; load = keys[1][1]
    t_coil = t_load + CHARGE_REAR
    t_out = t_coil + 2 * CHARGE_BREATH
    shift = t_out - t_load + CHARGE_EXTRA
    rear = [(t_load + CHARGE_REAR * i / REAR_SAMPLES, rear_pose(load, coil, i / REAR_SAMPLES, side), 'flow', {})
            for i in range(1, REAR_SAMPLES)] + [(t_coil, coil, 'flow', {})]
    hold = [(t_coil + (t_out - t_coil) * i / HOLD_SAMPLES, hold_pose(coil, i / HOLD_SAMPLES, side), 'flow', {})
            for i in range(1, HOLD_SAMPLES)] + [(t_out, coil, 'smooth', {})]
    strike, t_imp, imp_lags, t_follow = [], None, {}, None
    for k in keys[2:]:
        k = list(k); k[0] += shift; tap = k[1]
        k[1] = swap.get(id(tap), tap)
        if tap is imp_p:        # impact: the body turns through it into the follow-through without stopping, except
            # the `snap` groups, which accelerate into it
            t_imp, imp_lags = k[0], k[3]
            k[4] = {**k[4], **{g: 'strike' if g in snap else 'flow' for g in body}}
        elif tap is fol_p:      # follow-through: carries further round; every part reaches it a little after its impact
            t_follow = k[0]
            k[3] = {**k[3], **{g: max(k[3].get(g, 0.0), lag - (t_follow - t_imp) + 0.03) for g, lag in imp_lags.items()}}
        elif t_follow is not None:   # recovery, slowed
            k[0] = t_follow + (k[0] - t_follow) * CHARGE_RECOVER; k[3] = {g: v * CHARGE_RECOVER for g, v in k[3].items()}
        strike.append(tuple(k))
    return keys[:2] + rear + hold + strike, (t_load, t_coil, t_out, shift)

# The charged strike is an overhand: the same arc as the tap punch, but much bigger - the fist rises well above the
# shoulder with the elbow high behind it, then comes down onto the target and carries on down and across.
P_OVER_C = {'loop_r': [-9, 0, 28], 'elbow_pole_r': [-0.4, -0.9, 0.55]}
J_OVER_C = {'loop_l': [6, 0, 24], 'elbow_pole_l': [0.5, -0.55, -0.08]}
# Right: hips and shoulders coil back, weight eases onto the rear leg, the right shoulder and fist draw back.
P_COIL = with_(P_LOAD, pelvis_off=[-2.5, -5, -6], pelvis_rot=[23, 0, 0], spine_lo=[7, -1.5, 0], spine_hi=[11, -2.5, -1],
               clav_r=[8, 0, 4], hand_rel_r=[1], hand_off_r=[-12, 20, -2], foot_rot_r=[-2, -3, 0])
# Power drive: the torso sways forward into the punch and the right shoulder rolls forward and in along its path,
# so the chest and shoulder push the fist through; the hips carry further forward, the lead knee takes the weight
# and the rear leg drives long behind it.
P_IMPACT_C = with_(P_IMPACT, pelvis_off=[6, 17, -7.2], pelvis_rot=[-33, -9, -3], spine_lo=[-17, -13, 6], spine_hi=[-25, -17, 12],
                   clav_r=[-38, 0, 15], hand_r=[-2, 112, 134], elbow_pole_r=[0.05, -0.85, 0.55], reach_r=[0.997], fist_roll_r=[-50],
                   foot_rot_r=[-35, -36, 0])
P_FOLLOW_C = with_(P_IMPACT_C, pelvis_off=[6.4, 18, -7.5], pelvis_rot=[-37, -9, -3], spine_lo=[-19, -13 - 1, 6], spine_hi=[-29, -17 - 2, 13],
                   hand_r=[2, 112 + 4, 122], reach_r=[0.999])
# Left: the hips and chest open, weight eases back, the left shoulder and fist draw back; rear hand stays on guard.
J_COIL = with_(J_LOAD, pelvis_off=[-3, -5.5, -5.5], pelvis_rot=[-8, 0, 0], spine_lo=[-13, -1.5, 0], spine_hi=[-16, -2, 0],
               clav_l=[-8, 0, -4], hand_rel_l=[1], hand_off_l=[3, 20, 2], foot_rot_r=[0, -3, 0])
# Power drive, as on the right: torso sways forward, the left shoulder rolls forward and in along the punch.
J_IMPACT_C = with_(J_IMPACT, pelvis_off=[1.5, 13, -6], pelvis_rot=[36, -8, 1], spine_lo=[15, -12, -1.5], spine_hi=[19, -16, -5],
                   clav_l=[30, 0, -12], hand_l=[-22, 114, 126], elbow_pole_l=[0.3, -0.7, 0.05], reach_l=[0.998], fist_roll_l=[10])
J_FOLLOW_C = with_(J_IMPACT_C, pelvis_off=[2, 14, -6.3], pelvis_rot=[40, -8, 1], spine_lo=[17, -12 - 1, -1.5], spine_hi=[22, -16 - 2, -5],
                   hand_l=[-22 - 5, 116, 118], reach_l=[0.999])
# (right: the chest and shoulder snap in - easing them lets the arm roll over at the top of the overhand)
# Charged strike paths, authored like the tap punches' but from the coil: the cocked fist (by the cheek, drawn back
# with its shoulder) rises well above the shoulder as the hips release, tops the arc about two thirds of the way out,
# and comes down onto the target - the big overhand. The stretch key holds the coil's stored chest twist while the hips
# start (more separation than the tap punch: this is where the extra power comes from), then the chest whips through.
P_ARC_A_C = {'hand_r': [-20, 36, 154], 'hand_rel_r': [0.6], 'hand_off_r': [-4, 24, 17], 'elbow_pole_r': [-0.6, -0.9, -0.2]}
P_LOOP_C = {'hand_r': [-11, 70, 159], 'hand_rel_r': [0], 'elbow_pole_r': [-0.4, -0.9, 0.55]}
P_STRETCH_C = {'spine_lo': [12, -3, 2], 'spine_hi': [16, -4, 3]}
J_ARC_A_C = {'hand_l': [21, 44, 152], 'hand_rel_l': [0.6], 'hand_off_l': [4, 24, 17], 'elbow_pole_l': [0.6, -0.5, -0.6]}
J_LOOP_C = {'hand_l': [9, 74, 158], 'hand_rel_l': [0], 'elbow_pole_l': [0.5, -0.55, -0.08]}
J_STRETCH_C = {'spine_lo': [-15, -2, 0], 'spine_hi': [-19, -3, -1]}
def make_punch_charged():
    return charged_keys(PUNCH, 'r', P_COIL, {id(P_ARC_A): P_ARC_A_C, id(P_LOOP): P_LOOP_C, id(P_STRETCH): P_STRETCH_C,
                                            id(P_IMPACT): P_IMPACT_C, id(P_FOLLOW): P_FOLLOW_C},
                        BODY, snap=('torso_hi', 'shoulder_r'), roles=(P_IMPACT, P_FOLLOW))
def make_jab_charged():
    return charged_keys(JAB, 'l', J_COIL, {id(J_ARC_A): J_ARC_A_C, id(J_LOOP): J_LOOP_C, id(J_STRETCH): J_STRETCH_C,
                                          id(J_IMPACT): J_IMPACT_C, id(J_FOLLOW): J_FOLLOW_C},
                        BODY_L, roles=(J_IMPACT, J_FOLLOW))
PUNCH_CHARGED, PUNCH_CHARGED_T = make_punch_charged()
JAB_CHARGED, JAB_CHARGED_T = make_jab_charged()


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
# (built on the tap kicks' impacts: a little more hip drive and counter-lean, the support leg giving ~1 cm, the weight
# committing a touch further onto the kick; the balance solver still holds the base)
RK_IMPACT_P = with_(RK_IMPACT, pelvis_off=[3, 21, 1.5], pelvis_rot=[-14, 26, -8], spine_lo=[0, 9, 0], spine_hi=[0, 7, 1],
                    foot_r=[1, 110, 108], head_rot=[-4, -12, 0], balance=[0.85])
RK_FOLLOW_P = with_(RK_IMPACT_P, foot_r=[1, 116, 110], pelvis_off=[3.5, 23, 1.7], pelvis_rot=[-15, 27, -8])
LK_IMPACT_P = with_(LK_IMPACT, pelvis_off=[-1, 17, 1.5], pelvis_rot=[8, 25, 8], spine_lo=[0, 12, 1], spine_hi=[0, 10, 1],
                    foot_l=[1, 100, 107], head_rot=[-4, -12, 0], hand_r=[-19, 44, 140], balance=[0.55])
LK_FOLLOW_P = with_(LK_IMPACT_P, foot_l=[0, 105, 109], pelvis_off=[-0.5, 18.5, 1.7], pelvis_rot=[8, 26, 8])
def make_kick_charged_r():
    return kick_charged(RIGHT_KICK, 0.1167, power={id(RK_IMPACT): RK_IMPACT_P, id(RK_FOLLOW): RK_FOLLOW_P})
def make_kick_charged_l():
    return kick_charged(LEFT_KICK, 0.2333, raised='l', power={id(LK_IMPACT): LK_IMPACT_P, id(LK_FOLLOW): LK_FOLLOW_P})
RIGHT_KICK_CHARGED, RIGHT_KICK_CHARGED_T = make_kick_charged_r()
LEFT_KICK_CHARGED, LEFT_KICK_CHARGED_T = make_kick_charged_l()

# Secondary motion (protoanim.sample_track): the head, upper torso and the hand(s) not striking trail the motion a little
# and settle with a touch of overshoot. (hz, damping): lower hz = more lag. Never on the striking limb (its hit timing
# and reach are exact). Pins are hold-loop seams and the kick hold frame, where a pose must repeat exactly.
SPRING_HEAD, SPRING_CHEST, SPRING_GUARD = (5.0, 0.7), (8.0, 0.8), (6.0, 0.65)
PUNCH_SPRINGS = {'head': SPRING_HEAD, 'torso_hi': SPRING_CHEST, 'arm_l': SPRING_GUARD}
JAB_SPRINGS = {'head': SPRING_HEAD, 'torso_hi': SPRING_CHEST, 'arm_r': SPRING_GUARD}
KICK_SPRINGS = {'head': SPRING_HEAD, 'torso_hi': SPRING_CHEST, 'arm_l': SPRING_GUARD, 'arm_r': SPRING_GUARD}

# name: keys, impact time, springs, pins
SPECS = {
    'RPG_Proto_Punch_R': (PUNCH, 0.275 / PUNCH_SPEED, PUNCH_SPRINGS, ()),
    'RPG_Proto_FrontKick_R': (RIGHT_KICK, 0.40, KICK_SPRINGS, (RIGHT_KICK_CHARGED_T[0],)),
    'RPG_Proto_FrontKick_L': (LEFT_KICK, 0.33, KICK_SPRINGS, (LEFT_KICK_CHARGED_T[0],)),
    'RPG_Proto_Punch_L': (JAB, 0.235 / PUNCH_SPEED, JAB_SPRINGS, ()),
    'RPG_Proto_Punch_R_Charged': (PUNCH_CHARGED, 0.275 / PUNCH_SPEED + PUNCH_CHARGED_T[3], PUNCH_SPRINGS, PUNCH_CHARGED_T[1:3]),
    'RPG_Proto_Punch_L_Charged': (JAB_CHARGED, 0.235 / PUNCH_SPEED + JAB_CHARGED_T[3], JAB_SPRINGS, JAB_CHARGED_T[1:3]),
    'RPG_Proto_FrontKick_R_Charged': (RIGHT_KICK_CHARGED, 0.40 + RIGHT_KICK_CHARGED_T[2], KICK_SPRINGS, RIGHT_KICK_CHARGED_T[:2]),
    'RPG_Proto_FrontKick_L_Charged': (LEFT_KICK_CHARGED, 0.33 + LEFT_KICK_CHARGED_T[2], KICK_SPRINGS, LEFT_KICK_CHARGED_T[:2]),
    'RPG_Proto_UnarmedGuard': (UNARMED_GUARD, None, None, ()),
    'RPG_Proto_CombatStance': (COMBAT_STANCE, None, None, ()),
}
CLIPS = [(name, spec[0], spec[1]) for name, spec in SPECS.items()]

def bake_spec(name):
    keys, impact, springs, pins = SPECS[name]
    clip = bake(name, keys, GROUPS, springs, pins); clip['impact'] = impact
    clip['keys'] = [k[0] for k in keys]
    return clip

if __name__ == '__main__':
    # python poses.py [name-filter ...]: re-bake only matching clips, keeping the rest of clips.json
    import sys
    want = sys.argv[1:]
    path = os.path.join(HERE, 'clips.json')
    out = json.load(open(path)) if want and os.path.exists(path) else {}
    for name in SPECS:
        if want and not any(w in name for w in want): continue
        clip = bake_spec(name); out[name] = clip
        print(name, clip['frames'], 'frames', round((clip['frames'] - 1) / FPS, 3), 's')
    json.dump(out, open(path, 'w'))
