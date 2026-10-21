"""Motion-quality report for clips.json: what makes a clip read as procedural rather than athletic.

  python motioncheck.py [name-filter] [--json out.json]

Per clip:
  smooth   speed-peak count of the strike limb / pelvis / chest / head (1 per real movement phase; extra peaks =
           pose-to-pose hesitation) and log dimensionless jerk of the strike limb (higher = smoother; human reaches
           sit around -6..-9, robotic stop-start motion well below)
  kink     largest single-frame change in acceleration (a velocity corner, mostly at keys) for each tracked point,
           as a ratio to that point's typical acceleration; > 4 reads as a hitch
  pop      fastest bone rotation step (deg/frame at 60 fps) and the largest jump in it between frames
  pause    mid-clip dips in whole-body speed below 20% of the surrounding peaks (a held frame between moves)
  feet     planted-foot slide (cm, ball and heel while on the floor), knee-over-toe deviation (deg) on loaded legs
  arms     straightest elbow (deg, 180 = locked) and fastest upper-arm twist (deg/frame)
  balance  how far the centre of mass leaves the support of the planted feet (cm, 0 = over the base)
  seam     loop clips: pose and velocity mismatch between the last and first frame
"""
import json, math, os, sys
from protoanim import to_cs, sub, add, mul, dot, cross, length, norm, qmul, qconj, qrot, IDLE_CS, HERE

TRACK = {   # points whose paths matter; fist / sole are the striking surfaces
    'fist_r': ('hand_r', 'middle_01_r', .6), 'fist_l': ('hand_l', 'middle_01_l', .6),
    'elbow_r': ('lowerarm_r', None, 0), 'elbow_l': ('lowerarm_l', None, 0),
    'ball_r': ('ball_r', None, 0), 'ball_l': ('ball_l', None, 0),
    'knee_r': ('calf_r', None, 0), 'knee_l': ('calf_l', None, 0),
    'pelvis': ('pelvis', None, 0), 'chest': ('spine_05', None, 0), 'head': ('head', None, 0),
}
ROT = ['pelvis', 'spine_02', 'spine_03', 'spine_05', 'neck_01', 'head', 'clavicle_r', 'clavicle_l', 'upperarm_r', 'lowerarm_r',
       'hand_r', 'upperarm_l', 'lowerarm_l', 'hand_l', 'thigh_r', 'calf_r', 'foot_r', 'thigh_l', 'calf_l', 'foot_l']
from protoanim import MASS
FLOOR_BALL = {s: IDLE_CS['ball_' + s][1][2] for s in 'lr'}
FLOOR_ANK = {s: IDLE_CS['foot_' + s][1][2] for s in 'lr'}
PLANT_TOL = 2.0   # cm above its idle height that a ball / ankle still counts as on the floor

def point(cs, spec):
    a, b, t = spec
    return cs[a][1] if b is None else add(cs[a][1], mul(sub(cs[b][1], cs[a][1]), t))

def ang(qa, qb):
    d = qmul(qa, qconj(qb)); return math.degrees(2 * math.acos(min(1.0, abs(d[3]))))

def speed_peaks(sp, frac=0.12):
    """Local maxima above frac * max (separated by a real dip), i.e. distinct movement phases."""
    top = max(sp) if sp else 0
    if top < 1e-6: return 0
    peaks, rising, last_peak, trough = 0, False, 0.0, top
    for i in range(1, len(sp)):
        if sp[i] > sp[i - 1]:
            rising = True; trough = min(trough, sp[i - 1])
        elif rising and sp[i] < sp[i - 1]:
            p = sp[i - 1]
            # count it if it is significant and separated from the previous peak by a dip of at least 25% of the smaller
            if p >= frac * top and (peaks == 0 or trough <= 0.75 * min(p, last_peak)):
                peaks += 1; last_peak = p; trough = p
            elif p > last_peak: last_peak = p
            rising = False
    return peaks

