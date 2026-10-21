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
_IDLE_HEEL = math.degrees(math.atan2(IDLE_CS['foot_l'][1][2] - IDLE_CS['ball_l'][1][2],
    math.hypot(IDLE_CS['foot_l'][1][0] - IDLE_CS['ball_l'][1][0], IDLE_CS['foot_l'][1][1] - IDLE_CS['ball_l'][1][1])))

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

SOFT_IK = 0.06    # the last this-much (fraction of limb length) before max reach is eased into, not hit
SOFT_IK_ARM = 0.08   # arms: a wider zone, so a punch decelerates into full extension over several frames

def soft_reach(d, dmax, soft):
    """Distance the IK actually reaches for a target at d: exact up to dmax - soft, then easing toward dmax with no
    corner, so a limb decelerates into full extension instead of locking straight on one frame (elbow / knee pop)."""
    s0 = dmax - soft
    return d if d <= s0 else s0 + soft * (1.0 - math.exp(-(d - s0) / soft))

def two_bone(cs, local, upper, lower, end, target, pole, end_rot=None, max_reach=0.995):
    """Re-aim upper/lower so `end` reaches target, knee/elbow toward pole. Updates cs in place for the chain."""
    H = cs[upper][1]
    la = length(local[lower][1]); lb = length(local[end][1])
    ax_u = norm(local[lower][1]); ax_l = norm(local[end][1])   # bone direction in its own local frame
    to = sub(target, H)
    soft = (SOFT_IK_ARM if upper.startswith('upperarm') else SOFT_IK) * (la + lb)
    c = max(soft_reach(length(to), (la + lb) * max_reach, soft), abs(la - lb) + 0.1)
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

KNEE_TRACK = 1.0    # how fully a planted leg's knee is aimed over its toes (0 = authored knee_pole_* only)
KNEE_OUT = 0.08     # ...and how far outside the toe line (a slightly turned-out knee, never collapsed inward)
LEG_REACH = 0.995   # leg two-bone max reach (the soft zone starts SOFT_IK below it)
# Stance-wide turn of each foot about the vertical (deg, - = toes in / less turnout for the rear foot), under every pose's
# own foot_turn / foot_rot: the stance's foot angle, set once (poses.py) instead of in every key.
FOOT_BASE_TURN = {'l': 0.0, 'r': 0.0}
HEEL_MAX = 38.0     # most a planted heel ever lifts (deg of sole off the floor), authored + automatic
HEEL_SOFT = 4.0     # cm over which the automatic heel lift eases in

def heel_lift(cs, local, side, ball, ank_off, weight=1.0):
    """A planted leg that can't reach its foot (the hip has driven too far away) rises onto the ball instead of the
    foot dragging off the floor: the extra rotation of the foot about the ball's lateral axis that brings the ankle
    back within easy reach. Continuous in the hip position, so the heel peels up and sets down smoothly."""
    hip = cs['thigh_' + side][1]
    L = length(local['calf_' + side][1]) + length(local['foot_' + side][1])
    # the ankle's wanted distance from the hip eases toward the limit (no corner when the heel starts to rise)
    dist0 = length(sub(add(ball, ank_off), hip))
    limit = soft_reach(dist0, L * (LEG_REACH - SOFT_IK) - 0.2, HEEL_SOFT)
    if dist0 <= limit + 1e-4 or weight <= 0: return [0.0, 0.0, 0.0, 1.0]
    toe = norm([-ank_off[0], -ank_off[1], 0.0])
    lat = norm(cross([0.0, 0.0, 1.0], toe))
    if qrot(qaxis(lat, 5.0), ank_off)[2] < ank_off[2]: lat = mul(lat, -1)   # the sign that raises the heel
    # never past HEEL_MAX of heel-up (sole to floor): beyond that the foot would stand on its toes like a dancer, and
    # the ankle would start swinging over the ball
    pitch = lambda off: math.degrees(math.atan2(off[2], math.hypot(off[0], off[1])))
    cap = max(0.0, _IDLE_HEEL + HEEL_MAX - pitch(ank_off))
    dist = lambda a: length(sub(add(ball, qrot(qaxis(lat, a), ank_off)), hip))
    lo, hi = 0.0, cap
    for _ in range(40):                      # closest the ankle gets to the hip within the cap (one dip on an arc)
        m1, m2 = lo + (hi - lo) / 3, hi - (hi - lo) / 3
        if dist(m1) <= dist(m2): hi = m2
        else: lo = m1
    best = 0.5 * (lo + hi)
    if dist(best) > limit: return qaxis(lat, best * weight)   # out of reach anyway: the leg straightens (soft IK)
    lo, hi = 0.0, best
    for _ in range(30):
        mid = 0.5 * (lo + hi)
        if dist(mid) > limit: lo = mid
        else: hi = mid
    return qaxis(lat, hi * weight)

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

