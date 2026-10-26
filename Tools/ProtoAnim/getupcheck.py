"""Anatomy check for the get-ups (and any clip in clips_uefn.json): every frame against human joint ranges, hinges bending
the wrong way or sideways, and body parts passing through each other.
Run: RPG_SKEL=uefn python getupcheck.py [name-filter ...]   (default: RPG_GetUp)

Joint angles are measured in anatomical frames read off the idle (squared to face straight ahead), so the idle itself
reads ~0 everywhere. Self-collision uses capsules round the bones; what the idle already overlaps (mesh-normal contact,
e.g. upper arm against the side of the chest) is subtracted.
"""
import json, math, sys
import protoanim as pa
from protoanim import add, sub, mul, dot, cross, norm, length, qrot, qconj, qmul, qaxis

I = pa.IDLE_CS
def deg(x): return math.degrees(x)
def clampd(x): return max(-1.0, min(1.0, x))

# anatomical axes (left, forward, up) of a bone, in its own local space, from the idle squared to face +Y
def _anat(bone, yaw):
    q = qconj(I[bone][0]); r = qaxis([0, 0, 1], yaw)
    return [qrot(q, qrot(r, a)) for a in ([1, 0, 0], [0, 1, 0], [0, 0, 1])]
ANAT = {'pelvis': _anat('pelvis', pa.SQUARE_HIPS), 'spine_05': _anat('spine_05', pa.SQUARE_HIPS + pa.SQUARE_CHEST),
        'head': _anat('head', pa.SQUARE_HIPS + pa.SQUARE_CHEST)}
def frame(cs, bone):
    return [qrot(cs[bone][0], a) for a in ANAT[bone]]   # world left, forward, up

def hinge_axis(upper, lower, end):
    """The idle's bend normal (flexion positive) in the upper bone's local space."""
    a, b, c = (I[x][1] for x in (upper, lower, end))
    n = norm(cross(sub(b, a), sub(c, b)))
    return qrot(qconj(I[upper][0]), n)
HINGE = {s: {'elbow': hinge_axis('upperarm_' + s, 'lowerarm_' + s, 'hand_' + s),
             'knee': hinge_axis('thigh_' + s, 'calf_' + s, 'foot_' + s)} for s in 'lr'}

def hinge(cs, s, upper, lower, end, key):
    """(flexion deg, sideways deg) of a hinge joint: flexion signed about the idle's bend axis carried by the upper bone,
    sideways = how far the lower segment leaves the bending plane."""
    a, b, c = (cs[x + '_' + s][1] for x in (upper, lower, end))
    u, f = norm(sub(b, a)), norm(sub(c, b))
    n = norm(qrot(cs[upper + '_' + s][0], HINGE[s][key]))
    n = norm(sub(n, mul(u, dot(n, u))))
    side = deg(math.asin(clampd(dot(f, n))))
    fp = norm(sub(f, mul(n, dot(f, n))))
    flex = deg(math.atan2(dot(cross(u, fp), n), dot(u, fp)))
    return flex, side

def limb_angles(cs, s, root_bone, top, bottom):
    """A ball joint (hip / shoulder): (flexion, abduction) of the limb top->bottom in root_bone's anatomical frame.
    flexion: forward swing from hanging straight down (+ forward, - backward); abduction: out to the side."""
    L, F, U = frame(cs, root_bone)
    out = L if s == 'l' else mul(L, -1)
    d = norm(sub(cs[bottom][1], cs[top][1]))
    flex = deg(math.atan2(dot(d, F), -dot(d, U)))
    if flex < -120: flex += 360   # overhead and past it, not far behind the back
    abd = deg(math.asin(clampd(dot(d, out))))
    return flex, abd

def relative(cs, child, parent):
    """(flexion, lateral bend, twist) of child's anatomical frame relative to parent's. flexion + = forward."""
    pl, pf, pu = frame(cs, parent); cl, cf, cu = frame(cs, child)
    flex = deg(math.atan2(dot(cu, pf), dot(cu, pu)))
    lat = deg(math.atan2(-dot(cu, pl), dot(cu, pu)))   # + = toward the right
    cfp = norm(sub(cf, mul(pu, dot(cf, pu))))
    twist = deg(math.atan2(dot(cross(pf, cfp), pu), dot(pf, cfp)))
    return flex, lat, twist

def wrist(cs, s):
    fore = norm(sub(cs['hand_' + s][1], cs['lowerarm_' + s][1]))
    fing = norm(sub(cs['middle_01_' + s][1], cs['hand_' + s][1]))
    return deg(math.acos(clampd(dot(fore, fing))))

