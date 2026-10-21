"""Builds the game's unarmed attack clips from the retargeted RamsterZ mocap (source_poses.json, from dump_poses.py).

Each attack is cut out of a combo, blended in from the fighting stance and back out to it, and given the structure the
combat code expects (URPGPrototypeAnimationComponent):
  tap clip:      stance -> load key (where a held press waits) -> strike -> recovery -> stance
  charged clip:  identical to the tap up to the load / hold point, then rears back to a coiled pose, a seamless
                 breathing loop [HoldStart, HoldEnd) while the button is held, then the big strike from the coil.
The right kick is mirrored into the left kick. Writes mocap_clips.json (for mocap_import.py) and mocap_timings.json
(the times to put in the component's defaults).

Run with any Python 3 (no Unreal needed):  python compose.py
"""
import json, math, os

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = json.load(open(os.path.join(HERE, 'source_poses.json')))
BONES = SRC['bones']
FPS = 30.0

# ---------------------------------------------------------------- quaternion / transform maths (UE conventions)
# FQuat A*B applies B first, then A. Component = parent_component * local.


def qmul(a, b):
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return [aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz]


def qinv(q):
    return [-q[0], -q[1], -q[2], q[3]]


def qnorm(q):
    n = math.sqrt(sum(c * c for c in q)) or 1.0
    return [c / n for c in q]


def qrot(q, v):
    p = qmul(qmul(q, [v[0], v[1], v[2], 0.0]), qinv(q))
    return p[:3]


def slerp(a, b, s):
    d = sum(x * y for x, y in zip(a, b))
    if d < 0:
        b = [-c for c in b]; d = -d
    if d > 0.9995:
        return qnorm([x + (y - x) * s for x, y in zip(a, b)])
    th = math.acos(d)
    wa, wb = math.sin((1 - s) * th) / math.sin(th), math.sin(s * th) / math.sin(th)
    return [x * wa + y * wb for x, y in zip(a, b)]


def lerp3(a, b, s):
    return [x + (y - x) * s for x, y in zip(a, b)]


def blend(pa, pb, s):
    """Local poses {bone: [q, t]}."""
    if s <= 0: return pa
    if s >= 1: return pb
    return {b: [slerp(pa[b][0], pb[b][0], s), lerp3(pa[b][1], pb[b][1], s)] for b in BONES}


def smooth(x):
    x = max(0.0, min(1.0, x))
    return x * x * (3 - 2 * x)

# ---------------------------------------------------------------- skeleton: parents from the reference pose


REF_L, REF_C = SRC['ref']['local'], SRC['ref']['comp']


def find_parents():
    parents = {}
    for i, b in enumerate(BONES):
        lq, lt = REF_L[b]
        cq, ct = REF_C[b]
        best, err = None, 1e9
        for p in BONES[:i]:
            pq, pt = REF_C[p]
            gt = [x + y for x, y in zip(pt, qrot(pq, lt))]
            gq = qmul(pq, lq)
            e = sum((x - y) ** 2 for x, y in zip(gt, ct)) + 1000 * min(sum((x - y) ** 2 for x, y in zip(gq, cq)), sum((x + y) ** 2 for x, y in zip(gq, cq)))
            if e < err:
                best, err = p, e
        root_err = sum((x - y) ** 2 for x, y in zip(lt, ct))
        parents[b] = None if i == 0 or root_err < 1e-6 and best is None else best
    parents[BONES[0]] = None
    return parents


PARENT = find_parents()


def to_comp(local):
    comp = {}
    for b in BONES:
        q, t = local[b]
        p = PARENT[b]
        if p is None:
            comp[b] = [q, t]
        else:
            pq, pt = comp[p]
            comp[b] = [qnorm(qmul(pq, q)), [x + y for x, y in zip(pt, qrot(pq, t))]]
    return comp