# Dempster-style segment masses (fraction of body), at segment midpoints (None: at the bone itself)
MASS = [('pelvis', 'spine_01', .142), ('spine_02', 'spine_04', .139), ('spine_04', 'neck_01', .216), ('head', None, .081),
        ('upperarm_l', 'lowerarm_l', .028), ('upperarm_r', 'lowerarm_r', .028), ('lowerarm_l', 'hand_l', .016),
        ('lowerarm_r', 'hand_r', .016), ('hand_l', None, .006), ('hand_r', None, .006),
        ('thigh_l', 'calf_l', .1), ('thigh_r', 'calf_r', .1), ('calf_l', 'foot_l', .0465), ('calf_r', 'foot_r', .0465),
        ('foot_l', 'ball_l', .0145), ('foot_r', 'ball_r', .0145)]
_MASS_SUM = sum(m for *_, m in MASS)

def center_of_mass(cs):
    c = [0.0, 0.0, 0.0]
    for a, b, m in MASS:
        q = cs[a][1] if b is None else mul(add(cs[a][1], cs[b][1]), 0.5); c = add(c, mul(q, m))
    return mul(c, 1 / _MASS_SUM)

BALANCE_INSET = 3.0   # cm toward the other foot from the standing foot's middle
BALANCE_BALL = 0.55   # where over a supporting foot the centre of mass is held: this share of the way from ankle to ball

def build_pose(P, return_cs=False):
    """P: parameter dict (see STANCE). Returns local transforms per bone.
    balance (0..1): on one foot the centre of mass has to be over that foot or the body falls. With balance > 0 the
    pelvis is shifted horizontally (that share of the way) until the centre of mass sits over the base given by
    weight_l (share of the weight on the left foot: 1 = all on the left, 0 = all on the right), mid-foot and a little
    toward the ball. The authored pose keeps its posture (lean, hip drive); this only moves it over its base. Off (0) in
    two-footed moves, whose base is wide."""
    return _build_pose(balanced(P), return_cs)

def balanced(P):
    """P with pelvis_off moved over the base (see build_pose) and balance cleared."""
    w = P.get('balance', [0.0])[0]
    if w <= 1e-4: return P
    P0 = P; P = dict(P); off = list(P['pelvis_off'])
    for _ in range(6):
        P['pelvis_off'] = off
        cs = _build_pose(P, True)
        wl = P.get('weight_l', [0.5])[0]
        mid = {s: lerp(cs['foot_' + s][1], cs['ball_' + s][1], BALANCE_BALL) for s in 'lr'}
        # held over the inner half of the standing foot (toward the body's midline), still well inside the foot
        inset = [1.0 if mid['l'][0] > mid['r'][0] else -1.0, 0.0, 0.0]   # sideways only: toward the midline
        mid['l'] = sub(mid['l'], mul(inset, BALANCE_INSET)); mid['r'] = add(mid['r'], mul(inset, BALANCE_INSET))
        tgt = lerp(mid['r'], mid['l'], wl)
        g = center_of_mass(cs)
        err = [g[0] - tgt[0], g[1] - tgt[1]]
        if abs(err[0]) < 0.05 and abs(err[1]) < 0.05: break
        off = [off[0] - err[0] / 0.9, off[1] - err[1] / 0.9, off[2]]
    # solved fully, then applied by the balance weight (linear in it, so ramping balance moves the hips evenly)
    a = P0['pelvis_off']
    P['pelvis_off'] = [a[0] + w * (off[0] - a[0]), a[1] + w * (off[1] - a[1]), a[2]]
    P['balance'] = [0.0]
    return P

