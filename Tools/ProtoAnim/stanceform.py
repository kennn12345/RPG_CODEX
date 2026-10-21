"""Form readout of the fighting stance (poses.STANCE), in numbers:
  head    - where the face points: degrees below level (+ = looking down)
  fists   - each fist's height below the eyes, forward of the chest, and the gap between the fists (cm)
  wrists  - bend between forearm and fist (degrees; 0 = straight line)
  feet    - ankle spacing sideways and front-to-back (cm)
  torso   - chest facing yaw (degrees; kept where it is)
Run: python stanceform.py
"""
import contextlib, io, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
with contextlib.redirect_stdout(io.StringIO()):
    import poses
from protoanim import build_pose, qrot, sub, norm, dot, length

EYE_LOCAL = [15.0, 13.0, 0.0]   # (from fpanim.py) camera point in the head bone's space: X up, Y forward on Manny


def report(P, label):
    cs = build_pose(P, return_cs=True)
    hq, ht = cs['head']
    face = norm(qrot(hq, [0.0, 1.0, 0.0]))
    head_down = -math.degrees(math.asin(max(-1, min(1, face[2]))))
    eye = [ht[i] + qrot(hq, EYE_LOCAL)[i] for i in range(3)]
    chest = cs['spine_05'][1]
    out = ['%s:' % label, '  head: face points %.1f deg below level' % head_down]
    fists = {}
    for s in 'lr':
        fist = [(cs['hand_' + s][1][i] + cs['middle_01_' + s][1][i]) / 2 for i in range(3)]
        fists[s] = fist
        fore = norm(sub(cs['hand_' + s][1], cs['lowerarm_' + s][1]))
        palm = norm(sub(cs['middle_01_' + s][1], cs['hand_' + s][1]))
        bend = math.degrees(math.acos(max(-1, min(1, dot(fore, palm)))))
        out.append('  %s fist: %.0f cm below the eyes, %.0f cm in front of the chest | wrist bend %.0f deg' % (
            'lead (left)' if s == 'l' else 'rear (right)', eye[2] - fist[2], fist[1] - chest[1], bend))
    out.append('  gap between fists: %.0f cm' % length(sub(fists['l'], fists['r'])))
    fl, fr = cs['foot_l'][1], cs['foot_r'][1]
    out.append('  feet: %.0f cm apart sideways, %.0f cm front-to-back' % (abs(fl[0] - fr[0]), abs(fl[1] - fr[1])))
    sh = sub(cs['upperarm_l'][1], cs['upperarm_r'][1])
    out.append('  torso: shoulders face %.1f deg (yaw); shoulder height %.0f / %.0f cm, neck length (head - shoulders) %.0f cm' % (
        math.degrees(math.atan2(sh[1], sh[0])), cs['upperarm_l'][1][2], cs['upperarm_r'][1][2],
        ht[2] - (cs['upperarm_l'][1][2] + cs['upperarm_r'][1][2]) / 2))
    print('\n'.join(out))


if __name__ == '__main__':
    report(poses.STANCE, 'current stance')
