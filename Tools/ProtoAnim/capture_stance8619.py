"""Bake IMG_8619's steady guard as planted, fixed-heading loops on the UEFN skeleton.

RPG_SKEL=uefn python capture_stance8619.py
The turntable solve is retained as input/reference; turns and repositioning are not playback.
"""
import copy
import json
import math
import statistics
import contextlib
import io
from pathlib import Path
import protoanim as p
import getupcheck as check
from capture_getups import repair_chain, follow_ik

HERE = Path(__file__).resolve().parent
TP = 'RPG_CombatStance_Capture8619'
FP = 'RPG_FP_CombatStance_Capture8619'
FPS = 60
HAND_LOWER_CM = 5.0
HAND_FORWARD_CM = 5.0
HAND_SPREAD_CM = 5.0  # Total additional spacing; half per hand.


def hand_offset(side):
    return [HAND_SPREAD_CM*.5*(1. if side=='l' else -1.), HAND_FORWARD_CM, -HAND_LOWER_CM]


def fit_view_shoulder(local,side,target):
    """Protract only as needed for a reachable first-person hand target; retain bone lengths."""
    cs=p.to_cs(local);bone='clavicle_'+side;parent=p.PARENT[bone]
    axis=check.frame(cs,'spine_05')[2];rotation=cs[bone][0]
    inverse=p.qconj(cs[parent][0])
    def reachable(angle):
        turn=p.qaxis(axis,angle*(1. if side=='l' else -1.))
        local[bone]=(p.qnorm(p.qmul(inverse,p.qmul(turn,rotation))),local[bone][1])
        posed=p.to_cs(local);shoulder=posed['upperarm_'+side][1]
        reach=p.length(p.sub(posed['lowerarm_'+side][1],shoulder))+p.length(p.sub(posed['hand_'+side][1],posed['lowerarm_'+side][1]))
        return p.length(p.sub(target,shoulder))<=reach-.1
    if reachable(0.):return
    low,high=0.,40.
    if reachable(high):
        for _ in range(14):
            middle=(low+high)*.5
            if reachable(middle):high=middle
            else:low=middle
    reachable(high)


def average(poses):
    result = copy.deepcopy(poses[0])
    for n, pose in enumerate(poses[1:], 2):
        for b in result:
            result[b] = (p._nlerp(result[b][0], pose[b][0], 1./n),
                         p.lerp(result[b][1], pose[b][1], 1./n))
    return result


def blend(a, b, u):
    return {bone: (p._nlerp(a[bone][0], b[bone][0], u), p.lerp(a[bone][1], b[bone][1], u)) for bone in p.BONES}


