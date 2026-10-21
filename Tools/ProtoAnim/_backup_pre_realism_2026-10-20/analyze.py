"""Per-frame kinetic-chain readout: when each segment moves fastest, fist speed, elbow angle, foot slide."""
import json, math, os, sys
from protoanim import to_cs, sub, length, norm, dot, qrot, HERE

def yaw(cs, a, b):   # facing yaw of a left->right line (degrees); joint = right elbow for punches, kicking knee for kicks
    v = sub(cs[a][1], cs[b][1]); return math.degrees(math.atan2(v[1], v[0]))

clips = json.load(open(os.path.join(HERE, 'clips.json')))
for name, clip in clips.items():
    if len(sys.argv) > 1 and sys.argv[1] not in name: continue
    side = 'r' if name.endswith('_R') else 'l'
    limb = ('hand_' if 'Punch' in name else 'ball_') + side
    frames = [to_cs({b: tuple(v) for b, v in p.items()}) for p in clip['poses']]
    f0 = frames[0]
    print(f'\n{name}  (impact {clip["impact"]}s)')
    print(' t     hipYaw chestYaw  limbSpd  joint  thumbDir          slideL slideR')
    prev = None
    for i, cs in enumerate(frames):
        hy, cy = yaw(cs, 'thigh_l', 'thigh_r'), yaw(cs, 'upperarm_l', 'upperarm_r')
        spd = length(sub(cs[limb][1], prev[limb][1])) * clip['fps'] if prev else 0
        u, l, h = cs['upperarm_' + side][1], cs['lowerarm_' + side][1], cs['hand_' + side][1]
        elbow = math.degrees(math.acos(max(-1, min(1, dot(norm(sub(u, l)), norm(sub(h, l)))))))
        th = norm(sub(cs['thumb_01_' + side][1], cs['hand_' + side][1]))
        if 'Kick' in name:   # report the kicking knee instead of the right elbow
            u, l, h = cs['thigh_' + side][1], cs['calf_' + side][1], cs['foot_' + side][1]
            elbow = math.degrees(math.acos(max(-1, min(1, dot(norm(sub(u, l)), norm(sub(h, l)))))))
        sl = [length(sub(cs['ball_' + s][1], f0['ball_' + s][1])) for s in 'lr']
        print(f'{i/clip["fps"]:.3f} {hy:7.1f} {cy:8.1f} {spd:8.0f} {elbow:6.0f}  {[round(x,2) for x in th]!s:17s} {sl[0]:5.1f} {sl[1]:5.1f}')
        prev = cs
