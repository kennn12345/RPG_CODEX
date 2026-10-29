"""Knee-down catches (Docs/HIT_OUTCOMES.md, stage 3), hand-keyed for the UEFN mannequin and built into Unreal assets by
ue_import.py (RPG_ROOT_MOTION=1). Knocked off balance, the body stumbles a step, drops onto its right knee with the left
foot forward and a hand braced on the front knee, the other arm out for balance; then it either rises back to GASP's
idle (Recover) or keeps going over and reaches for the ground (Fall: the clip ends there and the game hands the body to
the braced ragdoll).
  RPG_Catch_KneeFwd_Recover / _Fall   falling forward (pushed from behind): stumble step forward with the left foot
  RPG_Catch_KneeBack_Recover / _Fall  falling backward (pushed from the front): step back with the right foot
Run (UE's Python):  RPG_SKEL=uefn python kneecatch.py [name-filter]   -> clips_uefn.json (merged), stick sheets in
previews/, then the realism gate (realism.py) on every clip: anything it finds must be fixed, never waived.

Targets are UEFN component space (cm; +Y forward, +X character-left, +Z up), authored with the body standing at the
origin; the body travels and the root follows the hips (root motion), so the planted feet never slide.
"""
import json, math, os, sys
import protoanim as pa
from protoanim import with_, sample_track, build_pose, FPS
import getups as G
from getups import pose, IDLE_PELVIS, TUCK

IP = IDLE_PELVIS
IDLE_FOOT_L, IDLE_FOOT_R = pa.IDLE_CS['foot_l'][1], pa.IDLE_CS['foot_r'][1]
IDLE_HAND_L, IDLE_HAND_R = pa.IDLE_CS['hand_l'][1], pa.IDLE_CS['hand_r'][1]

def at(v, dy, dx=0.0, dz=0.0): return [v[0] + dx, v[1] + dy, v[2] + dz]

def ball_r(p, dy=0.0, dx=0.0):
    """The right foot planted on its ball, dy / dx from where the idle's ball stands: the ankle goes where the foot,
    turned as foot_rot / foot_turn say, puts it with its ball on that spot. The ball stays put while the foot turns
    about it, so the toes never drag. (Placed here rather than with build_pose's pivot, which would also aim the knee
    over the toes: with the toes tucked under that is backwards, and the knee flips.)"""
    q = {k: list(v) for k, v in p.items()}
    ball = pa.add(pa.IDLE_CS['ball_r'][1], [dx, dy, 0.0])
    R = pa.qmul(pa.qeuler(*q['foot_rot_r']), pa.qaxis([0, 0, 1], q['foot_turn_r'][0] + pa.FOOT_BASE_TURN['r']))
    q['foot_r'] = G.unmap_foot('r', pa.add(ball, pa.qrot(R, pa.sub(pa.IDLE_CS['foot_r'][1], pa.IDLE_CS['ball_r'][1]))))
    return q
LIFT_R = {}      # (the key where the right foot leaves the floor: nothing to undo)

def lift_off(p):
    """The back foot leaving the floor: up off its ball (and its toes swinging under) before it travels anywhere."""
    f = p['foot_r']
    return {'foot_r': [f[0], f[1] + 6.0, f[2] + 9.0], 'foot_rot_r': [0, -55, 0], 'knee_pole_r': [0.1, 1, -0.1]}

def pin_ball_r(p, dy=0.0, dx=0.0):
    """ball_r on one sampled frame: the ankle placed for that frame's foot turn, so the ball can't drift between keys."""
    q = ball_r(p, dy, dx)
    p['foot_r'] = q['foot_r']
    return p

def neutral(dy):
    """GASP's idle, standing dy further forward (where the clip ends after travelling)."""
    p = pose(at(IP, dy), [0, 0, 0], [0, 0, 0], [0, 0, 0], [0, 0, 0], (at(IDLE_HAND_L, dy), at(IDLE_HAND_R, dy)),
             (at(IDLE_FOOT_L, dy), at(IDLE_FOOT_R, dy)), ([0.4, -0.3, -1], [-0.4, -0.3, -1]), KN_IDLE, turn=(0, 0))
    p['square'] = [0]
    return p

def _toe(side):   # the idle foot's toe direction on the ground: a standing knee tracks over it
    d = pa.sub(pa.IDLE_CS['ball_' + side][1], pa.IDLE_CS['foot_' + side][1]); return pa.norm([d[0], d[1], 0.0])
