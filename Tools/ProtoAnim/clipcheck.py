"""Rough self-collision check: how close each arm gets to the torso/head capsule chain.
Clearance = distance from the limb point to the spine/neck/head segments minus a body radius."""
import json, os, sys
from protoanim import to_cs, sub, add, mul, dot, length, HERE

CHAIN = ['spine_02', 'spine_03', 'spine_04', 'spine_05', 'neck_01', 'head']
RADIUS = {'spine_02': 14, 'spine_03': 15, 'spine_04': 15, 'spine_05': 13, 'neck_01': 7, 'head': 11}

def seg_dist(p, a, b):
    ab = sub(b, a); t = max(0, min(1, dot(sub(p, a), ab) / max(dot(ab, ab), 1e-9)))
    return length(sub(p, add(a, mul(ab, t)))), t

def clearance(cs, p):
    best = 1e9
    for a, b in zip(CHAIN, CHAIN[1:]):
        d, t = seg_dist(p, cs[a][1], cs[b][1])
        best = min(best, d - (RADIUS[a] * (1 - t) + RADIUS[b] * t))
    return best

clips = json.load(open(os.path.join(HERE, 'clips.json')))
for name, clip in clips.items():
    if len(sys.argv) > 1 and sys.argv[1] not in name: continue
    worst = {}
    for i, pose in enumerate(clip['poses']):
        cs = to_cs({b: tuple(v) for b, v in pose.items()})
        for s in 'lr':
            fore_mid = mul(add(cs['lowerarm_' + s][1], cs['hand_' + s][1]), 0.5)
            for label, p, r in (('fist', cs['hand_' + s][1], 5), ('forearm', fore_mid, 4), ('elbow', cs['lowerarm_' + s][1], 4)):
                c = clearance(cs, p) - r
                k = f'{s}_{label}'
                if k not in worst or c < worst[k][0]: worst[k] = (c, round(i / clip['fps'], 3))
    print(name, ' '.join(f'{k}={v[0]:.1f}cm@{v[1]}s' for k, v in sorted(worst.items())))
