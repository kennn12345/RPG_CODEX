"""First-person versions of the prototype clips (RPG_FP_*), baked from the SAME keys as the third-person clips.

Run after poses.py: it adds the RPG_FP_* clips to clips.json (ue_import.py then imports everything).

In first person the game shows the same body mesh from the eyes (head hidden, see URPGCameraModeComponent), so legs,
hips, torso and timing are untouched. The only change is where the hands sit relative to the eyes: the third-person
guard holds the fists beside the chin, which from the eyes is below / beside the camera, off screen. Each frame the
hand targets get a view-space offset (fp_off_l / fp_off_r) moving the guard out in front of the face, into the lower
part of the screen. The offset is the same for the whole clip, so every path (windup, coil, arc) keeps its shape; it
fades out as a fist extends toward the target, and on the striking arm it is exactly zero inside the hit window, so
the melee sweeps see identical bones (same reach, impact location and timing) in both perspectives.
"""
import json, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import contextlib, io
with contextlib.redirect_stdout(io.StringIO()):
    import poses
from protoanim import build_pose, sample, sample_track, frame_count, add, sub, lerp, qrot, to_cs, HERE, FPS, BONES

# ---- camera model (mirrored by URPGCameraModeComponent; keep the numbers in sync) ----
EYE_LOCAL = [15.0, 13.0, 0.0]  # camera point in the head bone's space (X up, Y forward on Manny): a little above and in front of
                               # the eyes, so the upper chest and shoulders never reach it in the lunges
FOLLOW_H, FOLLOW_V = 0.2, 0.4  # share of the head's horizontal / vertical motion the camera follows

def head_eye(cs): return add(cs['head'][1], qrot(cs['head'][0], EYE_LOCAL))
REST_EYE = head_eye(build_pose(poses.STANCE, return_cs=True))
def camera(cs):
    d = sub(head_eye(cs), REST_EYE)
    return [REST_EYE[0] + FOLLOW_H * d[0], REST_EYE[1] + FOLLOW_H * d[1], REST_EYE[2] + FOLLOW_V * d[2]]

# ---- where the combat-stance fists sit in first person, relative to the camera (component axes: +X character-left,
# +Y forward, +Z up). Lower corners of the view, out from the face; the guard is this plus its usual 7 cm up / closer.
# The rear (right) shoulder sits further back in the bladed stance, so its fist rests a little closer to the face.
FP_STANCE_FIST = {'l': [28.2, 33.0, -13.9], 'r': [-15.1, 20.0, -6.7]}
EXTEND_FADE = (22.0, 55.0)     # fist this far forward of its first-person rest (cm): offset fades from full to none
STRIKE_RAMP = 0.10             # s: the striking arm's offset eases out / back in around the hit window
BACK_KEEP = 0.3                # a fist drawn back behind its rest (the charge rear-back) keeps this share of the
                               # draw-back depth, so the loaded arm stays in view instead of passing behind the camera
KEEP_MAX = 0.75                 # share of the way to its resting screen spot such a fist goes (all the way would put it
                               # right in front of the camera, it is so shallow)
BACK_FULL = 6.0                # cm behind its rest at which a fist fully keeps its resting screen spot (see fp_frame)
GUARD_STEADY = 0.6             # the off (guarding) hand cancels this share of the body's lunge relative to the steadier
                               # camera, so it holds its place in view while the other arm strikes
LENS_CLEAR = 20.0              # cm: a fist's centre never comes nearer the camera than this (outside the hit window), so
                               # it never fills the screen or meets the lens (e.g. the charged right fist leaving its coil)
