"""Bake IMG_8635's steady square guard as planted, fixed-heading loops on the UEFN skeleton.

RPG_SKEL=uefn python capture_stance8635.py
Same method as capture_stance8619.py, but the recording is used as performed: no hand, elbow or foot offsets.
The turntable section (side/back/opposite holds) only measures the footprint; it is not played back.
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
from capture_stance8619 import average, blend, fit_view_shoulder

HERE = Path(__file__).resolve().parent
TP = 'RPG_FightStance_Capture8635'
FP = 'RPG_FP_FightStance_Capture8635'
FPS = 60
SOURCE_START = 0.5                     # Seconds into IMG_8635.mov of solve frame 0.
LOOP_RANGE = (4.0, 9.0)                # Steady front guard used for playback.
VIEWS = [('front', 4.0, 14.0), ('side', 25.5, 31.0), ('back', 34.5, 40.0), ('opposite', 43.5, 49.0)]
SEAM_SECONDS = 0.6
# User request (2026-10-28): lead (left) foot a little further forward. The recorded stance is near straight-legged, so
# the hips sink a little to let the planted legs reach the longer stance.
LEAD_FOOT_FORWARD_CM = 8.0
HIPS_DOWN_CM = 3.0


def tight_fist(bone):
    """The rig's fist with the four fingers closed around the palm (as in build_fight_stance.py); p.FIST alone only
    curls the knuckles about 38 degrees and reads as a half-open hand."""
    rotation = p.FIST[bone][0]
    if any(bone.startswith(f+'_') for f in ('index','middle','ring','pinky')) and bone.split('_')[-2] in ('01','02','03'):
        delta = p.qmul(p.qconj(p.REF[bone][0]), rotation)
        if delta[3] < 0: delta = [-v for v in delta]
        angle = math.degrees(2.*math.acos(max(-1., min(1., delta[3]))))
        rotation = p.qnorm(p.qmul(p.REF[bone][0], p.qaxis(delta[:3], max(angle, {'01':80.,'02':95.,'03':65.}[bone.split('_')[-2]]))))
    return (list(rotation), p.REF[bone][1])


def index(sfps, seconds):
    return round((seconds-SOURCE_START)*sfps)


def build(source):
    sfps = source['fps']
    raw = source['poses'][index(sfps, LOOP_RANGE[0]):index(sfps, LOOP_RANGE[1])]
    stable = [average(raw[max(0,i-2):min(len(raw),i+3)]) for i in range(len(raw))]
    rest = average(stable)
    front = check.frame(p.to_cs(rest), 'spine_05')[1]
    heading = 90.-math.degrees(math.atan2(front[1],front[0]))
    turn = p.qaxis([0,0,1],heading)
    fixed_hips_q = p.qnorm(p.qmul(turn,rest['pelvis'][0]))
    fixed_hips_t = [p.IDLE['pelvis'][1][0], p.IDLE['pelvis'][1][1], rest['pelvis'][1][2]-HIPS_DOWN_CM]
    with contextlib.redirect_stdout(io.StringIO()):
        import fpanim
    fp_key = p.sample_track(fpanim.poses.COMBAT_STANCE,[0.],fpanim.poses.GROUPS)[0]
    fp_rest = fpanim.fp_frame(fp_key,0.)
    normalized = []
    for local in stable:
        local = copy.deepcopy(local)
        local['pelvis'] = (fixed_hips_q,fixed_hips_t)
        local['root'] = ([0.,0.,0.,1.],[0.,0.,0.])
        for b in p.BONES:
            if any(k in b for k in ('index','middle','ring','pinky','thumb','metacarpal')) and b in p.FIST:
                local[b] = tight_fist(b)  # Body solve does not track fingers; use closed fists.
        normalized.append(local)
    anchor = p.to_cs(average(normalized))
    # Footprint: median over the four held directions, each expressed in its own chest-facing frame.
    views, headings = {}, {}
    for label,start,end in VIEWS:
        spans=[];facing=[]
        for local in source['poses'][index(sfps,start):index(sfps,end)]:
            cs=p.to_cs(local);f=check.frame(cs,'spine_05')[1]
            facing.append(math.degrees(math.atan2(f[1],f[0])))
            align=p.qaxis([0,0,1],90.-facing[-1])
            spans.append(p.qrot(align,p.sub(cs['foot_l'][1],cs['foot_r'][1])))
        views[label]=[statistics.median(v[i] for v in spans) for i in range(2)]
        headings[label]=statistics.median(facing)
    footprint=[statistics.median(v[i] for v in views.values()) for i in range(2)]
    current=p.sub(anchor['foot_l'][1],anchor['foot_r'][1])
    feet = {}
    for side in 'lr':
        q,t = anchor['foot_'+side];t=list(t)
        for axis in range(2): t[axis]+=(.5 if side=='l' else -.5)*(footprint[axis]-current[axis])
        if side == 'l': t[1] += LEAD_FOOT_FORWARD_CM
        toe_z=p.add(t,p.qrot(q,normalized[0]['ball_'+side][1]))[2]
        t[2] += max(0.,3.2-toe_z,4.-t[2])
        feet[side] = (q,t)
    seam = round(SEAM_SECONDS*sfps)
    for i in range(seam):
        u = i/(seam-1);u=u*u*(3.-2.*u)
        normalized[-seam+i] = blend(normalized[-seam+i],normalized[i],u)
    cycle = normalized[seam-1:]
    cycle[-1] = copy.deepcopy(cycle[0])
    count = round((len(cycle)-1)/sfps*FPS)+1
    poses, fp_poses = [], []
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
                # Re-seat the recorded elbow on the rig's hinge; the hand stays exactly where it was recorded.
                cs=p.to_cs(local)
                repair_chain(local,'upperarm_'+side,'lowerarm_'+side,'hand_'+side,cs['hand_'+side][1],
                             p.sub(cs['lowerarm_'+side][1],cs['upperarm_'+side][1]))
            poses.append(follow_ik(local))
            view = copy.deepcopy(local)
            for side in 'lr':
                cs=p.to_cs(local)
                # Keep the recorded breathing but fit resting fists to the established first-person view.
                delta=p.mul(p.sub(cs['hand_'+side][1],arm_anchor['hand_'+side][1]),.3)
                target=p.add(ref_fp['hand_'+side][1],delta)
                fit_view_shoulder(view,side,target)
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
                          source='IMG_8635.mov; steady front guard; turntable holds used for footprint only',
                          reference_range_seconds=list(LOOP_RANGE),heading_degrees=heading,
                          view_headings_degrees=headings,reference_foot_spans_cm=views,footprint_cm=footprint,
                          removed_reference_turns=True,planted_feet=True)
    return result


if __name__ == '__main__':
    assert p.SP.NAME=='uefn','Use RPG_SKEL=uefn'
    source=json.loads((HERE/'captures/IMG8635_uefn_retarget.json').read_text())
    clips=build(source)
    (HERE/'capture_stance8635_uefn.json').write_text(json.dumps(clips))
    from preview import render
    for name,clip in clips.items():
        render(clip,str(HERE/'previews'/f'{name}.png'),clip['keys'])
        print(name,clip['frames'],'frames',(clip['frames']-1)/FPS,'seconds','footprint',[round(v,1) for v in clip['footprint_cm']],
              'views',{k:round(v) for k,v in clip['view_headings_degrees'].items()})