def ankle(cs, s):
    shin = norm(sub(cs['foot_' + s][1], cs['calf_' + s][1]))
    foot = norm(sub(cs['ball_' + s][1], cs['foot_' + s][1]))
    return deg(math.acos(clampd(dot(shin, foot))))
IDLE_ANKLE = {s: ankle(I, s) for s in 'lr'}
IDLE_WRIST = {s: wrist(I, s) for s in 'lr'}

# (name, function(cs) -> value, low, high): human ranges, a little inside the textbook limits for comfortable motion
def checks():
    out = []
    for s in 'lr':
        out += [(f'elbow_{s} flex', lambda cs, s=s: hinge(cs, s, 'upperarm', 'lowerarm', 'hand', 'elbow')[0], -5, 145),
                (f'elbow_{s} sideways', lambda cs, s=s: hinge(cs, s, 'upperarm', 'lowerarm', 'hand', 'elbow')[1], -10, 10),
                (f'knee_{s} flex', lambda cs, s=s: hinge(cs, s, 'thigh', 'calf', 'foot', 'knee')[0], -5, 150),
                (f'knee_{s} sideways', lambda cs, s=s: hinge(cs, s, 'thigh', 'calf', 'foot', 'knee')[1], -10, 10),
                (f'hip_{s} flex', lambda cs, s=s: limb_angles(cs, s, 'pelvis', 'thigh_' + s, 'calf_' + s)[0], -25, 130),
                (f'hip_{s} abduct', lambda cs, s=s: limb_angles(cs, s, 'pelvis', 'thigh_' + s, 'calf_' + s)[1], -30, 50),
                (f'shoulder_{s} flex', lambda cs, s=s: limb_angles(cs, s, 'spine_05', 'upperarm_' + s, 'lowerarm_' + s)[0], -55, 175),
                # (side to side means nothing with the arm overhead)
                (f'shoulder_{s} abduct', lambda cs, s=s: (lambda f, a: a if abs(f) < 110 else 0.0)(*limb_angles(cs, s, 'spine_05', 'upperarm_' + s, 'lowerarm_' + s)), -40, 175),
                (f'wrist_{s} bend', lambda cs, s=s: wrist(cs, s), 0, 85),
                (f'ankle_{s} (+dorsi)', lambda cs, s=s: ankle(cs, s) - IDLE_ANKLE[s], -50, 35)]   # (a loaded lunge: 35-40)
    out += [('spine flex', lambda cs: relative(cs, 'spine_05', 'pelvis')[0], -30, 75),
            ('spine lateral', lambda cs: relative(cs, 'spine_05', 'pelvis')[1], -35, 35),
            ('spine twist', lambda cs: relative(cs, 'spine_05', 'pelvis')[2], -40, 40),
            ('neck flex', lambda cs: relative(cs, 'head', 'spine_05')[0], -55, 55),
            ('neck lateral', lambda cs: relative(cs, 'head', 'spine_05')[1], -35, 35),
            ('neck twist', lambda cs: relative(cs, 'head', 'spine_05')[2], -70, 70)]
    return out
CHECKS = checks()

# ---- self-collision: capsules (bone a -> bone b, radius cm) ----
def head_top(cs):   # the head bone is at the skull base; the skull's middle is ~9 cm up its own axis
    return add(cs['head'][1], mul(frame(cs, 'head')[2], 9.0))
CAPS = {
    'hips': (lambda cs: cs['pelvis'][1], lambda cs: cs['spine_02'][1], 12.0),
    'chest': (lambda cs: cs['spine_03'][1], lambda cs: cs['spine_05'][1], 12.0),
    'head': (head_top, head_top, 10.0),
}
for s in 'lr':
    CAPS.update({
        'uarm_' + s: (lambda cs, s=s: cs['upperarm_' + s][1], lambda cs, s=s: cs['lowerarm_' + s][1], 4.5),
        'farm_' + s: (lambda cs, s=s: cs['lowerarm_' + s][1], lambda cs, s=s: cs['hand_' + s][1], 3.5),
        'hand_' + s: (lambda cs, s=s: cs['hand_' + s][1], lambda cs, s=s: cs['middle_01_' + s][1], 3.0),
        'thigh_' + s: (lambda cs, s=s: cs['thigh_' + s][1], lambda cs, s=s: cs['calf_' + s][1], 7.0),
        'shin_' + s: (lambda cs, s=s: cs['calf_' + s][1], lambda cs, s=s: cs['foot_' + s][1], 5.0),
        'foot_' + s: (lambda cs, s=s: cs['foot_' + s][1], lambda cs, s=s: cs['ball_' + s][1], 4.0),
    })