KN_IDLE = (_toe('l'), _toe('r'))

# Poles: elbows point back / down / out (never forward: a forward elbow pole inverts the elbow), knees over the toes.
EL_DOWN = ([0.5, -0.4, -0.8], [-0.5, -0.4, -0.8])
EL_OUT = ([0.8, -0.5, -0.3], [-0.8, -0.5, -0.3])
KN_FWD = ([0.12, 1, 0], [-0.12, 1, 0])
KN_KNEEL = ([0.12, 1, 0.1], [-0.05, 1, -0.4])        # the rear (right) knee pointing down / forward to the floor
KN_KNEEL_R = (KN_KNEEL[0], [0.15, 1, -0.4])          # kneeling: the back knee over its tucked foot (measured: < 1 deg off)
HAND_ON_KNEE = dict(flat=(0.6, 0), dirs=([-0.3, 1, 0], [0, 1, 0]), open_=(1.4, 1.1))

def kneel(dy, pitch=-24, spine=-10, head=-14, hand_r=None, pelvis_dz=0.0, rear_dy=0.0):
    """Down on the right knee, left foot forward, left hand on the left knee, right arm out for balance. dy: how far
    forward of the standing spot the pelvis is."""
    return pose(at([-3, -7, 48], dy, dz=pelvis_dz), [0, pitch, 0], [0, spine, 0], [0, spine, 0], [0, head, 0],
                (at([23, 24, 60 + pelvis_dz], dy), hand_r or at([-40, 18, 74], dy)),
                (at([20, 9, 9.4], dy), at([-11, -49, 16.5], dy + rear_dy)), ([1, -0.6, 0.3], [-0.7, -0.5, -0.4]), KN_KNEEL_R,
                foot_rot=([0, 0, 0], TUCK), turn=(0, G.TOES_AHEAD[1]), **HAND_ON_KNEE)

def rise(dy, rear_dy=0.0):
    """Pushing up off the knee: the knee lifts and the hips come up and forward only as far as the back foot, still on
    its ball where it knelt, can reach (it steps up under them next)."""
    return pose(at([1, -9, 61], dy), [0, -26, 0], [0, -6, 0], [0, -6, 0], [0, -12, 0], (at([21, 20, 68], dy), at([-27, 14, 74], dy)),
                (at([20, 9, 9.4], dy), at([-13, -44, 17.5], dy + rear_dy)), ([0.8, -0.5, -0.4], [-0.6, -0.4, -0.7]), ([0.1, 1, 0], [0.15, 1, -0.1]),
                foot_rot=([0, 0, 0], [0, -48, 0]), open_=(1.2, 1.1), turn=(0, G.TOES_AHEAD[1]), flat=(0.4, 0), dirs=([-0.3, 1, 0], [0, 1, 0]))

# ---------------- the clips: three strengths each (Soft / Mid / Hard: a shorter, slower stumble or a longer, faster
# one; the game picks by the force of the hit plus how fast the body was already moving that way) ----------------
BACK = 3.5           # backward: the kneel's pelvis (the front foot stays planted where it stood; the right steps back)
# step: how far the kneel's hips end up forward (the left foot lands at 9 + step); the right foot stays where it stood, so
# a longer step brings the hips over the kneeling knee. drop: the hips' height so that knee rests on the floor.
STRENGTHS = {'Soft': dict(step=44.0, drop=-0.8, depth=8.0, tempo=1.12), 'Mid': dict(step=50.0, drop=-2.0, depth=0.0, tempo=1.0),
             'Hard': dict(step=56.0, drop=-3.0, depth=-6.0, tempo=0.92)}

