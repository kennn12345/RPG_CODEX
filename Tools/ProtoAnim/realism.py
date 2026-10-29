"""Strict realism gate for hand-keyed clips (the user's rules, 2026-10-29):
  - hands never bent out of place: wrist bend 0-85 deg (getupcheck) AND sideways (radial / ulnar) deviation -20..+30
  - elbows never invert: elbow flexion >= -5 deg, at most 10 deg out of its bending plane (getupcheck), and with the
    arm hanging a bent elbow never points forward (the user caught this at 0.98 s of KneeBack_Fall, 2026-10-29)
  - knees follow where knees go: flexion >= -5, <= 10 deg sideways (getupcheck) AND a bent knee points over its own
    foot (the knee at most 30 deg out of the foot's toe plane)
  - limbs never clip through each other: bone capsules (getupcheck), any overlap past 1.5 cm beyond what the idle has
  - planted feet never skate across the floor (a foot pivots on its ball; it is never dragged)
  - nothing goes into the floor; no IK flips; no snaps or stop-and-go hitches except at a declared contact (a knee
    meeting the floor, a foot planting), where a real body does stop suddenly (getupsmooth)
plus every human joint range in getupcheck. A clip passes only with zero findings.
Run: RPG_SKEL=uefn python realism.py [name-filter ...]   (reads clips_uefn.json, or RPG_CLIP_FILE)
"""
import json, math, os, sys
import protoanim as pa
from protoanim import sub, norm, dot, cross, mul, add, length
import getupcheck as gc
import getupsmooth as gs

def _deg(x): return math.degrees(math.asin(max(-1.0, min(1.0, x))))

def wrist_deviation(cs, s):
    """Radial (-) / ulnar (+) deviation: how far the fingers swing sideways in the palm's plane from the forearm line."""
    fore = norm(sub(cs['hand_' + s][1], cs['lowerarm_' + s][1]))
    fing = norm(sub(cs['middle_01_' + s][1], cs['hand_' + s][1]))
    across = norm(sub(cs['index_01_' + s][1], cs['pinky_01_' + s][1]))   # thumb side
    return _deg(dot(fore, across)) - _deg(dot(fing, across))
IDLE_DEV = {s: wrist_deviation(pa.IDLE_CS, s) for s in 'lr'}

def knee_off_foot(cs, s):
    """Degrees a bent knee points out of its foot's plane (toe direction and vertical). 0 when the leg is near straight."""
    hip, knee, ank, ball = (cs[b + '_' + s][1] for b in ('thigh', 'calf', 'foot', 'ball'))
    line = norm(sub(ank, hip))
    k = sub(knee, hip); out = sub(k, mul(line, dot(k, line)))
    bend = 180.0 - math.degrees(math.acos(max(-1.0, min(1.0, dot(norm(sub(hip, knee)), norm(sub(ank, knee)))))))
    if bend < 25.0: return 0.0                 # nearly straight: the knee has no direction to judge
    toe = norm(sub(ball, ank))
    side = cross(toe, [0.0, 0.0, 1.0])
    if length(side) < 0.2: side = cross(toe, line)  # foot pointing straight down: use the shin
    return abs(_deg(dot(norm(out), norm(side))))
IDLE_KNEE = {s: knee_off_foot(pa.IDLE_CS, s) for s in 'lr'}

def elbow_points_forward(cs, s):
    """With the upper arm hanging (raised less than 45 deg forward), how much a bent elbow's tip points forward
    (0..1 toward the chest's front). A hanging arm's elbow points back / out; forward reads as an inverted elbow
    even when the hinge itself bends the right way (the whole arm wrung round in the shoulder)."""
    flex = gc.hinge(cs, s, 'upperarm', 'lowerarm', 'hand', 'elbow')[0]
    raised = gc.limb_angles(cs, s, 'spine_05', 'upperarm_' + s, 'lowerarm_' + s)[0]
    if flex < 20.0 or raised > 45.0: return 0.0
    sh, el, ha = (cs[b + '_' + s][1] for b in ('upperarm', 'lowerarm', 'hand'))
    line = norm(sub(ha, sh)); k = sub(el, sh)
    tip = norm(sub(k, mul(line, dot(k, line))))
    return max(0.0, dot(tip, gc.frame(cs, 'spine_05')[1]))   # GASP's idle turns the right foot out ~31 deg