def to_local(comp):
    local = {}
    for b in BONES:
        q, t = comp[b]
        p = PARENT[b]
        if p is None:
            local[b] = [q, t]
        else:
            pq, pt = comp[p]
            iq = qinv(pq)
            local[b] = [qnorm(qmul(iq, q)), qrot(iq, [x - y for x, y in zip(t, pt)])]
    return local

# ---------------------------------------------------------------- mirroring across the character's sagittal plane


def partner(b):
    for a, c in (('_l', '_r'), ('_r', '_l')):
        if b.endswith(a) and b[:-2] + c in BONES:
            return b[:-2] + c
        if a + '_' in b and b.replace(a + '_', c + '_') in BONES:
            return b.replace(a + '_', c + '_')
    return b


# The lateral axis: the one the hands are spread along in the reference pose.
spread = [abs(REF_C['hand_r'][1][k] - REF_C['hand_l'][1][k]) for k in range(3)]
AXIS = spread.index(max(spread))


def mq(q):   # reflect a rotation through the plane with normal AXIS: keep that component, negate the other two
    return [q[k] if k == AXIS or k == 3 else -q[k] for k in range(4)]


def mt(t):
    return [-t[k] if k == AXIS else t[k] for k in range(3)]


# Per-bone axis correction so the reference pose mirrors onto itself (left / right bones have mirrored local axes).
FIX = {b: qmul(qinv(mq(REF_C[partner(b)][0])), REF_C[b][0]) for b in BONES}


def mirror(local):
    comp = to_comp(local)
    out = {b: [qnorm(qmul(mq(comp[partner(b)][0]), FIX[b])), mt(comp[partner(b)][1])] for b in BONES}
    return to_local(out)

# ---------------------------------------------------------------- sampling source clips


def sample(clip, t):
    c = SRC['clips'][clip]
    rate = c['fps']
    x = max(0.0, min(t * rate, c['frames'] - 1))
    i = int(math.floor(x))
    j = min(i + 1, c['frames'] - 1)
    return blend(c['poses'][i]['local'], c['poses'][j]['local'], x - i)


STANCE_CLIP = 'H2H_Idle_Manny'
STANCE = sample(STANCE_CLIP, 0.0)

# ---------------------------------------------------------------- clip assembly

BLEND_IN, BLEND_OUT = 0.10, 0.22
LOAD = 2 / 30.0      # tap punch load key (on a frame): a held press waits here until it is clearly not a tap
REAR = 0.75          # charged: from the load key to full coil
LOOP = 0.90          # charged: length of the breathing loop
BREATH = 0.25        # how far the breathing eases back toward the pose just before the coil


# ---------------------------------------------------------------- root motion: the fighter steps into each strike
# The mocap fighter really steps forward as they strike, and the retargeted clips carry that travel on their root
# bone (the body stays centred over it). Each strike keeps that travel as root motion (it moves the character's
# capsule), measured from where the strike starts; the body is taken relative to the root, so blends to / from the
# stance happen where the fighter is instead of pulling them back to where they started. Planted feet stay planted.

ROOT = BONES[0]
STANCE = {**STANCE, ROOT: [STANCE[ROOT][0], [0.0, 0.0, STANCE[ROOT][1][2]]]}
STANCE_COMP = to_comp(STANCE)
assert abs(STANCE[ROOT][0][3]) > 0.9999, 'root bone is rotated in the source; root motion extraction assumes it is not'


def with_root(pose, rx, ry):
    out = dict(pose)
    q, t = pose[ROOT]
    out[ROOT] = [q, [rx, ry, t[2]]]
    return out


def strike_segment(clip, start, end):
    """Body poses (root at the origin) and root travel (from 0) of clip between start and end."""
    n = int(round((end - start) * FPS)) + 1
    raw = [sample(clip, start + i / FPS) for i in range(n)]
    r0 = raw[0][ROOT][1]
    root = [[p[ROOT][1][0] - r0[0], p[ROOT][1][1] - r0[1]] for p in raw]
    body = [with_root(p, 0.0, 0.0) for p in raw]
    return body, root