def fwd(step, tempo, fall, drop=-5.0):
    """Falling forward: lurch, stumble step with the left foot (to 9 + step), right knee down where the right foot
    stood (it never slides), left hand braced on the left knee, right arm out; then rise into the idle, or tip over."""
    k = step / 30.0
    rear = 30.0 - step                      # keeps the kneeling right foot on its idle spot
    T = lambda t: t * tempo                 # the stumble's timing
    lurch = pose(at(IP, 6 * k, dz=-3), [0, -8, 0], [0, -8, 0], [0, -8, 0], [0, 6, 0],
                 ([30, 8, 88], [-30, 6, 87]), (IDLE_FOOT_L, IDLE_FOOT_R), EL_DOWN, KN_IDLE, turn=(0, 0))
    stepping = ball_r(pose(at(IP, 17 * k, dz=-10), [0, -14, 0], [0, -10, 0], [0, -10, 0], [0, 4, 0],
                    ([33, 30 * k, 92], [-34, 24 * k, 90]), ([19, 12.5 + 18 * k, 22], at(IDLE_FOOT_R, 0, dz=4)), EL_OUT,
                    (KN_FWD[0], KN_IDLE[1]), foot_rot=([0, 6, 0], [0, -22, 0]), turn=(G.TOES_AHEAD[0], 0)))
    plant = ball_r(pose(at(IP, 24 * k, dz=-26), [0, -20, 0], [0, -12, 0], [0, -12, 0], [0, -4, 0],
                 ([24, 8 + step, 74], [-36, 16 + step, 84]), (at([20, 9, 9.4], step), at(IDLE_FOOT_R, 2, dz=8)), EL_OUT, KN_KNEEL,
                 foot_rot=([0, 0, 0], [0, -55, 0]), turn=(0, G.TOES_AHEAD[1])))   # heel up, it pivots on the ball
    # (falling, the balance arm is already on its way to the ground as the knee lands: no pause)
    knee = ball_r(kneel(step, pitch=-28, spine=-13, head=-16, pelvis_dz=drop - 1.5, rear_dy=rear,
                        hand_r=at([-34, 30, 58], step) if fall else None))
    keys = [(0.00, neutral(0), 'smooth', {}),
            (T(0.14), lurch, 'flow', {'arm_l': -0.03, 'arm_r': -0.02}),
            (T(0.30), stepping, 'flow', {'head': 0.03, 'arm_r': 0.02}),
            (T(0.46), plant, 'flow', {'leg_r': 0.04, 'torso': 0.03, 'arm_r': 0.03}),
            (T(0.66), knee, 'flow' if fall else 'out', {'arm_r': 0.06, 'head': 0.05, 'torso': 0.03})]
    t_knee = T(0.66)
    if fall:
        tip = ball_r(pose(at([-3, -2, 46], step), [0, -37, 0], [0, -20, 0], [0, -20, 0], [0, -40, 0],
                   (at([30, 46, 30], step), at([-30, 42, 32], step)), (at([20, 9, 9.4], step), at([-11, -46, 17.5], step + rear)),
                   EL_OUT, KN_KNEEL, foot_rot=([0, -8, 0], TUCK), turn=(0, G.TOES_AHEAD[1]), flat=(0.5, 0.5),
                   dirs=([-0.2, 1, 0], [0.2, 1, 0]), open_=(1.6, 1.6)))
        keys.append((t_knee + 0.39, tip, 'flow', {'arm_l': -0.06, 'arm_r': -0.02}))
        return keys, dict(knee=t_knee, contacts=[0.0, T(0.16), T(0.46), t_knee], pins=[(0.0, 99.0, 0.0, 0.0)])
    absorb = {k: v for k, v in ball_r(kneel(step, pitch=-22, spine=-7, head=-8, rear_dy=rear, pelvis_dz=drop)).items()
              if k not in ('hand_r', 'elbow_pole_r', 'clav_r')}
    up = ball_r(rise(step, rear))
    t_rise = t_knee + 1.14
    keys += [(t_knee + 0.39, absorb, 'flow', {}),
             (t_rise, up, 'flow', {'head': 0.04, 'arm_r': 0.04}),
             (t_rise + 0.10, lift_off(up), 'flow', {}),
             (t_rise + 0.34, dict(LIFT_R, foot_r=G.unmap_foot('r', at([-14, -10, 18], step)), foot_rot_r=[0, -4, 0], knee_pole_r=[-0.35, 0.94, 0]), 'flow', {}),
             (t_rise + 0.62, neutral(step - 3.5), 'smooth', {'arm_l': 0.04, 'arm_r': 0.06, 'head': 0.04})]
    return keys, dict(knee=t_knee, absorb=t_knee + 0.39, rise=t_rise,
                      contacts=[0.0, T(0.16), T(0.46), t_knee, t_rise, t_rise + 0.10, t_rise + 0.34], pins=[(0.0, t_rise, 0.0, 0.0)])