def ldj(pos, fps):
    """Log dimensionless jerk of a path (Balasubramanian et al.); closer to zero = smoother."""
    n = len(pos)
    if n < 6: return 0.0
    dt = 1.0 / fps; T = (n - 1) * dt
    v = [mul(sub(pos[i + 1], pos[i]), fps) for i in range(n - 1)]
    vpk = max(length(x) for x in v)
    if vpk < 1e-3: return 0.0
    j = [mul(add(sub(v[i + 2], mul(v[i + 1], 2)), v[i]), fps * fps) for i in range(len(v) - 2)]
    J = sum(dot(x, x) for x in j) * dt
    return -math.log(max(J * T ** 3 / (vpk ** 2), 1e-12))

def hull_dist(p, pts):
    """2D distance from p to the convex hull of pts (0 inside). Small sets: points, segment, polygon."""
    pts = sorted(set((round(x, 3), round(y, 3)) for x, y in pts))
    if not pts: return 1e9
    def seg(p, a, b):
        ab = (b[0] - a[0], b[1] - a[1]); t = max(0, min(1, ((p[0] - a[0]) * ab[0] + (p[1] - a[1]) * ab[1]) / max(ab[0] ** 2 + ab[1] ** 2, 1e-9)))
        return math.hypot(p[0] - a[0] - ab[0] * t, p[1] - a[1] - ab[1] * t)
    if len(pts) == 1: return math.hypot(p[0] - pts[0][0], p[1] - pts[0][1])
    def crs(o, a, b): return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lo, hi = [], []
    for q in pts:
        while len(lo) >= 2 and crs(lo[-2], lo[-1], q) <= 0: lo.pop()
        lo.append(q)
    for q in reversed(pts):
        while len(hi) >= 2 and crs(hi[-2], hi[-1], q) <= 0: hi.pop()
        hi.append(q)
    h = lo[:-1] + hi[:-1]
    if len(h) >= 3 and all(crs(h[i], h[(i + 1) % len(h)], p) >= 0 for i in range(len(h))): return 0.0
    return min(seg(p, h[i], h[(i + 1) % len(h)]) for i in range(len(h))) if len(h) >= 2 else seg(p, pts[0], pts[-1])