# The first-person combat stance fists are laid out symmetrically on screen (solved by fpsym.py): the same distance in
# from the left and right edges and at the same height (screen x -/+0.85, y -0.77).
#
# The block (held guard, RPG_FP_UnarmedGuard) has its own first-person layout: each fist out toward its own side of the
# screen, raised into a guard, with the centre of the view left open. FP_GUARD_FIST is where each fist sits relative to
# the camera on the guard's first frame (same axes as FP_STANCE_FIST); the clip's constant offset is solved to put it
# there (_guard_shift), so the guard's breathing loop keeps its shape. Both fists at about the same depth, so they read
# the same size. FP_GUARD_POLE turns the elbows down and out, so each forearm runs from its fist out through its own
# lower corner of the view (the wrist stays straight with the forearm, see protoanim.build_pose). FP_GUARD_CLAV (added
# to the clip's clavicles) brings the rear shoulder up and forward into the block, which a real guard does to cover the
# chin; it is also what lets the rear fist sit far enough out from the eyes (that shoulder starts ~35 cm behind them).
# First person only: the shadow body and the third-person guard keep RPG_Proto_UnarmedGuard.
# Screen position of a camera-relative point (16:9, 90 deg horizontal FOV): x = -X / Y, y = Z / Y / (9/16).
FP_GUARD_FIST = {'l': [27.6, 31.0, -5.2], 'r': [-21.6, 24.0, -4.0]}
FP_GUARD_CLAV = {'r': [-18.0, 0.0, 0.0]}
FP_GUARD_POLE = {'l': [0.9, -0.2, -1.0], 'r': [-0.9, -0.2, -1.0]}

def fist(cs, s): return lerp(cs['hand_' + s][1], cs['middle_01_' + s][1], 0.6)
def _offsets():
    cs = build_pose(poses.STANCE, return_cs=True); cam = camera(cs)
    return {s: sub(add(cam, FP_STANCE_FIST[s]), fist(cs, s)) for s in 'lr'}
OFFSET = _offsets()

def smooth(x): x = min(max(x, 0.0), 1.0); return x * x * (3 - 2 * x)

SWITCH = {'RPG_FP_Punch_R': 0.1 / poses.PUNCH_SPEED, 'RPG_FP_Punch_L': 0.06 / poses.PUNCH_SPEED}   # tap -> Charged switch frames

def strike_weight(t, window, keep_until=0.0):
    """keep_until: before this time the offset is untouched (the switch frame, where the Charged clip must still match
    the tap punch); the fade before the hit window is squeezed into what's left after it."""
    if not window: return 1.0
    a, b = window
    if a <= t <= b: return 0.0
    if t > b: return smooth((t - b) / STRIKE_RAMP)
    start = max(a - STRIKE_RAMP, keep_until)
    return 1.0 if t <= start else smooth((a - t) / (a - start))

def fp_frame(P, t, striking=None, window=None, shift=None, keep_until=0.0, poles=None, clav=None):
    """P: this frame's parameters from the third-person pipeline (keys, springs, balance), so the body moves identically."""
    P = dict(P)
    for s, pole in (poles or {}).items(): P['elbow_pole_' + s] = list(pole)
    for s, c in (clav or {}).items(): P['clav_' + s] = add(P['clav_' + s], c)
    cs = build_pose(P, return_cs=True); cam = camera(cs)
    lunge = sub(head_eye(cs), cam)   # the body's drive the camera doesn't follow
    for s in 'lr':
        # how far this fist has travelled forward of where the offset would put it at rest
        fwd = fist(cs, s)[1] + OFFSET[s][1] - (cam[1] + FP_STANCE_FIST[s][1])
        w = 1.0 - smooth((fwd - EXTEND_FADE[0]) / (EXTEND_FADE[1] - EXTEND_FADE[0]))
        back = max(0.0, -fwd) * (1.0 - BACK_KEEP)
        if s == striking: w *= strike_weight(t, window, keep_until)
        sh = (shift or {}).get(s, [0.0, 0.0, 0.0])
        steady = [0.0, 0.0, 0.0] if s == striking else [-GUARD_STEADY * x for x in lunge]
        P['fp_off_' + s] = [(OFFSET[s][0] + sh[0]) * w + steady[0], (OFFSET[s][1] + sh[1] + back) * w + steady[1], (OFFSET[s][2] + sh[2]) * w + steady[2]]
        P['_w_' + s], P['_back_' + s] = w, smooth(-fwd / BACK_FULL)
    # A fist drawn back behind its rest (the charge load / coil) can't reach further forward, so instead it keeps its
    # resting spot on screen: moved in / up to where its rest direction from the eyes meets its actual depth.
    cs = build_pose(P, return_cs=True); cam = camera(cs)
    for s in 'lr':
        k = P['_w_' + s] * P.pop('_back_' + s) * KEEP_MAX
        if k <= 0.0: continue
        rel = sub(fist(cs, s), cam); rest = FP_STANCE_FIST[s]; depth = max(rel[1], 1.0)
        want = [rest[0] * depth / rest[1], rel[1], rest[2] * depth / rest[1]]
        o = P['fp_off_' + s]
        P['fp_off_' + s] = [o[0] + (want[0] - rel[0]) * k, o[1], o[2] + (want[2] - rel[2]) * k]
    # Lens guard: push a fist that would come too near the camera straight back out along its line from the eyes.
    cs = build_pose(P, return_cs=True); cam = camera(cs)
    for s in 'lr':
        w = P.pop('_w_' + s, 1.0) if s != striking else strike_weight(t, window, keep_until)
        rel = sub(fist(cs, s), cam); d = math.sqrt(sum(x * x for x in rel))
        if d < LENS_CLEAR and d > 1e-3 and w > 0.0:
            push = (LENS_CLEAR - d) / d * w
            P['fp_off_' + s] = [o + r * push for o, r in zip(P['fp_off_' + s], rel)]
    for s in 'lr': P.pop('_w_' + s, None); P.pop('_back_' + s, None)
    return build_pose(P)

