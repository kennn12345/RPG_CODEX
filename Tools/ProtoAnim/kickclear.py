"""Kicking leg vs the torso and arms: estimated surface gap per body part over the whole kick (negative = the leg goes
into that part). Run: python kickclear.py clips.json"""
import json, math, sys
from protoanim import to_cs
def pt_seg(x, a, b):
    ab = [b[k] - a[k] for k in range(3)]; t = max(0, min(1, sum((x[k] - a[k]) * ab[k] for k in range(3)) / max(1e-6, sum(v * v for v in ab))))
    return math.dist(x, [a[k] + ab[k] * t for k in range(3)])
def lerp(a, b, u): return [a[k] + (b[k] - a[k]) * u for k in range(3)]
# rough limb radii (cm): thigh 9, knee/shin 6, forearm 5, upper arm 6, torso ~14 front-to-back half depth
PARTS = {'torso (pelvis..chest)': (('pelvis', 'spine_05'), 14), 'left forearm': (('lowerarm_l', 'hand_l'), 5), 'right forearm': (('lowerarm_r', 'hand_r'), 5),
         'left upper arm': (('upperarm_l', 'lowerarm_l'), 6), 'right upper arm': (('upperarm_r', 'lowerarm_r'), 6)}
clips = json.load(open(sys.argv[1]))
for name, k in [('RPG_Proto_FrontKick_R', 'r'), ('RPG_Proto_FrontKick_L', 'l')]:
    c = clips[name]; res = {}
    for i, p in enumerate(c['poses']):
        cs = to_cs({b: tuple(v) for b, v in p.items()})
        pts = [(lerp(cs['thigh_' + k][1], cs['calf_' + k][1], 0.5 + u / 10), 9) for u in range(6)] + [(lerp(cs['calf_' + k][1], cs['foot_' + k][1], u / 5), 6) for u in range(6)]
        for part, ((a, b), r) in PARTS.items():
            gap = min(pt_seg(x, cs[a][1], cs[b][1]) - rx - r for x, rx in pts)
            if part not in res or gap < res[part][0]: res[part] = (gap, i / c['fps'])
    print(name + ':  ' + ' | '.join('%s %+.0f cm @%.2fs' % (p, g, t) for p, (g, t) in res.items()))
print('(surface gap estimate: negative = the leg goes INTO that part)')