def extra_findings(name, clip):
    out = {}
    for i, local in enumerate(clip['poses']):
        cs = pa.to_cs({b: tuple(v) for b, v in local.items()})
        t = i / clip['fps']
        for s in 'lr':
            for label, v, lo, hi in ((f'wrist_{s} sideways', wrist_deviation(cs, s) - IDLE_DEV[s], -20, 30),
                                     (f'knee_{s} off its foot', knee_off_foot(cs, s) - IDLE_KNEE[s], -90, 25),
                                     (f'elbow_{s} points forward', elbow_points_forward(cs, s), 0, 0.2)):
                bad = v - hi if v > hi else (lo - v if v < lo else 0)
                if bad > 0:
                    w = out.setdefault(label, [t, t, v, bad]); w[1] = t
                    if bad > w[3]: w[2], w[3] = v, bad
        for b, skin in (('hand_l', 1.5), ('hand_r', 1.5), ('ball_l', 2.0), ('ball_r', 2.0), ('foot_l', 4.0), ('foot_r', 4.0),
                        ('calf_l', 4.5), ('calf_r', 4.5), ('pelvis', 9.0), ('head', 8.0)):
            z = cs[b][1][2] - skin
            if z < -1.0:
                label = f'{b} into the floor'
                w = out.setdefault(label, [t, t, z, -z]); w[1] = t
                if -z > w[3]: w[2], w[3] = z, -z
    # planted feet never skate: a ball of the foot on the floor (both frames) moving over it faster than SKATE cm/s for
    # SKATE_FRAMES frames in a row (a foot pivoting on its ball barely moves the ball; a dragged foot does)
    fps = clip['fps']; css = [pa.to_cs({b: tuple(v) for b, v in p.items()}) for p in clip['poses']]
    for s in 'lr':
        run = 0
        for i in range(len(css) - 1):
            a, b = css[i]['ball_' + s][1], css[i + 1]['ball_' + s][1]
            on = a[2] < 4.5 and b[2] < 4.5
            v = math.hypot(b[0] - a[0], b[1] - a[1]) * fps
            run = run + 1 if on and v > SKATE else 0
            if run >= SKATE_FRAMES:
                label = f'foot_{s} skates'; t = i / fps
                w = out.setdefault(label, [t, t, v, v]); w[1] = t
                if v > w[3]: w[2], w[3] = v, v
    for label, (t0, t1, v, bad) in sorted(out.items(), key=lambda kv: kv[1][0]):
        print(f'   {label:28s} {t0:4.2f}-{t1:4.2f}s  worst {v:7.1f} ({bad:.1f} past)')
    return out
SKATE, SKATE_FRAMES = 20.0, 3

CONTACT_WINDOW = 0.07   # s either side of a declared contact (a knee meeting the floor, a foot planting)
SNAP = 3000.0           # cm/s^2 change in one frame: a visible snap anywhere else

def smooth_failures(clip):
    """IK flips anywhere; snaps (big jolts) and stop-and-go of a moving part, except at the clip's declared contacts,
    where a real body does stop suddenly."""
    contacts = clip.get('contacts', [])
    # (the hit at t = 0 shoves the whole body: its onset may take a little longer to build)
    near = lambda t: any(abs(t - c) <= (2 * CONTACT_WINDOW if c == 0.0 else CONTACT_WINDOW) for c in contacts)
    bad = []
    for t, b, kind, what in gs.analyse(clip['name'], clip, report=False):
        if kind == 'flip': bad.append((t, b, kind, what))
        elif kind == 'jolt' and float(what.split()[0]) > SNAP and not near(t): bad.append((t, b, kind, what))
    # a pause part-way through a movement that then carries on the same way (a reversal through zero is natural)
    fps = clip['fps']; cs = [pa.to_cs({b: tuple(v) for b, v in p.items()}) for p in clip['poses']]; n = len(cs)
    w = int(0.4 * fps)
    for b in gs.POINTS:
        P = [c[b][1] for c in cs]
        v = [mul(sub(P[i + 1], P[i]), fps) for i in range(n - 1)]
        sp = [length(x) for x in v]
        for i in range(1, n - 2):
            if not (sp[i] <= sp[i - 1] and sp[i] <= sp[i + 1]) or near(i / fps): continue
            li = max(range(max(0, i - w), i), key=lambda j: sp[j]); ri = max(range(i + 1, min(n - 1, i + 1 + w)), key=lambda j: sp[j], default=i)
            peak = min(sp[li], sp[ri])
            if peak > 40 and sp[i] < 0.25 * peak and dot(norm(v[li]), norm(v[ri])) > 0.5:
                bad.append((i / fps, b, 'stop-and-go', f'{sp[i]:.0f} cm/s between {sp[li]:.0f} and {sp[ri]:.0f}, same way'))
    for t, b, kind, what in bad: print(f'   {t:5.2f}s  {b:11s} {kind:12s} {what}')
    return bad

def gate(name, clip):
    """All checks; returns the number of findings (0 = passes)."""
    issues = gc.check_clip(name, clip)
    extra = extra_findings(name, clip)
    hitches = smooth_failures(clip)
    n = len(issues) + len(extra) + len(hitches)
    print(f'   => {name}: {"PASS" if n == 0 else f"FAIL ({n})"}')
    return n

if __name__ == '__main__':
    want = sys.argv[1:] or ['RPG_Catch']
    clips = json.load(open(os.environ.get('RPG_CLIP_FILE', pa.SP.path('clips', '.json'))))
    total = 0
    for name, clip in clips.items():
        if any(w in name for w in want): total += gate(name, clip)
    sys.exit(1 if total else 0)
