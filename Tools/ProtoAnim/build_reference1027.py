"""Player-only review assets from the reference fit. Leaves approved gameplay clips intact.

The paired review preserves the visible filmed sequence. Single strikes adapt its
boundaries for RT/LT release controls; no automatic second punch or defensive weave.
"""
import copy,json,math
from pathlib import Path
import protoanim as p
import getupcheck as g
from capture_getups import repair_chain,follow_ik
from capture_punches8630 import fit_punch_shoulder,elbow_clearance

HERE=Path(__file__).resolve().parent;FPS=60
FIT=json.loads((HERE/'captures/reference1027_fitted_poses.json').read_text())
POSE={r['frame']:r['pose'] for r in FIT['rows']}
STANCE=POSE[128]
REFERENCE_FIST=copy.deepcopy(p.FIST)
for bone,(rotation,translation) in list(REFERENCE_FIST.items()):
    # The older probe's rear MCP joints are only partly curled (index38deg).
    # Keep its curl axis/thumb placement, but close the four fingers around the palm.
    if any(bone.startswith(f+'_') for f in ('index','middle','ring','pinky')) and bone.split('_')[-2] in ('01','02','03'):
        delta=p.qmul(p.qconj(p.REF[bone][0]),rotation)
        if delta[3]<0:delta=[-v for v in delta]
        angle=math.degrees(2.*math.acos(max(-1.,min(1.,delta[3]))))
        desired=max(angle,{'01':80.,'02':95.,'03':65.}[bone.split('_')[-2]])
        REFERENCE_FIST[bone]=(p.qnorm(p.qmul(p.REF[bone][0],p.qaxis(delta[:3],desired))),translation)

def ease(t):
    t=max(0.,min(1.,t));return t*t*t*(10.+t*(-15.+6.*t))

def blend(a,b,u):
    return {bone:(p._nlerp(a[bone][0],b[bone][0],u),p.lerp(a[bone][1],b[bone][1],u)) for bone in p.BONES}

def at(keys,t):
    if t<=keys[0][0]:return copy.deepcopy(keys[0][1])
    for (ta,a),(tb,b) in zip(keys,keys[1:]):
        if ta<=t<=tb:return blend(a,b,ease((t-ta)/(tb-ta)))
    return copy.deepcopy(keys[-1][1])

def straight_wrist(local,s):
    cs=p.to_cs(local);hand='hand_'+s;parent=p.PARENT[hand]
    finger=p.norm(p.sub(cs['middle_01_'+s][1],cs[hand][1]));fore=p.norm(p.sub(cs[hand][1],cs['lowerarm_'+s][1]))
    turn=p.qnorm(p.cross(finger,fore)+[1.+p.dot(finger,fore)])
    local[hand]=(p.qnorm(p.qmul(p.qconj(cs[parent][0]),p.qmul(turn,cs[hand][0]))),local[hand][1])

def lower_for_contacts(local,targets):
    """Let the knees bend; never satisfy a planted-foot solve by lifting an unreachable shoe."""
    cs=p.to_cs(local);down=0.
    for s,target in targets.items():
        hip=cs['thigh_'+s][1]
        reach=(p.length(local['calf_'+s][1])+p.length(local['foot_'+s][1]))*.97
        horizontal=(hip[0]-target[0])**2+(hip[1]-target[1])**2
        vertical=math.sqrt(max(0.,reach*reach-horizontal))
        down=max(down,hip[2]-target[2]-vertical)
    if down>0.:local['pelvis']=(local['pelvis'][0],p.add(local['pelvis'][1],[0.,0.,-down]))