def tap_frames(clip, start, end):
    """A strike cut [start, end] from clip with its travel as root motion, blended in from / out to the stance."""
    body, root = strike_segment(clip, start, end)
    dur = end - start
    frames = []
    for i, pose in enumerate(body):
        tau = i / FPS
        if tau < BLEND_IN:
            pose = blend(STANCE, pose, smooth(tau / BLEND_IN))
        if tau > dur - BLEND_OUT:
            pose = blend(pose, STANCE, smooth((tau - (dur - BLEND_OUT)) / BLEND_OUT))
        frames.append(with_root(pose, *root[i]))
    return frames


def frame_at(frames, t):
    x = max(0.0, min(t * FPS, len(frames) - 1))
    i = int(math.floor(x)); j = min(i + 1, len(frames) - 1)
    return blend(frames[i], frames[j], x - i)


def charged_frames(tap, hold_from, coil_clip, coil_t, strike_end):
    """Tap clip up to hold_from, rear back to the coil (coil_clip at coil_t), breathing loop, strike from the coil."""
    frames = [frame_at(tap, i / FPS) for i in range(int(round(hold_from * FPS)) + 1)]
    start_pose = frames[-1]
    r0 = start_pose[ROOT][1][:2]   # the root stays put through the rear-back and the hold
    body, root = strike_segment(coil_clip, coil_t, strike_end)
    coil = with_root(body[0], *r0)
    rear_n = int(round(REAR * FPS))
    for i in range(1, rear_n + 1):
        frames.append(blend(start_pose, coil, smooth(i / rear_n)))
    hold_start = (len(frames) - 1) / FPS
    eased = with_root(sample(coil_clip, max(0.0, coil_t - 0.12)), *r0)
    loop_n = int(round(LOOP * FPS))
    for i in range(1, loop_n):        # frames strictly inside the loop; the coil itself closes it
        frames.append(blend(coil, eased, BREATH * 0.5 * (1 - math.cos(2 * math.pi * i / loop_n))))
    frames.append(coil)
    hold_end = (len(frames) - 1) / FPS
    dur = strike_end - coil_t
    for i in range(1, len(body)):
        tau = i / FPS
        pose = body[i]
        if tau > dur - BLEND_OUT:
            pose = blend(pose, STANCE, smooth((tau - (dur - BLEND_OUT)) / BLEND_OUT))
        frames.append(with_root(pose, r0[0] + root[i][0], r0[1] + root[i][1]))
    return frames, hold_start, hold_end

# ---------------------------------------------------------------- the attacks (times from strikes.py)


clips, timings = {}, {}

# Right cross (PunchCombo01: out 0.34, contact 0.57, back 0.98) and its charged version: the overhand power punch
# (PunchCombo03: coil 1.35, contact 1.49), the fastest strike in the pack.
punch_r = tap_frames('H2H_PunchCombo01_Manny', 0.34, 0.98 + 0.12)
punch_r_c, r_hs, r_he = charged_frames(punch_r, LOAD, 'H2H_PunchCombo03_Manny', 1.35, 1.66 + 0.25)
clips['RPG_Mocap_Punch_R'], clips['RPG_Mocap_Punch_R_Charged'] = punch_r, punch_r_c
timings.update(RightPunchChargeSwitchTime=LOAD, RightPunchImpactTime=0.57 - 0.34,
               RightPunchHoldStart=r_hs, RightPunchHoldEnd=r_he, RightPunchChargedImpactTime=r_he + (1.49 - 1.35))

