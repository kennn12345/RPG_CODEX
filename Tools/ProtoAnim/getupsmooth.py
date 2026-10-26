"""Smoothness check for the get-ups: where a part of the body moves in a hitch instead of one flowing motion.
Run: RPG_SKEL=uefn python getupsmooth.py [name-filter ...]   (default: RPG_GetUp)

Per tracked point (hands, elbows, feet, knees, head, chest, hips) and per frame:
  stop-and-go  the speed drops to a near stop between two faster stretches (a key that halts the motion mid-way)
  jolt         the acceleration jumps in a single frame (a velocity corner: a key or a partial key the curve kinks at)
  flip         a bone turns much faster than its neighbours on that frame (an IK pole / twist flipping round)
"""
import json, math, sys
import protoanim as pa
from protoanim import sub, length, norm, dot

POINTS = ['pelvis', 'spine_05', 'head', 'hand_l', 'hand_r', 'lowerarm_l', 'lowerarm_r', 'foot_l', 'foot_r', 'calf_l',
          'calf_r']
TURNING = ['pelvis', 'spine_05', 'head', 'upperarm_l', 'upperarm_r', 'lowerarm_l', 'lowerarm_r', 'hand_l', 'hand_r',
           'thigh_l', 'thigh_r', 'calf_l', 'calf_r', 'foot_l', 'foot_r']

def qangle(a, b):
    d = abs(sum(x * y for x, y in zip(a, b)))
    return math.degrees(2 * math.acos(min(1.0, d)))

def analyse(name, clip, report=True):
    fps = clip['fps']; dt = 1.0 / fps
    cs = [pa.to_cs({b: tuple(v) for b, v in p.items()}) for p in clip['poses']]
    n = len(cs); found = []
    for b in POINTS:
        P = [c[b][1] for c in cs]
        v = [sub(P[i + 1], P[i]) for i in range(n - 1)]                 # cm per frame
        sp = [length(x) * fps for x in v]                                # cm/s
        a = [length(sub(v[i + 1], v[i])) * fps * fps for i in range(n - 2)]   # cm/s^2
        # stop-and-go: a local minimum under 25% of the slower of the peaks either side (within 0.4 s), peaks > 40 cm/s
        w = int(0.4 * fps)
        for i in range(1, n - 2):
            if not (sp[i] <= sp[i - 1] and sp[i] <= sp[i + 1]): continue
            left, right = max(sp[max(0, i - w):i]), max(sp[i + 1:i + 1 + w] or [0])
            peak = min(left, right)
            if peak > 40 and sp[i] < 0.25 * peak:
                found.append((i * dt, b, 'stop-and-go', f'{sp[i]:.0f} cm/s between {left:.0f} and {right:.0f}'))
        # jolt: the frame-to-frame change of acceleration, against the clip's typical
        jerk = [abs(a[i + 1] - a[i]) for i in range(len(a) - 1)]
        typical = sorted(jerk)[len(jerk) // 2] + 1e-6
        for i, j in enumerate(jerk):
            if j > 400 and j > 12 * typical and (i == 0 or jerk[i - 1] < j) and (i + 1 >= len(jerk) or jerk[i + 1] <= j):
                found.append(((i + 1) * dt, b, 'jolt', f'{j:.0f} cm/s^2 change in a frame ({typical:.0f} typical)'))
    for b in TURNING:
        r = [qangle(cs[i][b][0], cs[i + 1][b][0]) * fps for i in range(n - 1)]   # deg/s
        for i in range(1, n - 2):
            nb = 0.5 * (r[i - 1] + r[i + 1])
            if r[i] > 250 and r[i] > 2.5 * nb:
                found.append((i * dt, b, 'flip', f'{r[i]:.0f} deg/s against {nb:.0f} either side'))
    # merge repeats of the same (part, kind) within 0.1 s
    found.sort()
    out = []
    for f in found:
        if out and out[-1][1] == f[1] and out[-1][2] == f[2] and f[0] - out[-1][0] < 0.1: continue
        out.append(f)
    if report:
        print(f'{name}:', 'smooth' if not out else f'{len(out)} hitches')
        for t, b, kind, what in out: print(f'   {t:5.2f}s  {b:11s} {kind:12s} {what}')
    return out

if __name__ == '__main__':
    want = sys.argv[1:] or ['RPG_GetUp']
    clips = json.load(open(pa.SP.path('clips', '.json')))
    for name, clip in clips.items():
        if any(w in name for w in want): analyse(name, clip)