def build(source):
    # Use the first stable front view (source seconds 6.5–11.5). All headings remain in the checked-in solve.
    sfps = source['fps']
    raw = source['poses'][round(1.*sfps):round(6.*sfps)]
    stable = [average(raw[max(0,i-2):min(len(raw),i+3)]) for i in range(len(raw))]
    rest = average(stable)
    front = check.frame(p.to_cs(rest), 'spine_05')[1]
    heading = 90.-math.degrees(math.atan2(front[1],front[0]))
    turn = p.qaxis([0,0,1],heading)
    fixed_hips_q = p.qnorm(p.qmul(turn,rest['pelvis'][0]))
    fixed_hips_t = [p.IDLE['pelvis'][1][0], p.IDLE['pelvis'][1][1], rest['pelvis'][1][2]]
    with contextlib.redirect_stdout(io.StringIO()):
        import fpanim
    fp_key = p.sample_track(fpanim.poses.COMBAT_STANCE,[0.],fpanim.poses.GROUPS)[0]
    fp_rest = fpanim.fp_frame(fp_key,0.)
    normalized = []
    for local in stable:
        local = copy.deepcopy(local)
        # Fixed pelvis transform removes the reference rotation, translation and foot shuffles.
        # Retain local recorded torso/arm motion and the bladed pelvis/chest relationship.
        local['pelvis'] = (fixed_hips_q,fixed_hips_t)
        local['root'] = ([0.,0.,0.,1.],[0.,0.,0.])
        for b in p.BONES:
            if any(k in b for k in ('index','middle','ring','pinky','thumb','metacarpal')):
                if b in p.FIST:
                    local[b] = copy.deepcopy(p.FIST[b])  # Body solve does not track fingers; use the rig's existing closed fists.
        normalized.append(local)
    anchor = p.to_cs(average(normalized))
    # Cross-check the footprint in all four holds after expressing each in its own chest-facing frame.
    # A median across views reduces the depth ambiguity of the front-only foot solve.
    views = {}
    for label,start,end in [('front',6.5,11.5),('side',15.,18.),('back',21.5,24.5),('opposite',28.,31.)]:
        spans=[]
        for local in source['poses'][round((start-5.5)*sfps):round((end-5.5)*sfps)]:
            cs=p.to_cs(local);f=check.frame(cs,'spine_05')[1]
            align=p.qaxis([0,0,1],90.-math.degrees(math.atan2(f[1],f[0])))
            spans.append(p.qrot(align,p.sub(cs['foot_l'][1],cs['foot_r'][1])))
        views[label]=[statistics.median(v[i] for v in spans) for i in range(2)]
    footprint=[statistics.median(v[i] for v in views.values()) for i in range(2)]
    current=p.sub(anchor['foot_l'][1],anchor['foot_r'][1])
    feet = {}
    for side in 'lr':
        q,t = anchor['foot_'+side];t=list(t)
        for axis in range(2): t[axis]+=(.5 if side=='l' else -.5)*(footprint[axis]-current[axis])
        t[2] += max(0., 3.2-anchor['ball_'+side][1][2], 4.-t[2])
        feet[side] = (q,t)
    # Blend the last .6 s back into the first .6 s, then loop the remaining 4.4 s.
    seam = round(.6*sfps)
    for i in range(seam):
        u = i/(seam-1);u=u*u*(3.-2.*u)
        normalized[-seam+i] = blend(normalized[-seam+i],normalized[i],u)
    cycle = normalized[seam-1:]
    cycle[-1] = copy.deepcopy(cycle[0])
    count = round((len(cycle)-1)/sfps*FPS)+1
    poses = []
    fp_poses = []
    ref_fp = p.to_cs(fp_rest)
    arm_anchor = p.to_cs(average(cycle))
    old = p.SOFT_IK,p.SOFT_IK_ARM
    p.SOFT_IK=p.SOFT_IK_ARM=1.e-7
    try:
        for i in range(count):
            f = i*sfps/FPS;a=min(int(f),len(cycle)-1);b=min(a+1,len(cycle)-1)
            local = blend(cycle[a],cycle[b],f-a)
            for side in 'lr':
                cs=p.to_cs(local)
                repair_chain(local,'thigh_'+side,'calf_'+side,'foot_'+side,feet[side][1],
                             p.sub(cs['calf_'+side][1],cs['thigh_'+side][1]),feet[side][0])
                cs=p.to_cs(local)
                hand_target=p.add(cs['hand_'+side][1],hand_offset(side))
                repair_chain(local,'upperarm_'+side,'lowerarm_'+side,'hand_'+side,hand_target,
                             p.sub(cs['lowerarm_'+side][1],cs['upperarm_'+side][1]))
            poses.append(follow_ik(local))
            view = copy.deepcopy(local)
            for side in 'lr':
                cs=p.to_cs(local)
                # Keep the recorded breathing but fit resting fists to the established first-person view.
                adjusted_anchor=p.add(arm_anchor['hand_'+side][1],hand_offset(side))
                delta=p.mul(p.sub(cs['hand_'+side][1],adjusted_anchor),.3)
                target=p.add(p.add(ref_fp['hand_'+side][1],delta),hand_offset(side))
                fit_view_shoulder(view,side,target)
                vcs=p.to_cs(view)
                repair_chain(view,'upperarm_'+side,'lowerarm_'+side,'hand_'+side,target,
                             p.sub(ref_fp['lowerarm_'+side][1],ref_fp['upperarm_'+side][1]),ref_fp['hand_'+side][0])
            fp_poses.append(follow_ik(view))
    finally:
        p.SOFT_IK,p.SOFT_IK_ARM=old
    poses[-1]=copy.deepcopy(poses[0]);fp_poses[-1]=copy.deepcopy(fp_poses[0])
    result={}
    for name,rows in [(TP,poses),(FP,fp_poses)]:
        length=(len(rows)-1)/FPS
        result[name]=dict(name=name,fps=FPS,bones=p.BONES,frames=len(rows),poses=rows,
                          keys=[0.,length*.25,length*.5,length*.75,length],
                          source='IMG_8619.mov; steady front guard; other headings retained for reference',
                          reference_range_seconds=[6.5,11.5],heading_degrees=heading,
                          reference_foot_spans_cm=views,footprint_cm=footprint,
                          hand_lower_cm=HAND_LOWER_CM,
                          hand_forward_cm=HAND_FORWARD_CM,hand_spread_total_cm=HAND_SPREAD_CM,
                          removed_reference_turns=True,planted_feet=True)
    return result


if __name__ == '__main__':
    assert p.SP.NAME=='uefn','Use RPG_SKEL=uefn'
    source=json.loads((HERE/'captures/IMG8619_uefn_retarget.json').read_text())
    clips=build(source)
    (HERE/'capture_stance8619_uefn.json').write_text(json.dumps(clips))
    from preview import render
    for name,clip in clips.items():
        render(clip,str(HERE/'previews'/f'{name}.png'),clip['keys'])
        print(name,clip['frames'],'frames',(clip['frames']-1)/FPS,'seconds')
