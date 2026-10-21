"""Upper-arm twist per frame relative to the clavicle (deg/frame). Large spikes read as a shoulder 'pop' on the mesh."""
import json, math, os, sys
from protoanim import to_cs, qrot, qmul, qconj, qaxis, norm, dot, cross, length, ortho, IDLE, HERE
clips = json.load(open(os.path.join(HERE, 'clips.json')))
for name, clip in clips.items():
    if len(sys.argv) > 1 and sys.argv[1] not in name: continue
    fr = [to_cs({b: tuple(v) for b, v in p.items()}) for p in clip['poses']]
    for side in 'lr':
        ax = norm(IDLE['lowerarm_' + side][1]); worst = (0, 0, 0)
        total = 0
        for i in range(1, len(fr)):
            rel = lambda f: qmul(qconj(f['clavicle_' + side][0]), f['upperarm_' + side][0])
            q0, q1 = rel(fr[i-1]), rel(fr[i])
            d0, d1 = qrot(q0, ax), qrot(q1, ax)
            # remove the minimal swing d0->d1, then the leftover rotation about d1 is the twist
            c = cross(d0, d1); sw = qaxis(c, math.degrees(math.atan2(length(c), dot(d0, d1)))) if length(c) > 1e-9 else [0, 0, 0, 1]
            z_swung = qrot(qmul(sw, q0), [0, 0, 1])
            tw = math.degrees(math.acos(max(-1, min(1, dot(ortho(z_swung, d1), ortho(qrot(q1, [0, 0, 1]), d1))))))
            total += tw
            if tw > worst[0]: worst = (tw, i / clip['fps'], 0)
        print(f'{name} upperarm_{side}: max twist {worst[0]:.1f} deg/frame @ {worst[1]:.3f}s, total {total:.0f} deg')
