"""Procedural rough-prototype combat animations for the UE5 Manny skeleton.

Component space of SK_Mannequin: +Y forward, +X character-left, +Z up.
Quaternions use UE's convention: q = [x, y, z, w], CS[child].rot = CS[parent].rot * Local[child].rot.
The root bone is never animated (identity every frame), so clips can't tip the character over.
"""
import json, math, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
FPS = 60   # 60 rather than 30 so fast snaps (punch extension) are not rounded off between keys

# ---------- vector / quaternion math ----------
def add(a, b): return [a[0]+b[0], a[1]+b[1], a[2]+b[2]]
def sub(a, b): return [a[0]-b[0], a[1]-b[1], a[2]-b[2]]
def mul(a, s): return [a[0]*s, a[1]*s, a[2]*s]
def dot(a, b): return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]
def cross(a, b): return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]
def length(a): return math.sqrt(dot(a, a))
def norm(a):
    l = length(a)
    return [a[0]/l, a[1]/l, a[2]/l] if l > 1e-9 else [0.0, 0.0, 0.0]
def lerp(a, b, t): return [x + (y - x) * t for x, y in zip(a, b)]

def qmul(a, b):
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return [aw*bx + ax*bw + ay*bz - az*by,
            aw*by - ax*bz + ay*bw + az*bx,
            aw*bz + ax*by - ay*bx + az*bw,
            aw*bw - ax*bx - ay*by - az*bz]
def qconj(q): return [-q[0], -q[1], -q[2], q[3]]
def qnorm(q):
    l = math.sqrt(sum(x*x for x in q)); return [x/l for x in q]
def qrot(q, v):
    p = qmul(qmul(q, [v[0], v[1], v[2], 0.0]), qconj(q)); return p[:3]
def qaxis(axis, deg):
    a = norm(axis); h = math.radians(deg) * 0.5; s = math.sin(h)
    return [a[0]*s, a[1]*s, a[2]*s, math.cos(h)]
def qeuler(yaw=0.0, pitch=0.0, roll=0.0):
    """yaw about +Z (positive turns the chest to the character's right / left shoulder forward),
    pitch about +X (positive leans back), roll about +Y (positive tilts toward character-left)."""
    return qmul(qaxis([0, 0, 1], yaw), qmul(qaxis([1, 0, 0], pitch), qaxis([0, 1, 0], roll)))