# Left jab (PunchCombo01: out 0.20, contact 0.34, back 0.71) and its charged version: the left hook from
# PunchCombo02 (coil 0.74, contact 0.98, back 1.42).
punch_l = tap_frames('H2H_PunchCombo01_Manny', 0.20, 0.71 + 0.10)
punch_l_c, l_hs, l_he = charged_frames(punch_l, LOAD, 'H2H_PunchCombo02_Manny', 0.74, 1.42)
clips['RPG_Mocap_Punch_L'], clips['RPG_Mocap_Punch_L_Charged'] = punch_l, punch_l_c
timings.update(LeftPunchChargeSwitchTime=LOAD, LeftPunchImpactTime=0.34 - 0.20,
               LeftPunchHoldStart=l_hs, LeftPunchHoldEnd=l_he, LeftPunchChargedImpactTime=l_he + (0.98 - 0.74))

# Right kick (Kick02: out 0.54, contact 0.88, back 1.63). It holds at the chamber (knee up) when charged.
K_OUT, K_CONTACT, K_BACK = 0.54, 0.88, 1.63
K_CHAMBER = K_OUT + 0.55 * (K_CONTACT - K_OUT)
kick_r = tap_frames('H2H_Kick02_Manny', K_OUT, K_BACK)
hold = round((K_CHAMBER - K_OUT) * FPS) / FPS   # on a frame, so the tap and charged clips match exactly there
# Charged kick: the tap kick up to the chamber, a breathing loop there, then the rest of the tap kick.
kc = [frame_at(kick_r, i / FPS) for i in range(int(round(hold * FPS)) + 1)]
chamber = kc[-1]
eased = frame_at(kick_r, max(0.0, hold - 0.12))
loop_n = int(round(LOOP * FPS))
for i in range(1, loop_n):
    kc.append(blend(chamber, eased, BREATH * 0.5 * (1 - math.cos(2 * math.pi * i / loop_n))))
kc.append(chamber)
kick_hold_end = (len(kc) - 1) / FPS
for i in range(1, len(kick_r)):
    t = hold + i / FPS
    if t > (len(kick_r) - 1) / FPS:
        break
    kc.append(frame_at(kick_r, t))
clips['RPG_Mocap_Kick_R'], clips['RPG_Mocap_Kick_R_Charged'] = kick_r, kc
clips['RPG_Mocap_Kick_L'] = [mirror(p) for p in kick_r]
clips['RPG_Mocap_Kick_L_Charged'] = [mirror(p) for p in kc]
timings.update(RightKickHoldTime=hold, LeftKickHoldTime=hold, RightKickHoldEnd=kick_hold_end, LeftKickHoldEnd=kick_hold_end,
               RightKickImpactTime=K_CONTACT - K_OUT, LeftKickImpactTime=K_CONTACT - K_OUT,
               RightKickChargedImpactTime=kick_hold_end + (K_CONTACT - K_OUT - hold), LeftKickChargedImpactTime=kick_hold_end + (K_CONTACT - K_OUT - hold))

# Fighting stance: the mocap idle, looping.
idle = SRC['clips'][STANCE_CLIP]
clips['RPG_Mocap_Stance'] = [with_root(sample(STANCE_CLIP, i / FPS), 0.0, 0.0) for i in range(int(idle['length'] * FPS) + 1)]

# ---------------------------------------------------------------- IK target bones follow the real hands and feet
# The character's AnimBlueprint pins the feet (and hands) to the ik_* bones. Retargeting left those where the source
# had them (feet on the ground), which would drag a kicking foot back down, so every frame puts them on the limbs.
IK_FOLLOW = {'ik_foot_l': 'foot_l', 'ik_foot_r': 'foot_r', 'ik_hand_gun': 'hand_r', 'ik_hand_r': 'hand_r', 'ik_hand_l': 'hand_l'}


def bake_ik(pose):
    comp = to_comp(pose)
    for ik in ('ik_foot_root', 'ik_hand_root'):
        comp[ik] = comp[ROOT]
    for ik, limb in IK_FOLLOW.items():
        comp[ik] = comp[limb]
    return to_local(comp)


clips = {name: [bake_ik(p) for p in frames] for name, frames in clips.items()}

# ---------------------------------------------------------------- checks