def _build_pose(P, return_cs=False):
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
        R = qmul(qeuler(*P['foot_rot_' + side]), qaxis([0, 0, 1], P.get('foot_turn_' + side, [0])[0] + FOOT_BASE_TURN[side]))
        # planted feet pivot on the ball (heel swings), so the toes don't slide across the floor
        ball0, ank0 = IDLE_CS['ball_' + side][1], IDLE_CS[foot][1]
        # plant_* moves where a planted foot sits (a step): the ball goes to its idle spot + plant, the heel pivots there
        ball = add(ball0, P.get('plant_' + side, [0.0, 0.0, 0.0]))
        p = P['pivot_' + side][0]
        toe = qrot(R, sub(ball0, ank0)); toe = norm([toe[0], toe[1], 0.0])
        if p > 0:
            R = qmul(heel_lift(cs, local, side, ball, qrot(R, sub(ank0, ball0)), p), R)
        frot = qnorm(qmul(R, IDLE_CS[foot][0]))
        pivot_target = add(ball, qrot(R, sub(ank0, ball0)))
        target = lerp(P['foot_' + side], pivot_target, p)
        pole = P['knee_pole_' + side]
        if p > 0 and KNEE_TRACK > 0:
            # a loaded knee points over its toes (a touch outside them), whatever the authored pole says
            track = norm(add(toe, [KNEE_OUT * (1 if side == 'l' else -1), 0.0, 0.0]))
            pole = lerp(norm(pole), track, p * KNEE_TRACK)
        two_bone(cs, local, 'thigh_' + side, 'calf_' + side, foot, target, pole, frot, max_reach=LEG_REACH)
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
# Every channel is one C1-continuous curve through its keys: each key has a single velocity (tangent), shared by the
# segment arriving at it and the one leaving it, so nothing ever changes speed in a single frame (the old per-segment
# easing left velocity corners at keys, read as hitches and pops). The key's easing token chooses that velocity:
#   'smooth' / 'out' / 'snap'  arrive at rest (0): a real stop, a direction reversal, a hold, the start of a clip
#   'flow' / 'linear'          pass through at the natural speed (monotone Fritsch-Carlson tangent)
#   'strike' / 'whip' / 'in'   arrive accelerating: 1.5x / 1.9x / 2.4x the incoming average speed, and leave at that
#                              same speed (a strike carries on into its follow-through and brakes there)
# A token may also be a number: that multiple of the incoming average speed.
# Segments are Delbourgo-Gregory rational quadratics, which stay monotone (never overshoot a key) for any end
# velocities, so a fast arrival can brake over a short follow-through without bouncing past it. A segment whose
# end velocity points backwards (an authored overshoot) falls back to a cubic Hermite.
EASE_GAIN = {'strike': 1.5, 'whip': 1.9, 'in': 2.4}

def _key_tangents(ts, vs, eases):
    n = len(ts); d = [(vs[k+1] - vs[k]) / (ts[k+1] - ts[k]) for k in range(n - 1)]
    m = [0.0] * n
    for k in range(1, n - 1):
        e = eases[k]
        if isinstance(e, (int, float)) and not isinstance(e, bool):
            m[k] = e * d[k-1]
        elif e in EASE_GAIN:
            m[k] = EASE_GAIN[e] * d[k-1]
        elif e in ('flow', 'linear'):
            m[k] = 0.0 if d[k-1] * d[k] <= 0 else 3 * (ts[k+1] - ts[k-1]) / ((2*ts[k+1] - ts[k] - ts[k-1]) / d[k-1] + (ts[k+1] + ts[k] - 2*ts[k-1]) / d[k])
    # last key: a strike may end its clip moving (never in practice); otherwise at rest
    if n > 1 and (eases[-1] in EASE_GAIN or isinstance(eases[-1], (int, float))):
        m[-1] = (EASE_GAIN.get(eases[-1]) or eases[-1]) * d[-1]
    return m

def _segment(y0, y1, m0, m1, h, u):
    dy = y1 - y0
    if abs(dy) < 1e-9:
        # flat: only the end velocities move it (both are ~0 by construction)
        return y0 + h * (m0 * (u**3 - 2*u**2 + u) + m1 * (u**3 - u**2))
    d0, d1 = m0 * h / dy, m1 * h / dy
    if d0 >= 0 and d1 >= 0:
        w = u * (1 - u)
        return y0 + dy * (u*u + d0 * w) / (1 + (d0 + d1 - 2) * w)
    h00, h10, h01, h11 = 2*u**3 - 3*u**2 + 1, u**3 - 2*u**2 + u, -2*u**3 + 3*u**2, u**3 - u**2
    return h00*y0 + h10*h*m0 + h01*y1 + h11*h*m1

def sample_channel(ts, vs, eases, time, tans=None):
    """ts: key times, vs: key values (lists), eases[k]: how key k is arrived at (see above)."""
    if time <= ts[0]: return list(vs[0])
    if time >= ts[-1]: return list(vs[-1])
    k = next(i for i in range(1, len(ts)) if time <= ts[i])
    h = ts[k] - ts[k-1]; u = (time - ts[k-1]) / h
    tans = tans or [_key_tangents(ts, [v[c] for v in vs], eases) for c in range(len(vs[0]))]
    return [_segment(vs[k-1][c], vs[k][c], tans[c][k-1], tans[c][k], h, u) for c in range(len(vs[0]))]

