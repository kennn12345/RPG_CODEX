"""Retargeted IMG_8630 punches: one strike per clip, three variants per hand/tier.

RPG_SKEL=uefn python capture_punches8630.py
Recorded body motion is fitted to the approved stance, then given the requested graded extension.
"""
import copy
import json
import math
from pathlib import Path
import protoanim as p
import getupcheck as g
from capture_getups import repair_chain,follow_ik

HERE=Path(__file__).resolve().parent
FPS=60
TIERS=['Light','Heavy','Power']
SOURCE=json.loads((HERE/'captures/IMG8630_uefn_retarget.json').read_text())
TAKE=json.loads((HERE.parent/'Mocap/capture8630_take.json').read_text())
STANCES=json.loads((HERE/'capture_stance8619_uefn.json').read_text())
STANCE=STANCES['RPG_CombatStance_Capture8619']['poses'][0]
FP_STANCE=STANCES['RPG_FP_CombatStance_Capture8619']['poses'][0]
REST=p.to_cs(STANCE);FP_REST=p.to_cs(FP_STANCE)


def ease(u):
    u=max(0.,min(1.,u));return u*u*u*(u*(u*6.-15.)+10.)


def blend(a,b,u):
    return {bone:(p._nlerp(a[bone][0],b[bone][0],u),p.lerp(a[bone][1],b[bone][1],u)) for bone in p.BONES}


def sample(time,segment):
    rows=[r for r in TAKE['mapping'] if r['segment']==segment]
    if time<=rows[0]['source_time']:return copy.deepcopy(SOURCE['poses'][rows[0]['frame']])
    for a,b in zip(rows,rows[1:]):
        if a['source_time']<=time<=b['source_time']:
            u=(time-a['source_time'])/(b['source_time']-a['source_time'])
            return blend(SOURCE['poses'][a['frame']],SOURCE['poses'][b['frame']],u)
    return copy.deepcopy(SOURCE['poses'][rows[-1]['frame']])


def straight_wrist(local,side,weight):
    cs=p.to_cs(local);hand='hand_'+side;parent=p.PARENT[hand]
    finger=p.norm(p.sub(cs['middle_01_'+side][1],cs[hand][1]));fore=p.norm(p.sub(cs[hand][1],cs['lowerarm_'+side][1]))
    cross=p.cross(finger,fore);dot=max(-1.,min(1.,p.dot(finger,fore)))
    if dot<-.999:return
    turn=p.qnorm(cross+[1.+dot]);q=p.qmul(turn,cs[hand][0]);q=p.qmul(p.qconj(cs[parent][0]),q)
    local[hand]=(p._nlerp(local[hand][0],q,weight),local[hand][1])


def elbow_clearance(cs,side,target):
    shoulder=cs['upperarm_'+side][1]
    upper=p.length(p.sub(cs['lowerarm_'+side][1],shoulder));lower=p.length(p.sub(cs['hand_'+side][1],cs['lowerarm_'+side][1]))
    minimum=math.sqrt(upper*upper+lower*lower+2.*upper*lower*math.cos(math.radians(142.)))
    delta=p.sub(target,shoulder)
    return p.add(shoulder,p.mul(p.norm(delta),minimum)) if p.length(delta)<minimum else target


def fit_punch_shoulder(local,side,target):
    """Find the smallest useful clavicle adjustment, including already-rotated captured shoulders."""
    cs=p.to_cs(local);bone='clavicle_'+side;parent=p.PARENT[bone]
    reach=p.length(p.sub(cs['lowerarm_'+side][1],cs['upperarm_'+side][1]))+p.length(p.sub(cs['hand_'+side][1],cs['lowerarm_'+side][1]))
    axis=g.frame(cs,'spine_05')[2];rotation=cs[bone][0];inverse=p.qconj(cs[parent][0])
    def distance(angle):
        q=p.qmul(inverse,p.qmul(p.qaxis(axis,angle),rotation))
        local[bone]=(p.qnorm(q),local[bone][1])
        return p.length(p.sub(target,p.to_cs(local)['upperarm_'+side][1]))
    original=distance(0.)
    if original<=reach-.01:return
    best_angle=0.;best=original
    for angle in sorted(range(-40,41,2),key=abs):
        d=distance(angle)
        if d<best:best_angle,best=angle,d
        if d<=reach-.01:
            low,high=0.,float(angle)
            for _ in range(12):
                mid=(low+high)*.5
                if distance(mid)<=reach-.01:high=mid
                else:low=mid
            distance(high);return
    distance(best_angle)