def err_pose(a, b):
    return max(max(abs(x - y) for x, y in zip(a[k][1], b[k][1])) for k in BONES)


ref_local = {b: REF_L[b] for b in BONES}
print('lateral axis', 'XYZ'[AXIS], '| root', BONES[0], '| bones without parent:', [b for b in BONES if PARENT[b] is None])
print('mirror(ref) vs ref: max translation error %.3f cm' % err_pose(to_comp(mirror(ref_local)), to_comp(ref_local)))
some = clips['RPG_Mocap_Kick_R'][10]
print('mirror(mirror(pose)) vs pose: max translation error %.3f cm' % err_pose(to_comp(mirror(mirror(some))), to_comp(some)))
fk = to_comp(SRC['clips']['H2H_Kick02_Manny']['poses'][20]['local'])
dumped = SRC['clips']['H2H_Kick02_Manny']['poses'][20]['comp']
print('FK vs engine component space: max error %.3f cm' % err_pose(fk, dumped))


def reach_peak(frames, limb):
    best, bt = -1e9, 0
    for i, p in enumerate(frames):
        c = to_comp(p)
        r = c[limb][1][1] - c[ROOT][1][1]
        if r > best:
            best, bt = r, i / FPS
    return best, bt


for name, fr in clips.items():
    end = fr[-1][ROOT][1]
    worst = 0.0
    for i in range(1, len(fr)):   # how far the planted feet slide in the world between frames (root + body)
        for foot in ('foot_l', 'foot_r'):
            a, b = to_comp(fr[i - 1]), to_comp(fr[i])
            if b[foot][1][2] < STANCE_COMP[foot][1][2] + 3:
                worst = max(worst, math.hypot(b[foot][1][0] - a[foot][1][0], b[foot][1][1] - a[foot][1][1]))
    print('%-26s root travel to end: X %+.0f Y %+.0f cm | planted-foot slide, worst frame %.1f cm' % (name, end[0], end[1], worst))

for name, limb, impact in [('RPG_Mocap_Punch_R', 'hand_r', timings['RightPunchImpactTime']),
                           ('RPG_Mocap_Punch_R_Charged', 'hand_r', timings['RightPunchChargedImpactTime']),
                           ('RPG_Mocap_Punch_L', 'hand_l', timings['LeftPunchImpactTime']),
                           ('RPG_Mocap_Punch_L_Charged', 'hand_l', timings['LeftPunchChargedImpactTime']),
                           ('RPG_Mocap_Kick_R', 'foot_r', timings['RightKickImpactTime']),
                           ('RPG_Mocap_Kick_L', 'foot_l', timings['LeftKickImpactTime']),
                           ('RPG_Mocap_Kick_L_Charged', 'foot_l', timings['LeftKickChargedImpactTime'])]:
    reach, t = reach_peak(clips[name], limb)
    print('%-26s %3d frames %.2fs | %s furthest forward of the root %+.0f cm at %.2fs (impact time %.2fs)' % (
        name, len(clips[name]), (len(clips[name]) - 1) / FPS, limb, reach, t, impact))

out = {name: {'fps': int(FPS), 'frames': len(fr), 'bones': BONES,
              'poses': [{b: [p[b][0], p[b][1]] for b in BONES} for p in fr]} for name, fr in clips.items()}
json.dump(out, open(os.path.join(HERE, 'mocap_clips.json'), 'w'))
json.dump({k: round(v, 4) for k, v in timings.items()}, open(os.path.join(HERE, 'mocap_timings.json'), 'w'), indent=1)
print(json.dumps({k: round(v, 4) for k, v in timings.items()}, indent=1))

ik_err = max(math.dist(to_comp(p)['ik_foot_r'][1], to_comp(p)['foot_r'][1]) for p in clips['RPG_Mocap_Kick_R'])
print('ik_foot_r follows foot_r in the right kick: worst %.3f cm' % ik_err)