def _guard_shift():
    """Constant extra offset for the first-person guard clip that puts its first-frame fists at FP_GUARD_FIST."""
    _, _, springs, pins = poses.SPECS['RPG_Proto_UnarmedGuard']
    P = sample_track(poses.UNARMED_GUARD, [0.0], poses.GROUPS, springs, pins)[0]
    shift = {s: [0.0, 0.0, 0.0] for s in 'lr'}
    for _ in range(10):
        cs = to_cs(fp_frame(P, 0.0, shift=shift, poles=FP_GUARD_POLE, clav=FP_GUARD_CLAV)); cam = camera(cs)
        for s in 'lr':
            err = sub(add(cam, FP_GUARD_FIST[s]), fist(cs, s))
            shift[s] = add(shift[s], err)
    return shift
GUARD_SHIFT = _guard_shift()

def fp_bake(name, keys, impact=None, striking=None, before=0.07, after=0.05, shift=None, springs=None, pins=(), keep_until=0.0, poles=None, clav=None):
    # one extra frame each side: the melee sweep starts from the previous frame's limb position
    window = (impact - before - 1.0 / FPS, impact + after + 1.0 / FPS) if impact is not None and striking else None
    n = frame_count(keys)
    track = sample_track(keys, [i / FPS for i in range(n)], poses.GROUPS, springs, pins)
    frames = [fp_frame(track[i], i / FPS, striking, window, shift, keep_until, poles, clav) for i in range(n)]
    return {'name': name, 'fps': FPS, 'frames': n, 'impact': impact, 'bones': BONES, 'poses': frames, 'keys': [k[0] for k in keys]}

# ---- calmer holds: the third-person charge holds have a faint arm tremor and sway; seen from the eyes, a few cm from the
# camera, it reads as fidgeting. The first-person versions are rebuilt from the same keys with less of it (same times).
FP_HOLD_SWAY, FP_HOLD_TREMOR = 0.5, 0.05      # third person: poses.HOLD_SWAY 1.0, HOLD_TREMOR 0.25
FP_KICK_SWAY, FP_KICK_TREMOR = 0.5, 0.2      # share of the kick hold's hand sway / tremor kept

def _fp_kick_hold_pose(P, u, raised=None):
    # poses.kick_hold_pose with its hand sway and tremor scaled down (the breath and the raised foot's balance stay)
    full = _tp_kick_hold_pose(P, u, raised)
    env = 0.5 - 0.5 * math.cos(2 * math.pi * u)
    sway = [0.8 * math.sin(2 * math.pi * u) * env, 0.0, 0.5 * math.sin(4 * math.pi * u + 1) * env]
    t = [0.15 * math.sin(2 * math.pi * 7 * u) * env, 0.0, 0.15 * math.sin(2 * math.pi * 7 * u + 2) * env]
    d = [sway[i] * (1 - FP_KICK_SWAY) + t[i] * (1 - FP_KICK_TREMOR) for i in range(3)]
    full['hand_l'] = [full['hand_l'][i] - d[i] for i in range(3)]
    full['hand_r'] = [full['hand_r'][i] + d[i] for i in range(3)]
    return full