def recorded(side,time,tier,extension=0.,variation=0.):
    segment='normal_pair' if tier==0 else ('charged_left' if side=='l' else 'charged_right')
    baseline_time=28.95 if tier==0 else (63.7 if side=='l' else 66.05)
    base=sample(baseline_time,segment);frame=sample(time,segment)
    base_cs=p.to_cs(base);source_cs=p.to_cs(frame)
    facing=g.frame(base_cs,'spine_05')[1]
    turn=p.qaxis([0,0,1],90.-math.degrees(math.atan2(facing[1],facing[0])))
    amount=[.65,.85,1.][tier]
    local=copy.deepcopy(STANCE)
    for bone in p.BONES:
        if bone=='root' or any(k in bone for k in ('index','middle','ring','pinky','thumb','metacarpal','ik_')):continue
        delta=p.qmul(p.qconj(base[bone][0]),frame[bone][0])
        local[bone]=(p.qnorm(p.qmul(STANCE[bone][0],p._nlerp([0,0,0,1],delta,amount))),local[bone][1])
    hips=p.mul(p.qrot(turn,p.sub(source_cs['pelvis'][1],base_cs['pelvis'][1])),amount)
    shift=[2.,7.,14.][tier]*extension
    hips=p.add(hips,[variation*.5*extension,shift,0.])
    local['pelvis']=(local['pelvis'][0],p.add(STANCE['pelvis'][1],hips))
    for s in 'lr':
        cs=p.to_cs(local);foot='foot_'+s
        target=p.add(REST[foot][1],p.mul(p.qrot(turn,p.sub(source_cs[foot][1],base_cs[foot][1])),.18*amount))
        target[2]=REST[foot][1][2]
        rotation=cs[foot][0]
        toe=p.add(target,p.qrot(rotation,local['ball_'+s][1]));target[2]+=max(0.,3.2-toe[2])
        repair_chain(local,'thigh_'+s,'calf_'+s,foot,target,p.sub(cs['calf_'+s][1],cs['thigh_'+s][1]),rotation)
    for s in 'lr':
        cs=p.to_cs(local);hand='hand_'+s;shoulder=cs['upperarm_'+s][1]
        delta=p.qrot(turn,p.sub(source_cs[hand][1],base_cs[hand][1]))
        target=p.add(REST[hand][1],p.mul(delta,amount*(1. if s==side else .35)))
        target=p.add(target,[0.,shift*(1. if s==side else .3),0.])
        if s==side:
            target[0]+=variation*1.5*extension;target[2]+=variation*1.2*extension
            reach=p.length(p.sub(cs['lowerarm_'+s][1],shoulder))+p.length(p.sub(cs[hand][1],cs['lowerarm_'+s][1]))
            aim=list(target);aim[0]=aim[0]*(1.-extension*.65)+(4. if s=='l' else -4.)*extension*.65
            aim[1]=max(aim[1],shoulder[1]+35.+5.*tier)
            extended=p.add(shoulder,p.mul(p.norm(p.sub(aim,shoulder)),reach*[.92,.96,.985][tier]))
            target=p.lerp(target,extended,extension)
            # Preserve close-range contact after fitting the shorter UEFN guard; protract instead of stretching bones.
            target=p.add(target,[0.,8.*extension,0.])
            if extension>0.:
                fit_punch_shoulder(local,s,target);cs=p.to_cs(local);shoulder=cs['upperarm_'+s][1]
        else:target[2]=max(target[2],REST[hand][1][2]-5.)
        target=elbow_clearance(cs,s,target)
        repair_chain(local,'upperarm_'+s,'lowerarm_'+s,hand,target,p.sub(cs['lowerarm_'+s][1],shoulder))
        straight_wrist(local,s,.8 if s==side else .5)
    local['root']=([0.,0.,0.,1.],[0.,0.,0.])
    return follow_ik(local)


def curve(keys,t):
    for (ta,a),(tb,b) in zip(keys,keys[1:]):
        if ta<=t<=tb:return blend(a,b,ease((t-ta)/(tb-ta)))
    return copy.deepcopy(keys[-1][1])