def analyse(name, clip):
    fps = clip['fps']; frames = [to_cs({b: tuple(v) for b, v in p.items()}) for p in clip['poses']]
    n = len(frames); r = {'name': name, 'frames': n}
    side = 'l' if name.endswith('_L') or '_L_' in name else 'r'
    limb = ('ball_' if 'Kick' in name else 'fist_') + side
    pos = {k: [point(cs, s) for cs in frames] for k, s in TRACK.items()}
    spd = {k: [length(sub(p[i + 1], p[i])) * fps for i in range(n - 1)] for k, p in pos.items()}
    # smoothness
    r['peaks'] = {k: speed_peaks(spd[k]) for k in (limb, 'pelvis', 'chest', 'head')}
    r['ldj_limb'] = round(ldj(pos[limb], fps), 2)
    # kinks: single-frame acceleration jumps relative to that point's typical acceleration
    kinks = {}
    for k, p in pos.items():
        acc = [mul(add(sub(p[i + 1], mul(p[i], 2)), p[i - 1]), fps * fps) for i in range(1, n - 1)]
        mags = sorted(length(a) for a in acc)
        typ = max(mags[len(mags) // 2] if mags else 0, 1e-6, 0.15 * (mags[-1] if mags else 0))
        jumps = [(length(sub(acc[i + 1], acc[i])) / typ, (i + 1.5) / fps) for i in range(len(acc) - 1)]
        if jumps and max(length(a) for a in acc) > 50: kinks[k] = max(jumps)
    r['kink'] = {k: (round(v[0], 1), round(v[1], 3)) for k, v in sorted(kinks.items(), key=lambda kv: -kv[1][0])[:4]}
    # rotation pops
    worst_step, worst_jump = (0, '', 0), (0, '', 0)
    for b in ROT:
        steps = [ang(frames[i + 1][b][0], frames[i][b][0]) for i in range(n - 1)]
        for i, s in enumerate(steps):
            if s > worst_step[0]: worst_step = (s, b, (i + 1) / fps)
            if i and abs(s - steps[i - 1]) > worst_jump[0]: worst_jump = (abs(s - steps[i - 1]), b, (i + 1) / fps)
    r['rot_step'] = (round(worst_step[0], 1), worst_step[1], round(worst_step[2], 3))
    r['rot_jump'] = (round(worst_jump[0], 1), worst_jump[1], round(worst_jump[2], 3))
    # pauses in whole-body motion (sum of tracked speeds, excluding the hold loops of charged clips)
    body = [sum(spd[k][i] for k in spd) for i in range(n - 1)]
    pauses = []
    for i in range(3, len(body) - 3):
        left, right = max(body[:i]), max(body[i + 1:])
        if body[i] < 0.2 * min(left, right) and body[i] <= min(body[i - 1], body[i + 1]) and min(left, right) > 60:
            pauses.append(round(i / fps, 3))
    r['pauses'] = pauses
    # longest run of frames (mid-clip) with whole-body speed under 20% of the clip's peak: a held / frozen moment
    top = max(body) if body else 0; run = best = 0
    for i in range(2, len(body) - 2):
        run = run + 1 if body[i] < 0.2 * top else 0; best = max(best, run if 0 < i < len(body) - 3 else 0)
    r['still_frames'] = best
    # feet: slide while planted, knee tracking on loaded legs
    slide = {}; knee = {}
    for s in 'lr':
        worst_slide, knee_dev = 0.0, 0.0
        for i in range(1, n):
            for bone, floor in (('ball_' + s, FLOOR_BALL[s]), ('foot_' + s, FLOOR_ANK[s])):
                a, b = frames[i - 1][bone][1], frames[i][bone][1]
                if a[2] < floor + PLANT_TOL and b[2] < floor + PLANT_TOL and bone.startswith('ball'):
                    worst_slide = max(worst_slide, math.hypot(b[0] - a[0], b[1] - a[1]) * fps)
            cs = frames[i]
            hip, kn, ank, ball = cs['thigh_' + s][1], cs['calf_' + s][1], cs['foot_' + s][1], cs['ball_' + s][1]
            if ball[2] < FLOOR_BALL[s] + PLANT_TOL:
                line = norm(sub(ank, hip)); off = sub(sub(kn, hip), mul(line, dot(sub(kn, hip), line)))
                toe = [ball[0] - ank[0], ball[1] - ank[1]]
                if length(off) > 1.5 and math.hypot(*toe) > 1e-3:
                    a1 = math.degrees(math.atan2(off[1], off[0])); a2 = math.degrees(math.atan2(toe[1], toe[0]))
                    d = (a1 - a2 + 180) % 360 - 180
                    knee_dev = max(knee_dev, abs(d))
        slide[s] = round(worst_slide, 1); knee[s] = round(knee_dev, 1)
    r['slide_cms'] = slide; r['knee_dev'] = knee
    # arms
    straight, twist = 0.0, 0.0
    for cs in frames:
        for s in 'lr':
            u, l, h = cs['upperarm_' + s][1], cs['lowerarm_' + s][1], cs['hand_' + s][1]
            straight = max(straight, math.degrees(math.acos(max(-1, min(1, dot(norm(sub(u, l)), norm(sub(h, l))))))))
    for s in 'lr':
        for i in range(1, n):
            q0 = qmul(qconj(frames[i - 1]['clavicle_' + s][0]), frames[i - 1]['upperarm_' + s][0])
            q1 = qmul(qconj(frames[i]['clavicle_' + s][0]), frames[i]['upperarm_' + s][0])
            ax = norm(sub(frames[i]['lowerarm_' + s][1], frames[i]['upperarm_' + s][1]))
            ax = qrot(qconj(frames[i]['clavicle_' + s][0]), ax)
            d = qmul(q1, qconj(q0)); w = max(-1, min(1, d[3]))
            tw = abs(math.degrees(2 * math.atan2(dot(d[:3], ax), w)))
            twist = max(twist, min(tw, 360 - tw))
    r['elbow_max'] = round(straight, 1); r['twist_max'] = round(twist, 1)
    # balance: centre of mass vs the planted feet
    out = 0.0; out_t = 0
    for i, cs in enumerate(frames):
        com = [0, 0, 0]
        for a, b, m in MASS:
            p = cs[a][1] if b is None else mul(add(cs[a][1], cs[b][1]), 0.5); com = add(com, mul(p, m))
        com = mul(com, 1 / sum(m for *_, m in MASS))
        pts = []
        for s in 'lr':
            if cs['ball_' + s][1][2] < FLOOR_BALL[s] + PLANT_TOL:
                pts.append(tuple(cs['ball_' + s][1][:2]))
                if cs['foot_' + s][1][2] < FLOOR_ANK[s] + 4: pts.append(tuple(cs['foot_' + s][1][:2]))
        d = hull_dist(tuple(com[:2]), pts) - 4.0   # feet have width: allow 4 cm
        if d > out: out, out_t = d, i / fps
    r['com_out'] = (round(max(out, 0), 1), round(out_t, 3))
    # loop seam
    if 'Stance' in name or 'Guard' in name:
        a, b = frames[0], frames[-1]
        r['seam_pose'] = round(max(length(sub(a[k][1], b[k][1])) for k in ('hand_l', 'hand_r', 'head', 'pelvis')), 2)
        r['seam_vel'] = round(max(length(sub(sub(frames[1][k][1], a[k][1]), sub(b[k][1], frames[-2][k][1]))) * fps
                                  for k in ('hand_l', 'hand_r', 'head', 'pelvis')), 1)
    return r

def switch_check(clips):
    """Charged clips must match their tap clip up to the switch frame (the game swaps clips there)."""
    pairs = {'RPG_Proto_Punch_R': 0.0909, 'RPG_Proto_Punch_L': 0.0545, 'RPG_Proto_FrontKick_R': 0.1167, 'RPG_Proto_FrontKick_L': 0.2333}
    out = {}
    for base, t in pairs.items():
        for pre in ('', 'FP'):
            a = base if not pre else base.replace('RPG_Proto_', 'RPG_FP_')
            b = a + '_Charged'
            if a not in clips or b not in clips: continue
            f = int(round(t * clips[a]['fps']))
            ca = to_cs({k: tuple(v) for k, v in clips[a]['poses'][f].items()})
            cb = to_cs({k: tuple(v) for k, v in clips[b]['poses'][f].items()})
            out[b] = round(max(length(sub(ca[k][1], cb[k][1])) for k in ca), 3)
    return out

if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    jpath = sys.argv[sys.argv.index('--json') + 1] if '--json' in sys.argv else None
    if jpath in args: args.remove(jpath)
    clips = json.load(open(os.path.join(HERE, 'clips.json')))
    res = {}
    for name, clip in clips.items():
        if args and not any(a in name for a in args): continue
        r = analyse(name, clip); res[name] = r
        print(f"\n{name} ({r['frames']} fr)")
        print(f"  smooth: speed peaks {r['peaks']}  LDJ(strike limb) {r['ldj_limb']}")
        print(f"  kink:   {r['kink']}")
        print(f"  pop:    fastest step {r['rot_step']}  largest step change {r['rot_jump']}")
        print(f"  pause:  {r['pauses'] or 'none'}  longest near-still run {r['still_frames']} frames")
        print(f"  feet:   planted slide cm/s {r['slide_cms']}  knee-over-toe dev {r['knee_dev']}")
        print(f"  arms:   straightest elbow {r['elbow_max']}  max upper-arm twist {r['twist_max']} deg/frame")
        print(f"  balance: CoM outside base {r['com_out']}")
        if 'seam_pose' in r: print(f"  seam:   pose {r['seam_pose']} cm  velocity {r['seam_vel']} cm/s")
    sw = switch_check(clips); print('\nswitch continuity (max bone gap cm at the switch frame):', sw)
    res['_switch'] = sw
    if jpath: json.dump(res, open(jpath, 'w'), indent=1)