def planted_leg(local,s,target,rotation):
    cs=p.to_cs(local);hip=cs['thigh_'+s][1]
    pole=p.sub(cs['calf_'+s][1],hip);axis=p.norm(p.sub(target,hip))
    original=copy.deepcopy(local);best=None
    for angle in [0]+[sign*a for a in range(5,181,5) for sign in (1,-1)]:
        candidate=copy.deepcopy(original)
        repair_chain(candidate,'thigh_'+s,'calf_'+s,'foot_'+s,target,p.qrot(p.qaxis(axis,angle),pole),rotation)
        posed=p.to_cs(candidate);flex,abd=g.limb_angles(posed,s,'pelvis','thigh_'+s,'calf_'+s)
        ankle=g.ankle(posed,s)-g.IDLE_ANKLE[s]
        cost=max(0.,abd-48.)+max(0.,-28.-abd)+max(0.,flex-128.)+max(0.,-23.-flex)+max(0.,ankle-33.)+max(0.,-48.-ankle)
        if best is None or cost<best[0]:best=(cost,candidate)
        if cost<1.e-5:
            # Refine the first feasible direction, avoiding a visible 5-degree knee step.
            if angle:
                low,high=abs(angle)-5.,float(abs(angle));sign=1. if angle>0 else -1.
                for _ in range(10):
                    middle=(low+high)*.5;trial=copy.deepcopy(original)
                    repair_chain(trial,'thigh_'+s,'calf_'+s,'foot_'+s,target,p.qrot(p.qaxis(axis,sign*middle),pole),rotation)
                    tcs=p.to_cs(trial);tf,ta=g.limb_angles(tcs,s,'pelvis','thigh_'+s,'calf_'+s);tk=g.ankle(tcs,s)-g.IDLE_ANKLE[s]
                    if -28.<=ta<=48. and -23.<=tf<=128. and -48.<=tk<=33.:high=middle;best=(0.,trial)
                    else:low=middle
            break
    local.update(best[1])

def finish(local,planted=False):
    local['root']=([0.,0.,0.,1.],[0.,0.,0.])
    # Automatic source finger retargeting leaves the rear hand partly open.
    # Use the established target-rig fist for every guard, windup and strike frame.
    for bone,(rotation,_) in REFERENCE_FIST.items():local[bone]=(list(rotation),p.REF[bone][1])
    rest=p.to_cs(STANCE)
    if planted:
        cs=p.to_cs(local);targets={}
        for s in 'lr':
            target=p.sub(rest['ball_'+s][1],p.qrot(cs['foot_'+s][0],local['ball_'+s][1]))
            target[2]=max(4.5,target[2]);targets[s]=target
        lower_for_contacts(local,targets)
    for s in 'lr':
        cs=p.to_cs(local);foot='foot_'+s;toe='ball_'+s
        target=list(cs[foot][1]);rot=cs[foot][0]
        if planted:
            # Lock the ball to the stance footprint while retaining the captured heel pivot.
            ball=rest[toe][1];offset=p.qrot(rot,local[toe][1]);target=p.sub(ball,offset)
        target[2]+=max(0.,3.2-cs[toe][1][2],4.5-target[2]) if not planted else max(0.,4.5-target[2])
        if planted:planted_leg(local,s,target,rot)
        else:repair_chain(local,'thigh_'+s,'calf_'+s,foot,target,p.sub(cs['calf_'+s][1],cs['thigh_'+s][1]),rot)
        cs=p.to_cs(local)
        repair_chain(local,'upperarm_'+s,'lowerarm_'+s,'hand_'+s,cs['hand_'+s][1],p.sub(cs['lowerarm_'+s][1],cs['upperarm_'+s][1]),cs['hand_'+s][0])
        # Retain source fist roll, remove incidental wrist bending after interpolation.
        if g.wrist(p.to_cs(local),s)>55.:straight_wrist(local,s)
    return follow_ik(local)

def clip(name,rows,keys,**info):
    return dict(name=name,bones=p.BONES,fps=FPS,frames=len(rows),poses=rows,keys=keys,
        source='Visible player punches in ScreenRecording_10-27-2026 05-26-11_1.mov; manual 2D fit with RamsterZ Combo03 3D prior',
        staging=True,certified_accuracy=False,**info)

def view_arm(local,s,target,rotation,pole):
    """Choose the nearest elbow plane that keeps the camera-fit arm outside the torso/head."""
    original=copy.deepcopy(local);cs=p.to_cs(local);axis=p.norm(p.sub(target,cs['upperarm_'+s][1]));best=None
    for angle in [0]+[sign*a for a in range(3,181,3) for sign in (1,-1)]:
        trial=copy.deepcopy(original)
        repair_chain(trial,'upperarm_'+s,'lowerarm_'+s,'hand_'+s,target,p.qrot(p.qaxis(axis,angle),pole),rotation)
        if g.wrist(p.to_cs(trial),s)>55.:straight_wrist(trial,s)
        tcs=p.to_cs(trial);flex,abd=g.limb_angles(tcs,s,'spine_05','upperarm_'+s,'lowerarm_'+s)
        cost=max(0.,-53.-flex)+max(0.,flex-173.)+(max(0.,-38.-abd) if abs(flex)<110. else 0.)
        for pair,over in g.overlaps(tcs).items():
            if any(b in pair for b in ['uarm_'+s,'farm_'+s,'hand_'+s]):
                cost+=max(0.,over-max(0.,g.IDLE_OVER[pair])-1.)*3.
        if best is None or cost<best[0]:best=(cost,trial)
        if cost<1.e-5:break
    local.update(best[1])