def build(side,tier,variant):
    impact=.3 if tier==0 else 1.5;length=.95 if tier==0 else (2.2 if tier==1 else 2.4)
    # All variants share the loading/hold frames. Selection at release cannot jump between different coils.
    normal_times=[29.55,29.79,30.00] if side=='l' else [30.14,30.42,31.04]
    charged_times=[64.23,64.70,65.03,65.36] if side=='l' else [66.70,67.03,67.35,67.78]
    load=recorded(side,normal_times[0],0)
    v=[0.,1.,-1.][variant]
    if tier==0:
        keys=[(0.,STANCE),(.12,load),(.3,recorded(side,normal_times[1],0,1.,v)),(.58,recorded(side,normal_times[2],0)),(length,STANCE)]
    else:
        coil=recorded(side,charged_times[0],1)
        keys=[(0.,STANCE),(.12,load),(.6,coil),(1.2,coil),(impact,recorded(side,charged_times[1],tier,1.,v)),
              (1.8,recorded(side,charged_times[2],tier,.25,v)),(length-.2,recorded(side,charged_times[3],tier)),(length,STANCE)]
    rows=[];views=[]
    for i in range(round(length*FPS)+1):
        t=i/FPS;pose=curve(keys,t)
        # Sample the intervening solved video frames as well as the named timing landmarks.
        if tier==0 and .12<t<.58:
            if t<=impact:
                u=(t-.12)/(impact-.12);source_time=normal_times[0]+(normal_times[1]-normal_times[0])*u;extension=ease(u)
            else:
                u=(t-impact)/(.58-impact);source_time=normal_times[1]+(normal_times[2]-normal_times[1])*u;extension=1.-ease(u)
            pose=recorded(side,source_time,0,extension,v)
        elif tier>0 and 1.2<t<length-.2:
            if t<=impact:
                u=(t-1.2)/(impact-1.2);source_time=charged_times[0]+(charged_times[1]-charged_times[0])*u;extension=ease(u)
            elif t<=1.8:
                u=(t-impact)/(1.8-impact);source_time=charged_times[1]+(charged_times[2]-charged_times[1])*u;extension=1.-.75*ease(u)
            else:
                u=(t-1.8)/(length-.2-1.8);source_time=charged_times[2]+(charged_times[3]-charged_times[2])*u;extension=.25*(1.-ease(u))
            pose=recorded(side,source_time,tier,extension,v)
            if tier==2 and t<impact:pose=blend(recorded(side,source_time,1,extension,v),pose,ease((t-1.2)/(impact-1.2)))
        # Re-establish target-rig hinges after interpolation, keeping the evaluated recorded endpoints.
        for s in 'lr':
            cs=p.to_cs(pose)
            for upper,lower,end in [('upperarm','lowerarm','hand'),('thigh','calf','foot')]:
                cs=p.to_cs(pose);target=elbow_clearance(cs,s,cs[end+'_'+s][1]) if upper=='upperarm' else cs[end+'_'+s][1]
                repair_chain(pose,upper+'_'+s,lower+'_'+s,end+'_'+s,target,p.sub(cs[lower+'_'+s][1],cs[upper+'_'+s][1]),cs[end+'_'+s][0])
        rows.append(follow_ik(pose));view=copy.deepcopy(pose)
        for s in 'lr':
            # The striking hand is identical to third person across the entire gameplay hit window.
            fp_weight=ease((abs(t-impact)-.09)/.12) if s==side else 1.
            if tier>0 and s==side and t<.25:
                prefix=ease((abs(min(t,.12)-.3)-.09)/.12)
                fp_weight=prefix if t<=.12 else prefix+(1.-prefix)*ease((t-.12)/.13)
            delta=p.sub(FP_REST['hand_'+s][1],REST['hand_'+s][1])
            cs=p.to_cs(pose);target=p.add(cs['hand_'+s][1],p.mul(delta,fp_weight))
            if fp_weight>0.:fit_punch_shoulder(view,s,target)
            vcs=p.to_cs(view)
            target=elbow_clearance(vcs,s,target)
            view_rotation=p.qmul(FP_REST['hand_'+s][0],p.qmul(p.qconj(REST['hand_'+s][0]),cs['hand_'+s][0]))
            rotation=p._nlerp(cs['hand_'+s][0],view_rotation,fp_weight)
            pole=p.sub(vcs['lowerarm_'+s][1],vcs['upperarm_'+s][1])
            if s!=side:
                engaged=ease(t/.12)*(1.-ease((t-(length-.2))/.2))
                pole=p.add(pole,[18.*(1. if s=='l' else -1.)*engaged,0.,0.])
            repair_chain(view,'upperarm_'+s,'lowerarm_'+s,'hand_'+s,target,pole,rotation)
            wrist=g.wrist(p.to_cs(view),s)
            if wrist>75. and fp_weight>0.:straight_wrist(view,s,(wrist-75.)/wrist)
        views.append(follow_ik(view))
    rows[0]=copy.deepcopy(STANCE);rows[-1]=copy.deepcopy(STANCE)
    views[0]=copy.deepcopy(FP_STANCE);views[-1]=copy.deepcopy(FP_STANCE)
    result={}
    for first,poses in [(False,rows),(True,views)]:
        name=f'RPG_{"FP_" if first else ""}Punch_{side.upper()}_{TIERS[tier]}_V{variant+1}'
        result[name]=dict(name=name,fps=FPS,frames=len(poses),bones=p.BONES,poses=poses,impact=impact,
            keys=sorted(set([a for a,_ in keys]+[max(0.,impact-.07),impact+.05])),hand=side,tier=TIERS[tier],variant=variant,
            charge_switch=.12,hold_start=.6,hold_end=1.2,source='IMG_8630.mov; captured punch body motion with graded extension and small variants')
    return result


if __name__=='__main__':
    assert p.SP.NAME=='uefn','Use RPG_SKEL=uefn'
    old=p.SOFT_IK,p.SOFT_IK_ARM;p.SOFT_IK=p.SOFT_IK_ARM=1.e-7
    try:
        clips={}
        for side in 'lr':
            for tier in range(3):
                for variant in range(3):clips.update(build(side,tier,variant))
    finally:p.SOFT_IK,p.SOFT_IK_ARM=old
    (HERE/'capture_punches8630_uefn.json').write_text(json.dumps(clips))
    from preview import render
    for name,clip in clips.items():
        if not name.startswith('RPG_FP_'):render(clip,str(HERE/'previews'/f'{name}.png'),clip['keys'])
    print('CAPTURED PUNCHES',len(clips),'clips; 3 variants x 2 hands x 3 tiers x 2 views')
