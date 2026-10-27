"""Finish IMG_8611 video capture on the UEFN skeleton without replacing authored clips.

RPG_SKEL=uefn python capture8611_getups.py
Input is the engine IK-retargeted capture, not a reconstruction from hand-authored keys.
"""
import copy
import json
import math
from pathlib import Path
import protoanim as p
import getupcheck as check
from flight_symmetry import blend_pose

HERE = Path(__file__).resolve().parent
NAME = 'RPG_GetUp_FaceUp_Capture8611'
START_FRAME = 0  # The input sequence already starts at 3.8 seconds of the phone video.
FPS = 60
SETTLE = 1.2


def ease(u):
    u = max(0., min(1., u))
    return u*u*u*(u*(u*6.-15.)+10.)


def repair_chain(local, upper, lower, end, target, pole, rotation=None):
    """Keep captured joint locations, but express the bend about the target rig's hinge."""
    cs = p.to_cs(local)
    p.two_bone(cs, local, upper, lower, end, target, pole,
               end_rot=rotation or cs[end][0], max_reach=1.)
    for b in (upper, lower, end):
        parent = p.PARENT[b]
        local[b] = (p.qnorm(p.qmul(p.qconj(cs[parent][0]), cs[b][0])), local[b][1])


def follow_ik(local):
    for marker, bone in [('ik_foot_l', 'foot_l'), ('ik_foot_r', 'foot_r'),
                         ('ik_hand_gun', 'hand_r'), ('ik_hand_l', 'hand_l'), ('ik_hand_r', 'hand_r')]:
        if marker not in local:
            continue
        cs = p.to_cs(local)
        pq, pt = cs[p.PARENT[marker]]
        q, t = cs[bone]
        inv = p.qconj(pq)
        local[marker] = (p.qnorm(p.qmul(inv, q)), p.qrot(inv, p.sub(t, pt)))
    return local


def finish_capture(source):
    # This is an offline contact adjustment, not a change to the shared authoring rig defaults.
    old_soft = p.SOFT_IK, p.SOFT_IK_ARM
    p.SOFT_IK = p.SOFT_IK_ARM = 1.e-7
    try:
        end = p.to_cs(source['poses'][-1])
        f = check.frame(end, 'spine_05')[1]
        idle_f = check.frame(p.IDLE_CS, 'spine_05')[1]
        yaw = math.degrees(math.atan2(idle_f[1], idle_f[0])-math.atan2(f[1], f[0]))
        turn = p.qaxis([0, 0, 1], yaw)
        last_hips = p.qrot(turn, end['pelvis'][1])
        offset = [p.IDLE_CS['pelvis'][1][i]-last_hips[i] if i < 2 else 0. for i in range(3)]
        captured = []
        for frame, original in enumerate(source['poses'][START_FRAME:]):
            local = copy.deepcopy(original)
            q, t = local['pelvis']
            local['pelvis'] = (p.qnorm(p.qmul(turn, q)), p.add(p.qrot(turn, t), offset))
            local['root'] = ([0., 0., 0., 1.], [0., 0., 0.])
            for side in 'lr':
                cs = p.to_cs(local)
                upper, lower, hand = ('upperarm_'+side, 'lowerarm_'+side, 'hand_'+side)
                target = list(cs[hand][1]); target[2] = max(3.0, target[2])
                pole = p.sub(cs[lower][1], cs[upper][1])
                if cs[lower][1][2] < 6.:
                    out = p.mul(check.frame(cs, 'spine_05')[0], 1. if side == 'l' else -1.)
                    clearance = 6.-cs[lower][1][2]
                    target = p.add(target, p.mul(out, 1.5*clearance))
                    pole = p.add(pole, p.mul(out, 2.*clearance))
                    pole[2] += .5*clearance
                repair_chain(local, upper, lower, hand, target, pole)
                cs = p.to_cs(local)
                thigh, calf, foot, toe = ('thigh_'+side, 'calf_'+side, 'foot_'+side, 'ball_'+side)
                target = list(cs[foot][1])
                target[2] += max(0., 3.0-cs[toe][1][2], 4.0-target[2])
                repair_chain(local, thigh, calf, foot, target, p.sub(cs[calf][1], cs[thigh][1]))
            captured.append(follow_ik(local))
        # Resample recorded motion at 60Hz. No additional performance detail is invented.
        poses = []
        for i in range((len(captured)-1)*2+1):
            a = min(i//2, len(captured)-1); b = min(a+1, len(captured)-1); u = (i % 2)*.5
            poses.append({bone: (p._nlerp(captured[a][bone][0], captured[b][bone][0], u),
                                 p.lerp(captured[a][bone][1], captured[b][bone][1], u)) for bone in p.BONES})
        # Two small closing steps enter GASP's left-foot-forward idle; feet don't slide into it.
        start = poses[-1]; start_cs = p.to_cs(start)
        for i in range(1, round(SETTLE*FPS)+1):
            t = i/FPS; u = t/SETTLE
            local = blend_pose(start, p.IDLE, u)
            step = 'l' if t < SETTLE*.5 else 'r'
            phase = (t % (SETTLE*.5))/(SETTLE*.5) if t < SETTLE else 1.
            support = start_cs['foot_r'][1] if step == 'l' else p.IDLE_CS['foot_l'][1]
            hips = list(local['pelvis'][1]); toward = [support[0]-hips[0], support[1]-hips[1], 0.]
            bias = p.mul(p.norm(toward), min(8., p.length(toward))*math.sin(math.pi*phase)**2)
            local['pelvis'] = (local['pelvis'][0], p.add(hips, bias))
            for side in 'lr':
                progress = max(0., min(1., (t-(0. if side == 'l' else SETTLE*.5))/(SETTLE*.5)))
                foot = 'foot_'+side
                target = p.lerp(start_cs[foot][1], p.IDLE_CS[foot][1], ease(progress))
                target[2] += (6. if side == 'l' else 4.)*math.sin(math.pi*progress)**2
                rotation = p._nlerp(start_cs[foot][0], p.IDLE_CS[foot][0], ease(progress))
                cs = p.to_cs(local)
                repair_chain(local, 'thigh_'+side, 'calf_'+side, foot, target,
                             p.sub(cs['calf_'+side][1], cs['thigh_'+side][1]), rotation)
            poses.append(follow_ik(local) if i < round(SETTLE*FPS) else copy.deepcopy(p.IDLE))
        return dict(name=NAME, fps=FPS, bones=p.BONES, frames=len(poses), poses=poses,
                    keys=[0., .6, 1.5, 3., 4.5, (len(captured)-1)/30., (len(poses)-1)/FPS],
                    source='IMG_8611.mov; face-up phone capture 2026-10-26',
                    heading_degrees=yaw, trimmed_start_seconds=START_FRAME/30., settle_seconds=SETTLE)
    finally:
        p.SOFT_IK, p.SOFT_IK_ARM = old_soft


if __name__ == '__main__':
    assert p.SP.NAME == 'uefn', 'Use RPG_SKEL=uefn'
    source = json.loads((HERE/'captures/IMG8611_uefn_retarget.json').read_text())
    clip = finish_capture(source)
    (HERE/'capture8611_getups_uefn.json').write_text(json.dumps({NAME: clip}))
    from preview import render
    render(clip, str(HERE/'previews'/f'{NAME}.png'), clip['keys'])
    print(NAME, clip['frames'], 'frames', (clip['frames']-1)/FPS, 'seconds')