def qfrom_frames(d0, h0, d1, h1):
    """Rotation taking orthonormal frame (d0, h0, d0 x h0) to (d1, h1, d1 x h1)."""
    def mat_to_q(m):
        tr = m[0][0] + m[1][1] + m[2][2]
        if tr > 0:
            s = math.sqrt(tr + 1.0) * 2
            return qnorm([(m[2][1]-m[1][2])/s, (m[0][2]-m[2][0])/s, (m[1][0]-m[0][1])/s, 0.25*s])
        if m[0][0] > m[1][1] and m[0][0] > m[2][2]:
            s = math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2
            return qnorm([0.25*s, (m[0][1]+m[1][0])/s, (m[0][2]+m[2][0])/s, (m[2][1]-m[1][2])/s])
        if m[1][1] > m[2][2]:
            s = math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2
            return qnorm([(m[0][1]+m[1][0])/s, 0.25*s, (m[1][2]+m[2][1])/s, (m[0][2]-m[2][0])/s])
        s = math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2
        return qnorm([(m[0][2]+m[2][0])/s, (m[1][2]+m[2][1])/s, 0.25*s, (m[1][0]-m[0][1])/s])
    a = [d0, h0, cross(d0, h0)]; b = [d1, h1, cross(d1, h1)]
    # R = B * A^T (columns are frame vectors)
    m = [[sum(b[k][i] * a[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
    return mat_to_q(m)
def ortho(v, d): return norm(sub(v, mul(d, dot(v, d))))

# ---------- skeleton ----------
D = json.load(open(os.path.join(HERE, 'skeleton_dump.json')))
BONES = D['bones']; PARENT = {b: (None if p == 'None' else p) for b, p in D['parents'].items()}
IDLE = {b: (D['idle_local'][b]['q'], D['idle_local'][b]['t']) for b in BONES}
PROBE = json.load(open(os.path.join(HERE, 'probe.json')))
FIST = {b: (v['q'], v['t']) for b, v in PROBE['attack_samples'][0]['fingers'].items()}

def to_cs(local):
    cs = {}
    for b in BONES:
        q, t = local[b]; p = PARENT[b]
        if p is None: cs[b] = (q, t)
        else:
            pq, pt = cs[p]; cs[b] = (qnorm(qmul(pq, q)), add(pt, qrot(pq, t)))
    return cs
IDLE_CS = to_cs(IDLE)

# Lower and upper spine are separate channels so the ribcage can trail the hips.
SPINE_LO = [('spine_01', .3), ('spine_02', .34), ('spine_03', .36)]
SPINE_HI = [('spine_04', .45), ('spine_05', .55)]
FOREARM_TWIST = [('lowerarm_twist_01', .65), ('lowerarm_twist_02', .35)]

# ---------- two-bone IK ----------
_HINGE = {}
def _hinge_sign(upper, lower, end, bone):
    """Which way local Z points relative to the bend normal, read once from the slightly bent idle pose.
    Using a fixed sign (instead of matching the previous frame) stops the forearm/shin flipping
    when the limb passes near straight."""
    key = (upper, bone)
    if key not in _HINGE:
        p = [IDLE_CS[b][1] for b in (upper, lower, end)]
        n = norm(cross(sub(p[1], p[0]), sub(p[2], p[1])))
        _HINGE[key] = 1.0 if dot(qrot(IDLE_CS[bone][0], [0, 0, 1]), n) >= 0 else -1.0
    return _HINGE[key]

def two_bone(cs, local, upper, lower, end, target, pole, end_rot=None, max_reach=0.995):
    """Re-aim upper/lower so `end` reaches target, knee/elbow toward pole. Updates cs in place for the chain."""
    H = cs[upper][1]
    la = length(local[lower][1]); lb = length(local[end][1])
    ax_u = norm(local[lower][1]); ax_l = norm(local[end][1])   # bone direction in its own local frame
    to = sub(target, H); c = min(max(length(to), abs(la - lb) + 0.1), (la + lb) * max_reach)
    u = norm(to); v = ortho(pole, u)
    along = (la*la - lb*lb + c*c) / (2*c); hgt = math.sqrt(max(la*la - along*along, 0))
    K = add(H, add(mul(u, along), mul(v, hgt))); T = add(H, mul(u, c))
    bend = mul(norm(cross(u, v)), -1)                         # = direction of cross(K-H, T-K), never ambiguous
    for bone, child_ax, a, b in ((upper, ax_u, H, K), (lower, ax_l, K, T)):
        q0 = cs[bone][0]
        d0 = qrot(q0, child_ax); h0 = ortho(qrot(q0, [0, 0, 1]), d0)   # Manny hinge axis is local Z
        d1 = norm(sub(b, a)); h1 = mul(ortho(bend, d1), _hinge_sign(upper, lower, end, bone))
        cs[bone] = (qnorm(qmul(qfrom_frames(d0, h0, d1, h1), q0)), a)
    cs[end] = (end_rot if end_rot else cs[end][0], T)

def descend(cs, local, start):
    """Recompute CS for descendants of `start` from locals (after start's CS changed)."""
    for b in BONES:
        p = PARENT[b]
        while p is not None and p != start: p = PARENT[p]
        if p == start:
            pq, pt = cs[PARENT[b]]; q, t = local[b]
            cs[b] = (qnorm(qmul(pq, q)), add(pt, qrot(pq, t)))

REF_CHEST = {}

def set_reference(P):
    """Record the head and chest transforms in pose P. Hands with guard_follow_* > 0 are then
    authored in this pose's space: the hand is carried by the head (a guard protects the head,
    which stays facing the target) and the elbow direction is carried by the chest, so the torso
    can turn under the guard without rotating into the arm or sweeping the hand off the face."""
    REF_CHEST.clear()
    P = dict(P, guard_follow_l=[0], guard_follow_r=[0])
    cs = build_pose(P, return_cs=True)
    REF_CHEST['q'], REF_CHEST['t'] = cs['spine_05']
    REF_CHEST['head_q'], REF_CHEST['head_t'] = cs['head']

def build_pose(P, return_cs=False):
    """P: parameter dict (see STANCE). Returns local transforms per bone."""
    local = {b: (list(q), list(t)) for b, (q, t) in IDLE.items()}
    for b, v in FIST.items(): local[b] = (list(v[0]), list(v[1]))
    # pelvis: world-space offset + rotation about its own pivot
    pq, pt = local['pelvis']
    local['pelvis'] = (qnorm(qmul(qeuler(*P['pelvis_rot']), pq)), add(pt, P['pelvis_off']))
    cs = to_cs(local)
    # spine: distribute a world-space rotation over the chain
    def rot_chain(bone, q_world):
        q0, t0 = cs[bone]; cs[bone] = (qnorm(qmul(q_world, q0)), t0)
        local[bone] = (qnorm(qmul(qconj(cs[PARENT[bone]][0]), cs[bone][0])), local[bone][1]); descend(cs, local, bone)
    for chan, chain in (('spine_lo', SPINE_LO), ('spine_hi', SPINE_HI)):
        y, p, r = P[chan]
        for bone, w in chain: rot_chain(bone, qeuler(y*w, p*w, r*w))
    for side in ('l', 'r'):
        y, p, r = P['clav_' + side]; rot_chain('clavicle_' + side, qeuler(y, p, r))
    # head: hold roughly level in world, plus a little of the body's turn
    hq_target = qmul(qeuler(*P['head_rot']), IDLE_CS['head'][0])
    nk = cs['neck_02'][0]; cur = cs['head'][0]
    blend = qmul(hq_target, qconj(cur))   # remaining world rotation needed at the head
    half = qnorm(lerp([0, 0, 0, 1], blend if blend[3] >= 0 else [-x for x in blend], 0.4))
    rot_chain('neck_01', half)
    cs_h = cs['head']; cs['head'] = (hq_target, cs_h[1])
    local['head'] = (qnorm(qmul(qconj(cs['neck_02'][0]), hq_target)), local['head'][1]); descend(cs, local, 'head')
    # arms
    for side in ('l', 'r'):
        hand = 'hand_' + side
        target, pole = P['hand_' + side], P['elbow_pole_' + side]
        w = P.get('guard_follow_' + side, [0])[0]
        rel = P.get('hand_rel_' + side, [0])[0]
        if rel > 0:   # target given as a world-axis offset from the shoulder's current position
            target = lerp(target, add(cs['upperarm_' + side][1], P['hand_off_' + side]), rel)
        target = add(target, P.get('loop_' + side, [0, 0, 0]))   # arc bump on top of the forward drive
        target = add(target, P.get('fp_off_' + side, [0, 0, 0]))   # first-person clips only (fpanim.py): hand moved into view
        if REF_CHEST:
            # elbow direction is always relative to the chest, like a real elbow: if it stayed fixed in
            # world space while the torso turns, the upper arm would wring around inside the shoulder
            pole = qrot(qmul(cs['spine_05'][0], qconj(REF_CHEST['q'])), pole)
            if w > 0:
                hq, ht = cs['head']; dh = qmul(hq, qconj(REF_CHEST['head_q']))
                target = lerp(target, add(ht, qrot(dh, sub(target, REF_CHEST['head_t']))), w)
        two_bone(cs, local, 'upperarm_' + side, 'lowerarm_' + side, hand, target, pole,
                 max_reach=P['reach_' + side][0])
        # wrist straight relative to the forearm, plus fist roll about the forearm axis
        fq = cs['lowerarm_' + side][0]
        axis = norm(qrot(fq, local[hand][1]))
        roll = P['fist_roll_' + side][0]
        cs[hand] = (qnorm(qmul(qaxis(axis, roll), qmul(fq, local[hand][0]))), cs[hand][1])
        for tb, w in FOREARM_TWIST:   # spread the roll along the forearm instead of snapping at the wrist
            b = tb + '_' + side
            local[b] = (qnorm(qmul(qaxis(qrot(qconj(fq), axis), roll * w), IDLE[b][0])), IDLE[b][1])  # roll in forearm space
    # legs
    for side in ('l', 'r'):
        foot = 'foot_' + side
        # foot_turn_*: turn about the vertical first (e.g. a turned-out rear foot straightening to point at the target as
        # it lifts), then foot_rot, so tipping the foot up never rolls it onto its side
        R = qmul(qeuler(*P['foot_rot_' + side]), qaxis([0, 0, 1], P.get('foot_turn_' + side, [0])[0])); frot = qnorm(qmul(R, IDLE_CS[foot][0]))
        # planted feet pivot on the ball (heel swings), so the toes don't slide across the floor
        ball0, ank0 = IDLE_CS['ball_' + side][1], IDLE_CS[foot][1]
        # plant_* moves where a planted foot sits (a step): the ball goes to its idle spot + plant, the heel pivots there
        pivot_target = add(add(ball0, P.get('plant_' + side, [0.0, 0.0, 0.0])), qrot(R, sub(ank0, ball0)))
        target = lerp(P['foot_' + side], pivot_target, P['pivot_' + side][0])
        two_bone(cs, local, 'thigh_' + side, 'calf_' + side, foot, target, P['knee_pole_' + side], frot)
    # rebuild locals for the IK chains, then propagate to children (twist bones, fingers, toes)
    for chain in (['upperarm_l', 'lowerarm_l', 'hand_l'], ['upperarm_r', 'lowerarm_r', 'hand_r'],
                  ['thigh_l', 'calf_l', 'foot_l'], ['thigh_r', 'calf_r', 'foot_r']):
        for b in chain:
            local[b] = (qnorm(qmul(qconj(cs[PARENT[b]][0]), cs[b][0])), local[b][1])
    cs = to_cs(local)
    # IK helper bones follow the real feet/hands so any foot/hand IK in the AnimBP stays consistent
    def follow(helper, src):
        pq, pt = cs[PARENT[helper]]
        q, t = cs[src]; local[helper] = (qnorm(qmul(qconj(pq), q)), qrot(qconj(pq), sub(t, pt)))
    follow('ik_foot_l', 'foot_l'); follow('ik_foot_r', 'foot_r'); follow('ik_hand_gun', 'hand_r')
    cs = to_cs(local)
    follow('ik_hand_r', 'hand_r'); follow('ik_hand_l', 'hand_l')
    local['root'] = ([0.0, 0.0, 0.0, 1.0], [0.0, 0.0, 0.0])
    return to_cs(local) if return_cs else local

# ---------- keyframing ----------
def ease(kind, t):
    if kind == 'linear': return t
    if kind == 'in': return t*t*t                      # accelerate (strike)
    if kind == 'strike': return t*t                    # accelerate, but spread over more frames than 'in'
    if kind == 'out': return 1 - (1-t)**3              # decelerate (settle)
    if kind == 'snap': return 1 - (1-t)**4             # very fast start, hard stop
    return t*t*(3 - 2*t)                               # smooth

def _fc_tangents(ts, vs):
    """Fritsch-Carlson monotone tangents for one scalar track (no overshoot between keys)."""
    n = len(ts); d = [(vs[k+1] - vs[k]) / (ts[k+1] - ts[k]) for k in range(n - 1)]
    m = [0.0] * n
    for k in range(1, n - 1):
        m[k] = 0.0 if d[k-1] * d[k] <= 0 else 3 * (ts[k+1] - ts[k-1]) / ((2*ts[k+1] - ts[k] - ts[k-1]) / d[k-1] + (ts[k+1] + ts[k] - 2*ts[k-1]) / d[k])
    return m

def sample_channel(ts, vs, eases, time):
    """ts: key times, vs: key values (lists), eases[k]: easing into key k."""
    if time <= ts[0]: return vs[0]
    if time >= ts[-1]: return vs[-1]
    k = next(i for i in range(1, len(ts)) if time <= ts[i])
    t0, t1 = ts[k-1], ts[k]; u = (time - t0) / (t1 - t0)
    if eases[k] not in ('flow', 'whip'): return lerp(vs[k-1], vs[k], ease(eases[k], u))
    h = t1 - t0; out = []
    for c in range(len(vs[0])):
        m = _fc_tangents(ts, [v[c] for v in vs])
        m1 = m[k]
        if eases[k] == 'whip':   # keep the incoming speed, but arrive at twice the average speed (accelerating)
            m1 = 2.0 * (vs[k][c] - vs[k-1][c]) / h
        h00, h10, h01, h11 = 2*u**3 - 3*u**2 + 1, u**3 - 2*u**2 + u, -2*u**3 + 3*u**2, u**3 - u**2
        out.append(h00*vs[k-1][c] + h10*h*m[k-1] + h01*vs[k][c] + h11*h*m1)
    return out

def sample(keys, time, groups=None):
    """keys: list of (time_s, params, ease_into_this_key[, lags[, eases]]).
    lags maps a channel (or a group name from `groups`) to seconds: that channel reaches this key
    that much later (+) or earlier (-) than the key time. This is what creates overlap.
    eases overrides the easing for a channel or group, so e.g. the body can arrive on a smooth
    curve while only the striking arm whips into the same key."""
    groups = groups or {}
    out = {}
    def lookup(d, chan, default):
        return d.get(chan, next((v for g, v in d.items() if chan in groups.get(g, ())), default))
    for chan in keys[0][1]:
        ts, vs, es = [], [], []
        for key in keys:
            t, p = key[0], key[1]
            if chan not in p: continue      # partial key: this channel passes straight through it
            lags = key[3] if len(key) > 3 else {}
            e = lookup(key[4], chan, key[2]) if len(key) > 4 else key[2]
            lag = lookup(lags, chan, 0.0)
            ts.append(max(t + lag, ts[-1] + 1e-3 if ts else 0.0)); vs.append(p[chan]); es.append(e)
        out[chan] = sample_channel(ts, vs, es, time)
    return out

def only(params, groups, *names):
    """Partial key containing just the channels of the named groups."""
    chans = {c for n in names for c in groups[n]}
    return {k: list(v) for k, v in params.items() if k in chans}

def with_(base, **kw):
    p = {k: list(v) for k, v in base.items()}
    for k, v in kw.items(): p[k] = list(v)
    return p

def bake(name, keys, groups=None):
    n = int(round(keys[-1][0] * FPS)) + 1
    # UE resamples sequences at the project rate (30 fps), so the length must be a whole number of 30 fps
    # frames; hold the last pose one extra key if needed.
    step = FPS // 30
    n += (-(n - 1)) % step
    frames = [build_pose(sample(keys, i / FPS, groups)) for i in range(n)]
    return {'name': name, 'fps': FPS, 'frames': n, 'impact': None, 'bones': BONES, 'poses': frames}
