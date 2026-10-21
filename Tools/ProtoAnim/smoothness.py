"""Per-frame angular speed (deg/frame, component space) of key bones, to spot pops, wobbles and reversals.
A smooth motion shows speeds that rise and fall gradually; spikes or zig-zags read as awkward."""
import json, math, os, sys
from protoanim import to_cs, qmul, qconj, HERE, sub, length

BONES = ['pelvis', 'spine_03', 'spine_05', 'head', 'clavicle_r', 'upperarm_r', 'lowerarm_r', 'hand_r',
         'upperarm_l', 'lowerarm_l', 'hand_l', 'thigh_r', 'calf_r', 'foot_r', 'thigh_l', 'calf_l']
def ang(a, b):
    d = qmul(a, qconj(b)); w = min(1.0, abs(d[3])); return math.degrees(2 * math.acos(w))

clips = json.load(open(os.path.join(HERE, 'clips.json')))
for name, clip in clips.items():
    if len(sys.argv) > 1 and sys.argv[1] not in name: continue
    frames = [to_cs({b: tuple(v) for b, v in p.items()}) for p in clip['poses']]
    print('\n' + name); print(' t    ' + ' '.join(f'{b[:8]:>8s}' for b in BONES))
    for i in range(1, len(frames)):
        print(f'{i/clip["fps"]:.3f} ' + ' '.join(f'{ang(frames[i][b][0], frames[i-1][b][0]):8.1f}' for b in BONES))