# pairs that are joined (or always touch by construction) are not tested
JOINED = {frozenset(p) for p in [('hips', 'chest'), ('chest', 'head'), ('hips', 'thigh_l'), ('hips', 'thigh_r'),
          ('thigh_l', 'thigh_r'), ('chest', 'uarm_l'), ('chest', 'uarm_r')]}
for s in 'lr':
    JOINED |= {frozenset(p) for p in [('uarm_' + s, 'farm_' + s), ('farm_' + s, 'hand_' + s), ('thigh_' + s, 'shin_' + s),
               ('shin_' + s, 'foot_' + s)]}
PAIRS = [(a, b) for i, a in enumerate(CAPS) for b in list(CAPS)[i + 1:] if frozenset((a, b)) not in JOINED]

def seg_dist(p1, q1, p2, q2):
    d1, d2, r = sub(q1, p1), sub(q2, p2), sub(p1, p2)
    a, e, f = dot(d1, d1), dot(d2, d2), dot(d2, r)
    if a < 1e-9 and e < 1e-9: return length(r)
    if a < 1e-9: s, t = 0.0, max(0.0, min(1.0, f / e))
    else:
        c = dot(d1, r)
        if e < 1e-9: s, t = max(0.0, min(1.0, -c / a)), 0.0
        else:
            b = dot(d1, d2); den = a * e - b * b
            s = max(0.0, min(1.0, (b * f - c * e) / den)) if den > 1e-9 else 0.0
            t = (b * s + f) / e
            if t < 0: t, s = 0.0, max(0.0, min(1.0, -c / a))
            elif t > 1: t, s = 1.0, max(0.0, min(1.0, (b - c) / a))
    return length(sub(add(p1, mul(d1, s)), add(p2, mul(d2, t))))

def overlaps(cs):
    out = {}
    for a, b in PAIRS:
        (pa_, qa, ra), (pb, qb, rb) = CAPS[a], CAPS[b]
        out[(a, b)] = ra + rb - seg_dist(pa_(cs), qa(cs), pb(cs), qb(cs))
    return out
IDLE_OVER = overlaps(I)

def check_clip(name, clip, tol_deg=0.0, tol_cm=1.5):
    fps = clip['fps']; issues = {}
    for i, local in enumerate(clip['poses']):
        cs = pa.to_cs({b: tuple(v) for b, v in local.items()})
        t = i / fps
        for label, fn, lo, hi in CHECKS:
            v = fn(cs)
            bad = v - hi if v > hi + tol_deg else (lo - v if v < lo - tol_deg else 0)
            if bad > 0:
                w = issues.setdefault(label, [t, t, v, bad]); w[1] = t
                if bad > w[3]: w[2], w[3] = v, bad
        for pair, o in overlaps(cs).items():
            o -= max(IDLE_OVER[pair], 0.0)
            if o > tol_cm:
                lbl = 'through: ' + ' / '.join(pair)
                w = issues.setdefault(lbl, [t, t, o, o]); w[1] = t
                if o > w[3]: w[2], w[3] = o, o
    print(f'{name}:', 'ok' if not issues else '')
    for label, (t0, t1, v, bad) in sorted(issues.items(), key=lambda kv: kv[1][0]):
        unit = 'cm' if label.startswith('through') else 'deg'
        print(f'   {label:28s} {t0:4.2f}-{t1:4.2f}s  worst {v:7.1f} {unit} ({bad:.1f} past)')
    return issues

def ranges(clip, labels, every=10):
    """Print the named measurements every `every` frames (for tuning)."""
    fns = {l: fn for l, fn, *_ in CHECKS}
    for i in range(0, clip['frames'], every):
        cs = pa.to_cs({b: tuple(v) for b, v in clip['poses'][i].items()})
        print(f'{i / clip["fps"]:5.2f} ' + ' '.join(f'{l}={fns[l](cs):6.1f}' for l in labels))

if __name__ == '__main__':
    want = sys.argv[1:] or ['RPG_GetUp']
    idle = {'fps': 30, 'frames': 1, 'poses': [pa.IDLE]}
    check_clip('idle (should be ok)', idle)
    clips = json.load(open(pa.SP.path('clips', '.json')))
    for name, clip in clips.items():
        if any(w in name for w in want): check_clip(name, clip)