def camera_torso(local,weight):
    # A forward camera cannot see fists held against the60-degree bladed chest.
    # Square the presentation torso in guard; return exactly
    # to the body performance before the strike/hit interval.
    for bone,share in [('spine_01',.2),('spine_03',.3),('spine_05',.5)]:
        cs=p.to_cs(local);q=p.qmul(p.qaxis([0.,0.,1.],-55.*weight*share),cs[bone][0])
        local[bone]=(p.qnorm(p.qmul(p.qconj(cs[p.PARENT[bone]][0]),q)),local[bone][1])

def first_person(local,rest,view_rest,t=0.,side=None):
    view=copy.deepcopy(local);rcs=p.to_cs(rest);vcs0=p.to_cs(view_rest)
    presentation=1. if side is None else 1.-ease(t/.08) if t<.36 else ease((t-.66)/.39)
    camera_torso(view,presentation)
    for s in 'lr':
        cs=p.to_cs(local)
        weight=presentation
        target=p.add(cs['hand_'+s][1],p.mul(p.sub(vcs0['hand_'+s][1],rcs['hand_'+s][1]),weight))
        if weight>0.:fit_punch_shoulder(view,s,target)
        vs=p.to_cs(view);target=elbow_clearance(vs,s,target)
        relative=p.qmul(p.qconj(rcs['hand_'+s][0]),cs['hand_'+s][0])
        rotation=p._nlerp(cs['hand_'+s][0],p.qmul(vcs0['hand_'+s][0],relative),weight)
        view_arm(view,s,target,rotation,p.sub(vs['lowerarm_'+s][1],vs['upperarm_'+s][1]))
    return follow_ik(view)