_SPLINES = {}
def _splines(keys, groups):
    """Per channel: key times (with lags), values, easings and tangents. Built once per key list."""
    ck = (id(keys), id(groups))
    hit = _SPLINES.get(ck)
    if hit and hit[0] is keys: return hit[1]
    def lookup(d, chan, default):
        return d.get(chan, next((v for g, v in d.items() if chan in groups.get(g, ())), default))
    out = {}
    for chan in keys[0][1]:
        ts, vs, es = [], [], []
        for key in keys:
            t, p = key[0], key[1]
            if chan not in p: continue      # partial key: this channel passes straight through it
            lags = key[3] if len(key) > 3 else {}
            e = lookup(key[4], chan, key[2]) if len(key) > 4 else key[2]
            lag = lookup(lags, chan, 0.0)
            ts.append(max(t + lag, ts[-1] + 1e-3 if ts else 0.0)); vs.append(p[chan]); es.append(e)
        out[chan] = (ts, vs, es, [_key_tangents(ts, [v[c] for v in vs], es) for c in range(len(vs[0]))])
    _SPLINES[ck] = (keys, out)   # holds keys, so its id can't be reused while cached
    return out

def sample(keys, time, groups=None):
    """keys: list of (time_s, params, ease_into_this_key[, lags[, eases]]).
    lags maps a channel (or a group name from `groups`) to seconds: that channel reaches this key
    that much later (+) or earlier (-) than the key time. This is what creates overlap.
    eases overrides the easing for a channel or group, so e.g. the body can arrive on a smooth
    curve while only the striking arm whips into the same key."""
    groups = groups or {}
    return {chan: sample_channel(ts, vs, es, time, tans) for chan, (ts, vs, es, tans) in _splines(keys, groups).items()}

# ---------- secondary motion ----------
# A light spring (second-order, slightly underdamped) on chosen channels: the head, the guard hand, the upper torso
# lag the motion a little and settle with a touch of overshoot, instead of starting and stopping on the frame their
# keys say. It only looks backwards in time, so two clips that share keys up to some time (a tap attack and its
# Charged clip, up to the switch) stay identical up to there. `pins` are times where the spring hands back to the
# keyed motion exactly (fading out over PIN_FADE before, picking up the keyed position and speed after): switch frames
# and hold-loop seams, which must match exactly.
PIN_FADE = 0.05
BALANCE_SPRING = (10.0, 1.0)   # how the hips follow the balance correction

def sample_track(keys, times, groups=None, springs=None, pins=()):
    """Params for every time in `times` (ascending): keyed motion plus the springs ({channel or group: (hz, damping)})."""
    groups = groups or {}
    raw = [sample(keys, t, groups) for t in times]
    if any(p.get('balance', [0.0])[0] > 1e-4 for p in raw):
        # balance is solved per frame, then the hips' correction is smoothed in time like the other springs: a body
        # doesn't hold its centre of mass dead still while a leg swings, it lets it drift and catches it
        raw = [balanced(p) for p in raw]
        springs = dict(springs or {}, pelvis_off=BALANCE_SPRING)
    if not springs: return raw
    chans = {}
    for name, sp in springs.items():
        for c in groups.get(name, (name,)):
            if c in raw[0]: chans[c] = sp
    out = [dict(p) for p in raw]
    for c, (hz, z) in chans.items():
        w0 = 2 * math.pi * hz
        x = list(raw[0][c]); v = [0.0] * len(x)
        for i in range(1, len(times)):
            dt = times[i] - times[i-1]; target = raw[i][c]
            near = min((abs(times[i] - p) for p in pins), default=1e9)
            crossed = any(times[i-1] < p <= times[i] for p in pins)
            if crossed:   # take over the keyed motion exactly at the pin
                x = list(target); v = [(a - b) / dt for a, b in zip(target, raw[i-1][c])]
            else:
                steps = max(1, int(math.ceil(dt / 0.004)))
                for _ in range(steps):
                    h = dt / steps
                    for j in range(len(x)):
                        a = w0 * w0 * (target[j] - x[j]) - 2 * z * w0 * v[j]
                        v[j] += a * h; x[j] += v[j] * h
            k = min(near / PIN_FADE, 1.0); k = k * k * (3 - 2 * k)
            out[i][c] = [t + (s - t) * k for s, t in zip(x, target)]
    return out

def only(params, groups, *names):
    """Partial key containing just the channels of the named groups."""
    chans = {c for n in names for c in groups[n]}
    return {k: list(v) for k, v in params.items() if k in chans}

def with_(base, **kw):
    p = {k: list(v) for k, v in base.items()}
    for k, v in kw.items(): p[k] = list(v)
    return p

def frame_count(keys):
    n = int(round(keys[-1][0] * FPS)) + 1
    # UE resamples sequences at the project rate (30 fps), so the length must be a whole number of 30 fps
    # frames; hold the last pose one extra key if needed.
    return n + (-(n - 1)) % (FPS // 30)

def bake(name, keys, groups=None, springs=None, pins=()):
    n = frame_count(keys)
    track = sample_track(keys, [i / FPS for i in range(n)], groups, springs, pins)
    frames = [build_pose(p) for p in track]
    return {'name': name, 'fps': FPS, 'frames': n, 'impact': None, 'bones': BONES, 'poses': frames}