def back(depth, tempo, fall):
    """Falling backward: recoil, the right foot steps back (deeper for a harder push), right knee down behind, the left
    foot stays planted; then rise into the idle, or sit back off the knee, hands reaching behind."""
    # (a leg only swings back so fast: a harder push steps deeper, it doesn't swing the leg faster than this)
    T = lambda t: t * max(tempo, 0.98)
    recoil = pose(at(IP, -5, dz=-2), [0, 6, 0], [0, 6, 0], [0, 6, 0], [0, -4, 0],
                  ([28, 12, 94], [-28, 10, 94]), (IDLE_FOOT_L, IDLE_FOOT_R), EL_DOWN, KN_IDLE, turn=(0, 0))
    stepping = pose(at(IP, -15, dz=-9), [0, 0, 0], [0, -2, 0], [0, -2, 0], [0, -6, 0],
                    ([30, 28, 100], [-30, 26, 100]), (IDLE_FOOT_L, at([-13, -51, 26], depth)), EL_DOWN, (KN_IDLE[0], KN_FWD[1]),
                    foot_rot=([0, 0, 0], [0, -48, 0]), turn=(0, G.TOES_AHEAD[1]))
    BALL = -40.0 + depth            # where the right ball lands, from the idle's
    plant = ball_r(pose(at(IP, -20 + depth * 0.4, dz=-24), [0, -12, 0], [0, -8, 0], [0, -8, 0], [0, -8, 0],
                 ([28, 28, 88], [-30, 26, 86]), (IDLE_FOOT_L, at([-11, -50, 17.5], BACK + depth)), EL_DOWN, KN_KNEEL,
                 foot_rot=([0, 0, 0], [0, -60, 0]), turn=(0, G.TOES_AHEAD[1])), BALL, 10.0)
    knee = ball_r(kneel(BACK, pitch=-26, spine=-12, head=-10, pelvis_dz=-1.5,
                        hand_r=at([-36, 28, 60], BACK) if fall else at([-38, 22, 72], BACK), rear_dy=depth), BALL, 10.0)
    keys = [(0.00, neutral(0), 'smooth', {}),
            # (the recoil doesn't hold the right leg: it goes straight from standing into lifting off)
            (T(0.14), {k: v for k, v in recoil.items() if k not in G.GROUPS['leg_r'] + G.GROUPS['arm_r']}, 'flow', {}),
            # the right foot comes up off the floor before it travels back (no toes scraping along the ground)
            (T(0.21), {'foot_r': G.unmap_foot('r', at(IDLE_FOOT_R, -8 + depth * 0.5, dz=8)), 'foot_rot_r': [0, -18, 0]}, 'flow', {}),
            (T(0.37), stepping, 'flow', {'arm_l': 0.02}),
            (T(0.50), plant, 'flow', {'torso': 0.03, 'arm_r': 0.03}),
            (T(0.70), knee, 'flow' if fall else 'out', {'arm_r': 0.06, 'head': 0.05})]
    t_knee = T(0.70)
    if fall:
        # (elbows aimed back and out: a hanging arm's elbow never points forward)
        sit = ball_r(pose(at([-3, -14 + min(depth, 0.0), 46], BACK), [0, 22, 0], [0, 10, 0], [0, 8, 0], [0, 6, 0],
                   (at([31, -28, 37], BACK), at([-31, -27, 38], BACK)), (at([20, 9, 9.4], BACK), at([-11, -49, 16.5], BACK + depth)),
                   ([0.55, -0.8, 0.1], [-0.55, -0.8, 0.1]), KN_KNEEL, foot_rot=([0, 0, 0], TUCK), turn=(0, G.TOES_AHEAD[1]), open_=(1.5, 1.5)), BALL, 10.0)
        keys.append((t_knee + 0.40, sit, 'flow', {'arm_l': -0.04, 'arm_r': -0.04}))
        return keys, dict(knee=t_knee, contacts=[0.0, T(0.18), T(0.50), t_knee],
                          pins=[(0.0, T(0.05), 0.0, 0.0), (T(0.50), 99.0, BALL, 10.0)])
    absorb = {k: v for k, v in ball_r(kneel(BACK, pitch=-22, spine=-7, head=-8, rear_dy=depth, pelvis_dz=0.0), BALL, 10.0).items()
              if k not in ('hand_r', 'elbow_pole_r', 'clav_r')}
    up = ball_r(rise(BACK, depth), BALL, 10.0)
    t_rise = t_knee + 1.15
    keys += [(t_knee + 0.40, absorb, 'flow', {}),
             (t_rise, up, 'flow', {'head': 0.04, 'arm_r': 0.04}),
             (t_rise + 0.10, lift_off(up), 'flow', {}),
             (t_rise + 0.34, dict(LIFT_R, foot_r=G.unmap_foot('r', at([-14, -10, 18], BACK)), foot_rot_r=[0, -4, 0], knee_pole_r=[-0.35, 0.94, 0]), 'flow', {}),
             (t_rise + 0.62, neutral(BACK - 3.5), 'smooth', {'arm_l': 0.04, 'arm_r': 0.06, 'head': 0.04})]
    return keys, dict(knee=t_knee, absorb=t_knee + 0.40, rise=t_rise,
                      contacts=[0.0, T(0.18), T(0.50), t_knee, t_rise, t_rise + 0.10, t_rise + 0.34],
                      pins=[(0.0, T(0.05), 0.0, 0.0), (T(0.50), t_rise, BALL, 10.0)])