_tp_kick_hold_pose = poses.kick_hold_pose

def _charged_sets():
    def build():
        pr = poses.make_punch_charged()[0]
        pl = poses.make_jab_charged()[0]
        kr = poses.make_kick_charged_r()[0]
        kl = poses.make_kick_charged_l()[0]
        return pr, pl, kr, kl
    # rebuilding with the third-person settings must give exactly the third-person keys (so only the hold differs)
    same = build()
    for a, b in zip(same, (poses.PUNCH_CHARGED, poses.JAB_CHARGED, poses.RIGHT_KICK_CHARGED, poses.LEFT_KICK_CHARGED)):
        assert [k[0] for k in a] == [k[0] for k in b] and all(x[1] == y[1] for x, y in zip(a, b)), 'charged keys out of sync with poses.py'
    saved = (poses.HOLD_SWAY, poses.HOLD_TREMOR, poses.kick_hold_pose)
    poses.HOLD_SWAY, poses.HOLD_TREMOR, poses.kick_hold_pose = FP_HOLD_SWAY, FP_HOLD_TREMOR, _fp_kick_hold_pose
    try:
        return build()
    finally:
        poses.HOLD_SWAY, poses.HOLD_TREMOR, poses.kick_hold_pose = saved
FP_PUNCH_CHARGED, FP_JAB_CHARGED, FP_RIGHT_KICK_CHARGED, FP_LEFT_KICK_CHARGED = _charged_sets()

P_IMP_R, P_IMP_L = 0.275 / poses.PUNCH_SPEED, 0.235 / poses.PUNCH_SPEED
# (first-person name, keys, impact, striking arm) - punches strike with an arm; kicks only offset the guard hands
FP_CLIPS = [
    ('RPG_FP_Punch_R', poses.PUNCH, P_IMP_R, 'r'),
    ('RPG_FP_Punch_L', poses.JAB, P_IMP_L, 'l'),
    ('RPG_FP_Punch_R_Charged', FP_PUNCH_CHARGED, P_IMP_R + poses.PUNCH_CHARGED_T[3], 'r'),
    ('RPG_FP_Punch_L_Charged', FP_JAB_CHARGED, P_IMP_L + poses.JAB_CHARGED_T[3], 'l'),
    ('RPG_FP_FrontKick_R', poses.RIGHT_KICK, 0.40, None),
    ('RPG_FP_FrontKick_L', poses.LEFT_KICK, 0.33, None),
    ('RPG_FP_FrontKick_R_Charged', FP_RIGHT_KICK_CHARGED, 0.40 + poses.RIGHT_KICK_CHARGED_T[2], None),
    ('RPG_FP_FrontKick_L_Charged', FP_LEFT_KICK_CHARGED, 0.33 + poses.LEFT_KICK_CHARGED_T[2], None),
    ('RPG_FP_UnarmedGuard', poses.UNARMED_GUARD, None, None),
    ('RPG_FP_CombatStance', poses.COMBAT_STANCE, None, None),
]

if __name__ == '__main__':
    path = os.path.join(HERE, 'clips.json')
    clips = json.load(open(path))
    only = set(sys.argv[1:])
    for name, keys, impact, striking in FP_CLIPS:
        if only and name not in only: continue
        _, _, springs, pins = poses.SPECS[name.replace('RPG_FP_', 'RPG_Proto_')]
        guard = name == 'RPG_FP_UnarmedGuard'
        clips[name] = fp_bake(name, keys, impact, striking, shift=GUARD_SHIFT if guard else None, poles=FP_GUARD_POLE if guard else None,
                              clav=FP_GUARD_CLAV if guard else None,
                              springs=springs, pins=pins, keep_until=SWITCH.get(name, 0.0))
        print(name, clips[name]['frames'], 'frames')
    json.dump(clips, open(path, 'w'))