def build():
    global STANCE
    assert p.SP.NAME=='uefn'
    p.SOFT_IK=p.SOFT_IK_ARM=1.e-7
    STANCE=finish(copy.deepcopy(STANCE))
    # The filmed front shoe is partly occluded: its annotated toe must not become
    # an elevated ankle. Establish a real ground-contact footprint before isolating attacks.
    targets={}
    for s in 'lr':
        cs=p.to_cs(STANCE);foot='foot_'+s;ball='ball_'+s
        rot=cs[foot][0]
        # Hidden rear-foot depth is underdetermined by the crop. Use the native
        # stagger as a prior, rather than the image fit's overlong63cm footprint.
        ball_target=[12.,30.,3.2] if s=='l' else [-20.,-18.,3.2]
        target=p.sub(ball_target,p.qrot(rot,STANCE[ball][1]))
        target[2]=max(4.5,target[2]);targets[s]=target
    lower_for_contacts(STANCE,targets)
    for s,target in targets.items():
        cs=p.to_cs(STANCE);foot='foot_'+s;rot=cs[foot][0]
        repair_chain(STANCE,'thigh_'+s,'calf_'+s,foot,target,p.sub(cs['calf_'+s][1],cs['thigh_'+s][1]),rot)
    follow_ik(STANCE)
    result={}
    stance_rows=[]
    for i in range(121):
        pose=copy.deepcopy(STANCE)
        # Quiet stationary breathing only, not a weave or an invented attack.
        amount=.22*math.sin(2.*math.pi*i/120.)**2
        pose['pelvis']=(pose['pelvis'][0],p.add(pose['pelvis'][1],[0.,0.,amount]))
        stance_rows.append(finish(pose,True))
    STANCE=stance_rows[0];stance_rows[-1]=copy.deepcopy(STANCE)
    name='RPG_CombatStance_Reference1027';result[name]=clip(name,stance_rows,[0.,.5,1.,1.5,2.])
    view_rest=copy.deepcopy(STANCE)
    camera_torso(view_rest,1.)
    for s in 'lr':
        # The gameplay camera only partially follows the animated skull. A full
        # skull-relative offset hides both fists below the frame during this crouch.
        target=[12.,55.,154.] if s=='l' else [-23.,55.,156.]
        fit_punch_shoulder(view_rest,s,target);cs=p.to_cs(view_rest);target=elbow_clearance(cs,s,target)
        view_arm(view_rest,s,target,cs['hand_'+s][0],p.sub(cs['lowerarm_'+s][1],cs['upperarm_'+s][1]))
    view_rest=follow_ik(view_rest)
    views=[first_person(row,STANCE,view_rest) for row in stance_rows]
    view_rest=views[0];views[-1]=copy.deepcopy(view_rest)
    name='RPG_FP_CombatStance_Reference1027';result[name]=clip(name,views,[0.,.5,1.,1.5,2.],presentation='Camera guard fitted for visible fists; reference is third-person only, so this is a presentation adaptation')
    # Keep the original reference pair separate from gameplay single-strike adaptations.
    pairkeys=[((f-72)/60.,POSE[f]) for f in POSE]
    rows=[finish(at(pairkeys,i/FPS)) for i in range(57)]
    name='RPG_Reference1027_PairReview';result[name]=clip(name,rows,[0.,8/60,16/60,20/60,24/60,28/60,40/60,56/60])
    for side in 'lr':
        if side=='l':keys=[(0.,STANCE),(.12,POSE[72]),(.20,POSE[76]),(.36,POSE[80]),(.43,POSE[84]),(.82,STANCE),(1.05,STANCE)]
        else:
            load=copy.deepcopy(POSE[88])
            # A single RT must not also throw the preceding filmed left jab.
            for b in p.BONES:
                if b.endswith('_l') and any(t in b for t in ('clavicle','arm','hand','index','middle','ring','pinky','thumb')):load[b]=copy.deepcopy(STANCE[b])
            keys=[(0.,STANCE),(.12,load),(.24,POSE[92]),(.36,POSE[96]),(.44,POSE[100]),(.54,POSE[104]),(.72,POSE[112]),(1.05,STANCE)]
        for variant,offset in enumerate([0.,1.,-1.],1):
            rows=[]
            for i in range(64):
                t=i/FPS
                # All variants share their .12 s loading boundary for smooth charge-tier handoff.
                # Slight acceleration/recovery timing changes keep the same strike and impact pose.
                # Spatial offsets near a fully lateral shoulder can choose a different elbow solution.
                phase=math.sin(math.pi*(t-.12)/.24)**2 if .12<t<.36 else math.sin(math.pi*(t-.36)/.69)**2 if .36<t<1.05 else 0.
                pose=at(keys,t+offset*.004*phase)
                # The source advances through the jab/cross. Keep a documented body drive
                # when removing capsule/root travel, with planted feet owning the support.
                drive=ease((t-.12)/.24)*(1.-ease((t-.48)/.5))
                pose['pelvis']=(pose['pelvis'][0],p.add(pose['pelvis'][1],[0.,20.*drive,0.]))
                rows.append(finish(pose,True))
            rows[0]=copy.deepcopy(STANCE);rows[-1]=copy.deepcopy(STANCE)
            name=f'RPG_Punch_{side.upper()}_Light_Reference1027_V{variant}'
            result[name]=clip(name,rows,[a for a,_ in keys],impact=.36,hand=side,tier='Light',variant=variant-1,
                             adaptation='Isolated strike with common guard endpoints, planted-ball pivots and20cm in-place body drive replacing source root advance')
            views=[first_person(row,STANCE,view_rest,i/FPS,side) for i,row in enumerate(rows)]
            views[0]=copy.deepcopy(view_rest);views[-1]=copy.deepcopy(view_rest)
            name=name.replace('RPG_Punch_','RPG_FP_Punch_')
            result[name]=clip(name,views,[a for a,_ in keys],impact=.36,hand=side,tier='Light',variant=variant-1,
                             presentation='Camera guard outside the hit interval; shared striking-hand path through impact and initial withdrawal')
    (HERE/'reference1027_uefn.json').write_text(json.dumps(result))
    print('REFERENCE REVIEW ASSETS',len(result),flush=True)
    for name,c in result.items():
        if 'V2' in name or 'V3' in name:continue
        g.check_clip(name,c)

if __name__=='__main__':build()
