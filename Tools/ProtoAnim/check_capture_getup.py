"""Checks the shipped recording's actual baked transforms, not just its authoring parameters."""
import json
import math
import sys
from pathlib import Path
import protoanim as p
import getupcheck as g
from getups import CONTACT

assert p.SP.NAME == 'uefn', 'Use RPG_SKEL=uefn'
manifest = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent/'capture_getups_uefn.json'
name = sys.argv[2] if len(sys.argv) > 2 else 'RPG_GetUp_FaceDown_Capture8614'
c = json.loads(manifest.read_text())[name]
assert c['frames'] == len(c['poses']) and c['fps'] == 60
worst_floor = 999.
for i, pose in enumerate(c['poses']):
    assert set(pose) == set(p.BONES)
    for b, (q, t) in pose.items():
        assert all(math.isfinite(x) for x in q+t), (i, b, 'nonfinite')
        assert abs(sum(x*x for x in q)-1.) < 1.e-4, (i, b, 'rotation')
    assert pose['root'] == [[0., 0., 0., 1.], [0., 0., 0.]]
    cs = p.to_cs(pose)
    for side in 'lr':
        elbow, swivel = g.hinge(cs, side, 'upperarm', 'lowerarm', 'hand', 'elbow')
        knee, sideways = g.hinge(cs, side, 'thigh', 'calf', 'foot', 'knee')
        shoulder, _ = g.limb_angles(cs, side, 'spine_05', 'upperarm_'+side, 'lowerarm_'+side)
        assert -5. <= elbow <= 150. and abs(swivel) <= 10., (i, side, 'elbow', elbow, swivel)
        assert -5. <= knee <= 150. and abs(sideways) <= 10., (i, side, 'knee', knee, sideways)
        assert -55. <= shoulder <= 175., (i, side, 'shoulder', shoulder)
    for bone, skin in CONTACT:
        clearance = cs[bone][1][2]-skin
        worst_floor = min(worst_floor, clearance)
        assert clearance >= -.5, (i, bone, 'floor', clearance)
end = {b: [q, t] for b, (q, t) in p.IDLE.items()}
assert c['poses'][-1] == end, 'Idle seam'
first, last = p.to_cs(c['poses'][0]), p.to_cs(c['poses'][-1])
assert last['pelvis'][1][2]-first['pelvis'][1][2] > 65., 'Missing rise'
assert p.length(p.sub(last['hand_l'][1], first['hand_l'][1])) > 50., 'Frozen capture'
print('PASS: recorded get-up; finite normalized transforms, fixed root, target hinges, shoulders, floor and exact idle seam;',
      c['frames'], 'frames; minimum skin clearance', round(worst_floor, 3), 'cm')