SPECS = {}
for strength, v in STRENGTHS.items():
    for fall in (False, True):
        outcome = 'Fall' if fall else 'Recover'
        SPECS['RPG_Catch_KneeFwd_%s_%s' % (outcome, strength)] = fwd(v['step'], v['tempo'], fall, v['drop']) + (not fall,)
        SPECS['RPG_Catch_KneeBack_%s_%s' % (outcome, strength)] = back(v['depth'], v['tempo'], fall) + (not fall,)

# ---------------- what makes it read as a body, not poses: layered on every clip ----------------
SPRINGS = dict(G.SPRINGS, head=(3.4, 0.75), torso=(6.0, 1.0))   # the head and chest lag the hips, overshoot a little, settle

def _window(t, a, b, fade):
    """1 inside [a, b], easing in / out over `fade` either side."""
    if t <= a - fade or t >= b + fade: return 0.0
    u = min(1.0, (t - (a - fade)) / fade) if t < a else 1.0
    w = min(1.0, ((b + fade) - t) / fade) if t > b else 1.0
    s = lambda x: x * x * (3 - 2 * x)
    return s(u) * s(w)

def layers(p, t, info):
    """Secondary motion on one sampled frame (params): the knee landing compresses the hips; a decaying balance
    wobble on the knee; heavy breathing while it gathers itself; an effort tremble in the bracing hand as it pushes."""
    p = {k: list(v) for k, v in p.items()}
    for a, b, dy, dx in info.get('pins', []):
        if a <= t <= b: pin_ball_r(p, dy, dx)
    tk = info['knee']; tau = t - tk
    if tau <= 0: return p
    # landing: the hips sink on past the knee's contact and come back up (a damped dip, ~0.3 s)
    dip = -2.0 * math.sin(math.pi * min(1.0, tau / 0.32)) * math.exp(-tau / 0.22)
    end = info.get('rise', tk + 0.6)
    w = _window(t, tk, end - 0.25, 0.25)
    # balance wobble: side to side and front to back, out of step, dying away as it steadies
    sway = math.exp(-tau / 0.55)
    dx = 1.7 * math.sin(2 * math.pi * 1.55 * tau) * sway
    dy = 1.0 * math.sin(2 * math.pi * 1.2 * tau + 0.9) * sway
    roll = 2.4 * math.sin(2 * math.pi * 1.55 * tau + 0.5) * sway
    pitch = 1.2 * math.sin(2 * math.pi * 1.2 * tau + 1.4) * sway
    # breathing: big slow breaths once the wobble has settled
    breath = 1.5 * math.sin(2 * math.pi * 0.95 * tau) * _window(t, tk + 0.25, end - 0.2, 0.25)
    po = p['pelvis_off']; p['pelvis_off'] = [po[0] + dx * w, po[1] + dy * w, po[2] + dip]
    sh = p['spine_hi']; p['spine_hi'] = [sh[0], sh[1] + pitch * w + breath, sh[2] + roll * w]
    sl = p['spine_lo']; p['spine_lo'] = [sl[0], sl[1], sl[2] - roll * 0.5 * w]
    if 'rise' in info:
        # the bracing (left) hand trembles with effort, most as it pushes off the knee
        push = _window(t, info['rise'] - 0.45, info['rise'] - 0.05, 0.2)
        hold = 0.35 * _window(t, tk + 0.2, info['rise'] - 0.45, 0.2)
        amp = 0.18 * (push + hold)
        if amp > 0:
            hl = p['hand_l']
            p['hand_l'] = [hl[0], hl[1] + amp * math.sin(2 * math.pi * 8.7 * t + 0.3),
                           hl[2] + amp * (math.sin(2 * math.pi * 11.3 * t) * 0.7 + math.sin(2 * math.pi * 6.1 * t + 2) * 0.3)]
    return p

def warp(times, info):
    """The rise is not one lift: push, a hesitation (it slows to under half speed, never stops), push again."""
    if 'absorb' not in info: return times
    a, b = info['absorb'], info['rise']
    out = []
    for t in times:
        if a < t < b:
            u = (t - a) / (b - a)
            t = a + (b - a) * (u - 0.42 * math.sin(4 * math.pi * u) / (4 * math.pi))
        out.append(t)
    return out

ROOT_CHILDREN = [b for b in pa.BONES if pa.PARENT[b] == 'root']
def bake(name, keys, info, to_idle):
    """Samples the keys (the rise time-warped), springs and secondary layers on top; the root follows the hips over the
    ground (root motion), everything else keeps its place."""
    T = keys[-1][0]; end = T + (G.END_BLEND if to_idle else 0.0)
    n = int(round(end * FPS)) + 1; n += (-(n - 1)) % (FPS // 30)
    times = [i / FPS for i in range(n)]
    track = sample_track(keys, warp(times, info), G.GROUPS, SPRINGS)
    track = [layers(q, t, info) for q, t in zip(track, times)]
    locs = [build_pose(q) for q in track]
    # the root under the hips (forward / back only), eased so it never lurches; ends exactly under the final hips
    ys = [pa.to_cs(l)['pelvis'][1][1] - IP[1] for l in locs]
    final = ys[-1]
    root = []
    r = 0.0
    for i, y in enumerate(ys):
        r += (y - r) * min(1.0, 9.0 / FPS)      # a 9/s follow: smooth, a little behind the hips
        root.append(r)
    blend = max(1, int(0.25 * FPS))
    for i in range(n - blend, n):               # settle exactly on the end
        k = (i - (n - blend)) / blend; k = k * k * (3 - 2 * k)
        root[i] = root[i] + (final - root[i]) * k
    frames = []
    for t, local, ry in zip(times, locs, root):
        if to_idle:
            k = min(max((t - T) / G.END_BLEND, 0.0), 1.0); k = k * k * (3 - 2 * k)
            if k > 0:
                # blend toward the idle standing over the (moved) root
                idle = {b: (q, list(tr)) for b, (q, tr) in pa.IDLE.items()}
                for c in ROOT_CHILDREN: idle[c] = (idle[c][0], pa.add(idle[c][1], [0.0, final, 0.0]))
                local = {b: (pa._nlerp(q, idle[b][0], k), pa.lerp(tr, idle[b][1], k)) for b, (q, tr) in local.items()}
        local = dict(local)
        local['root'] = ([0.0, 0.0, 0.0, 1.0], [0.0, ry, 0.0])
        for c in ROOT_CHILDREN:
            q, tr = local[c]; local[c] = (q, [tr[0], tr[1] - ry, tr[2]])
        frames.append(local)
    return {'name': name, 'fps': FPS, 'frames': n, 'impact': None, 'bones': pa.BONES, 'poses': frames,
            'keys': [k[0] for k in keys] + ([end] if to_idle else []), 'root_motion': True, 'travel': final}

if __name__ == '__main__':
    from preview import render
    import realism
    want = sys.argv[1:]
    path = os.environ.get('RPG_CLIP_FILE', os.path.join(pa.HERE, 'kneecatch_uefn.json'))
    out = json.load(open(path)) if os.path.exists(path) else {}
    fails = 0
    for old in [k for k in out if k.startswith('RPG_Catch_') and k not in SPECS]: del out[old]   # retired names
    for name, (keys, info, to_idle) in SPECS.items():
        if want and not any(w in name for w in want): continue
        clip = bake(name, keys, info, to_idle); clip['contacts'] = info['contacts']; out[name] = clip
        G.report(name, clip)
        render(clip, os.path.join(pa.HERE, 'previews', name + '.png'), clip['keys'])
        fails += realism.gate(name, clip)
    json.dump(out, open(path, 'w'))
    print('REALISM', 'PASS' if fails == 0 else f'FAIL ({fails} findings)')
