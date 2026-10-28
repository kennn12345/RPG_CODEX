"""Player-only review assets from the reference fit. Leaves approved gameplay clips intact.

The paired review preserves the visible filmed sequence. Single strikes adapt its
boundaries for RT/LT release controls; no automatic second punch or defensive weave.
"""
import copy,json,math,os
from pathlib import Path
import protoanim as p
import getupcheck as g
from capture_getups import repair_chain,follow_ik
from capture_punches8630 import fit_punch_shoulder,elbow_clearance

HERE=Path(__file__).resolve().parent;FPS=60
FIT=json.loads((HERE/'captures/reference1027_fitted_poses.json').read_text())
POSE={r['frame']:r['pose'] for r in FIT['rows']}
REFINED=HERE/'captures/reference1027_refined_upper_body.json'
if REFINED.exists():
    for row in json.loads(REFINED.read_text())['rows']:
        for bone,q in row['rotations'].items():POSE[row['frame']][bone]=(q,POSE[row['frame']][bone][1])
SUPPORT=HERE/'captures/reference1027_refined_support.json'
SUPPORT_PARAMETERS=json.loads(SUPPORT.read_text())['parameters'] if SUPPORT.exists() else [12.,30.,-20.,-18.,0.,0.,0.]
for pose in POSE.values():pose['pelvis']=(pose['pelvis'][0],p.add(pose['pelvis'][1],SUPPORT_PARAMETERS[4:7]))
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
    if u<=0.:return copy.deepcopy(a)
    if u>=1.:return copy.deepcopy(b)
    local={bone:(p._nlerp(a[bone][0],b[bone][0],u),p.lerp(a[bone][1],b[bone][1],u)) for bone in p.BONES}
    # Independent local-bone interpolation sweeps a fist around the shoulder
    # and can rotate the elbow through a different hinge branch between keys.
    # Interpolate the intended hand/elbow path, then solve a fixed-length hinge.
    ac,bc=p.to_cs(a),p.to_cs(b);body=p.to_cs(local)['spine_05']
    def body_point(cs,bone):
        q,t=cs['spine_05'];return p.qrot(p.qconj(q),p.sub(cs[bone][1],t))
    for s in 'lr':
        hand='hand_'+s;upper='upperarm_'+s;lower='lowerarm_'+s
        target=p.add(body[1],p.qrot(body[0],p.lerp(body_point(ac,hand),body_point(bc,hand),u)))
        elbow=p.add(body[1],p.qrot(body[0],p.lerp(body_point(ac,lower),body_point(bc,lower),u)))
        rotation=p._nlerp(ac[hand][0],bc[hand][0],u)
        repair_reference_arm(local,s,target,p.sub(elbow,p.to_cs(local)[upper][1]),rotation)
    return local

def level_bank(local,bone,limit):
    cs=p.to_cs(local);left,forward,_=g.frame(cs,bone)
    bank=math.degrees(math.asin(max(-1.,min(1.,left[2]))))
    if abs(bank)<=limit:return
    level=p.norm(p.cross(forward,[0.,0.,1.]));turn=p.qnorm(p.cross(left,level)+[1.+p.dot(left,level)])
    turn=p._nlerp([0.,0.,0.,1.],turn,1.-limit/abs(bank))
    world=p.qmul(turn,cs[bone][0]);parent=cs[p.PARENT[bone]][0]
    local[bone]=(p.qnorm(p.qmul(p.qconj(parent),world)),local[bone][1])

def side_lean(cs):
    """Sideways (toward the chest's right) lean of the hips-to-chest line, degrees, and the chest's level facing."""
    d=p.norm(p.sub(cs['spine_05'][1],cs['pelvis'][1]));left,forward,_=g.frame(cs,'spine_05')
    fh=p.norm([forward[0],forward[1],0.]);lh=p.norm([left[0],left[1],0.])
    return math.degrees(math.atan2(-p.dot(d,lh),d[2])),fh

# The fit leaves the chest sitting well off to one side of the hips (13.6 deg in guard, the RamsterZ mocap 3.6):
# the unlevel stance. Keep the hips-to-chest line within SIDE_LEAN across the chest. (Forward lean along the chest's
# facing or the camera's line of sight both pulled the head off the filmed head: the marks hold it.)
SIDE_LEAN=4.

def posture(local):
    cs=p.to_cs(local);side,fh=side_lean(cs)
    roll=max(-SIDE_LEAN,min(SIDE_LEAN,side))-side   # set, not add: finish() may run on a pose more than once
    if abs(roll)<.05:return
    # The lower spine rolls the chest back over the hips and the upper chest turns back by as much, so the chest
    # moves across without banking. (About +forward, + turns up toward the left.)
    for bone,share in [('spine_01',.5),('spine_03',.5),('spine_05',-1.)]:
        cs=p.to_cs(local);q=p.qmul(p.qaxis(fh,-roll*share),cs[bone][0])
        local[bone]=(p.qnorm(p.qmul(p.qconj(cs[p.PARENT[bone]][0]),q)),local[bone][1])

def repair_reference_arm(local,s,target,pole,rotation):
    # A punch can look fully extended without entering the straight-chain
    # singularity that makes its elbow plane/mesh roll undefined.
    upper,lower,end='upperarm_'+s,'lowerarm_'+s,'hand_'+s;cs=p.to_cs(local)
    a,b=p.length(local[lower][1]),p.length(local[end][1])
    minimum=math.sqrt(a*a+b*b+2.*a*b*math.cos(math.radians(144.)))
    delta=p.sub(target,cs[upper][1]);distance=p.length(delta)
    if distance<minimum:target=p.add(cs[upper][1],p.mul(p.norm(delta),minimum))
    p.two_bone(cs,local,upper,lower,end,target,pole,end_rot=rotation,max_reach=.998)
    for bone in [upper,lower,end]:
        local[bone]=(p.qnorm(p.qmul(p.qconj(cs[p.PARENT[bone]][0]),cs[bone][0])),local[bone][1])

# A raised elbow rotates the shoulder blade up and lifts the shoulder: the filmed cross cocks its right elbow up level
# with the head before driving. So beyond guard the cap grows with the right elbow's height above its shoulder, from
# 4 cm (elbow 10 cm below) to 4 + COCKED_RISE (elbow 10 cm above); a low elbow - guard, the extension - stays capped.
COCKED_RISE=float(os.environ.get('RPG_REF_COCKED_RISE','8'))

def shoulder_cap(local,guard):
    """How far the right shoulder may sit above the left (cm)."""
    if guard:return 1.5
    cs=p.to_cs(local);up=cs['lowerarm_r'][1][2]-cs['upperarm_r'][1][2]
    return 4.+COCKED_RISE*min(1.,max(0.,(up+10.)/20.))

def settle_right_shoulder(local,limit):
    cs=p.to_cs(local);excess=cs['upperarm_r'][1][2]-cs['upperarm_l'][1][2]-limit
    if excess<=0.:return
    clav='clavicle_r';v=p.sub(cs['upperarm_r'][1],cs[clav][1]);length=p.length(v)
    z=max(-length*.95,min(length*.95,v[2]-excess));horizontal=p.norm([v[0],v[1],0.])
    wanted=p.add(p.mul(horizontal,math.sqrt(max(0.,length*length-z*z))),[0.,0.,z])
    a,b=p.norm(v),p.norm(wanted);turn=p.qnorm(p.cross(a,b)+[1.+p.dot(a,b)])
    q=p.qmul(turn,cs[clav][0]);parent=cs[p.PARENT[clav]][0]
    target,rotation=cs['hand_r'][1],cs['hand_r'][0];elbow=cs['lowerarm_r'][1]
    local[clav]=(p.qnorm(p.qmul(p.qconj(parent),q)),local[clav][1])
    now=p.to_cs(local);view_arm(local,'r',target,rotation,p.sub(elbow,now['upperarm_r'][1]))

def key_progress(times,stops,t):
    """Fractional key index at time t on one C1 curve through the keys: zero speed only at `stops` (where the filmed
    motion turns round: guard, the back of a wind-up, full extension), passing through every other key at speed.
    Easing every key to rest stopped the fist mid-swing at each in-between key (stop-and-go)."""
    n=len(times);slopes=[1./(times[k+1]-times[k]) for k in range(n-1)]
    tangent=[0.]*n
    for k in range(1,n-1):
        if times[k] in stops:continue
        a,b=slopes[k-1],slopes[k];ha,hb=times[k]-times[k-1],times[k+1]-times[k]
        tangent[k]=3.*(ha+hb)/((2.*hb+ha)/a+(hb+2.*ha)/b)   # Fritsch-Carlson weighted harmonic mean: monotone
    for k in range(n-1):
        if times[k]<=t<=times[k+1]:
            h=times[k+1]-times[k];u=(t-times[k])/h
            h00,h10,h01,h11=2*u**3-3*u**2+1,u**3-2*u**2+u,-2*u**3+3*u**2,u**3-u**2
            return k+(h10*h*tangent[k]+h01+h11*h*tangent[k+1])   # (h00 multiplies 0: progress within segment k)
    return float(n-1)

def at(keys,t,stops=None):
    if t<=keys[0][0]:return copy.deepcopy(keys[0][1])
    if t>=keys[-1][0]:return copy.deepcopy(keys[-1][1])
    if stops is None:
        for (ta,a),(tb,b) in zip(keys,keys[1:]):
            if ta<=t<=tb:return blend(a,b,ease((t-ta)/(tb-ta)))
    s=key_progress([k[0] for k in keys],stops,t);i=min(int(s),len(keys)-2)
    return blend(keys[i][1],keys[i+1][1],min(1.,max(0.,s-i)))

def straight_wrist(local,s):
    cs=p.to_cs(local);hand='hand_'+s;parent=p.PARENT[hand]
    finger=p.norm(p.sub(cs['middle_01_'+s][1],cs[hand][1]));fore=p.norm(p.sub(cs[hand][1],cs['lowerarm_'+s][1]))
    turn=p.qnorm(p.cross(finger,fore)+[1.+p.dot(finger,fore)])
    local[hand]=(p.qnorm(p.qmul(p.qconj(cs[parent][0]),p.qmul(turn,cs[hand][0]))),local[hand][1])

# The user's guard (2026-10-28): almost the standing idle. Shoulders square with the lead side barely forward, feet
# about shoulder width with the lead foot barely ahead, a very slight knee bend. The arms are left as they are.
# (Facing +Y, left +X; a yaw above 90 turns the lead side forward.)
STANCE_PELVIS_YAW=98.;STANCE_CHEST_YAW=102.;STANCE_LEAN=6.;TURN_GAIN=1.5
STANCE_KNEE=40.;MIN_KNEE=8.   # lower_for_contacts used to hold every planted knee at ~28 deg: the old crouch
FEET_HALF_WIDTH=20.;FEET_STAGGER=12.;ANKLE_AHEAD=2.;TOE_YAW={'l':84.,'r':100.}
DRIVE=.4   # the filmed hip drive went between feet 78 cm apart; over these feet it would pitch the body past the lead foot

def yaw_of(v):return math.degrees(math.atan2(v[1],v[0]))

def toe_yaw(cs,s):return yaw_of(p.sub(cs['ball_'+s][1],cs['foot_'+s][1]))

def set_world(local,bone,world):
    cs=p.to_cs(local);local[bone]=(p.qnorm(p.qmul(p.qconj(cs[p.PARENT[bone]][0]),world)),local[bone][1])

def turn_world(local,bone,angle):
    set_world(local,bone,p.qmul(p.qaxis([0.,0.,1.],angle),p.to_cs(local)[bone][0]))

def square_up(local,old):
    """Re-base a filmed key on the squared guard. Turns between square and the filmed guard's blade scale onto
    square..the new guard; a turn past square the other way (the cross) stays; a turn beyond the filmed blade (the
    jab) keeps its turn past the guard, the chest's by TURN_GAIN: from square the shoulder must turn further to
    reach as far and to clear the face. The feet keep their filmed pivot relative to the guard."""
    cs=p.to_cs(local);feet={s:cs['foot_'+s][0] for s in 'lr'}
    # the fists stay where they were filmed (a turned chest must not turn the strike off its line)
    arms=arms_of(cs)
    def remap(y,o,new,gain):
        if y<=90.:return y
        if y<=o:return 90.+(y-90.)*(new-90.)/(o-90.)
        return new+(y-o)*gain
    yp,yc=yaw_of(g.frame(cs,'pelvis')[1]),yaw_of(g.frame(cs,'spine_05')[1])
    dp=remap(yp,old['pelvis'],STANCE_PELVIS_YAW,1.)-yp
    dc=remap(yc,old['chest'],STANCE_CHEST_YAW,TURN_GAIN)-yc
    turn_world(local,'pelvis',dp)
    for bone in ('spine_01','spine_03'):turn_world(local,bone,(dc-dp)*.5)
    for s in 'lr':set_world(local,'foot_'+s,p.qmul(p.qaxis([0.,0.,1.],old['feet'][s]),feet[s]))
    hold_arms(local,arms)

def arms_of(cs):return {s:(cs['hand_'+s][1],cs['hand_'+s][0],cs['lowerarm_'+s][1]) for s in 'lr'}

def hold_arms(local,arms):
    """A fist out on its strike line stays there; a fist tucked in guard turns with the chest (the rear fist was filmed
    at the chin behind a bladed chest: held in place it would sit inside a squared one). Blended by the arm's reach."""
    cs=p.to_cs(local)
    for s,(hand,rotation,elbow) in arms.items():
        upper=cs['upperarm_'+s][1];full=p.length(local['lowerarm_'+s][1])+p.length(local['hand_'+s][1])
        w=max(0.,min(1.,(p.length(p.sub(hand,upper))/full-.6)/.3))
        hand=p.lerp(cs['hand_'+s][1],hand,w);elbow=p.lerp(cs['lowerarm_'+s][1],elbow,w)
        rotation=p._nlerp(cs['hand_'+s][0],rotation,w)
        repair_reference_arm(local,s,hand,p.sub(elbow,p.to_cs(local)['upperarm_'+s][1]),rotation)
        view_arm(local,s,hand,rotation,None)

def stand_up(local):
    """Bring the guard's hunch back to STANCE_LEAN through the spine, keeping the head's gaze and the fists."""
    arms=arms_of(p.to_cs(local))
    for _ in range(3):
        cs=p.to_cs(local);d=p.sub(cs['neck_01'][1],cs['pelvis'][1])
        back=math.degrees(math.atan2(math.hypot(d[0],d[1]),d[2]))-STANCE_LEAN
        if back<=.1:break
        axis=p.norm(p.cross(d,[0.,0.,1.]));head=cs['head'][0]
        for bone,share in [('spine_01',.4),('spine_03',.3),('spine_05',.3)]:
            set_world(local,bone,p.qmul(p.qaxis(axis,back*share),p.to_cs(local)[bone][0]))
        set_world(local,'head',head)
    hold_arms(local,arms)

def leg_reach(local,s,knee):
    a,b=p.length(local['calf_'+s][1]),p.length(local['foot_'+s][1])
    return math.sqrt(a*a+b*b+2.*a*b*math.cos(math.radians(knee)))

def lower_for_contacts(local,targets):
    """Let the knees bend; never satisfy a planted-foot solve by lifting an unreachable shoe."""
    cs=p.to_cs(local);down=0.
    for s,target in targets.items():
        hip=cs['thigh_'+s][1]
        reach=leg_reach(local,s,MIN_KNEE)
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

def finish(local,planted=False,guard=False):
    local['root']=([0.,0.,0.,1.],[0.,0.,0.])
    # Automatic source finger retargeting leaves the rear hand partly open.
    # Use the established target-rig fist for every guard, windup and strike frame.
    for bone,(rotation,_) in REFERENCE_FIST.items():local[bone]=(list(rotation),p.REF[bone][1])
    cs=p.to_cs(local);foot_rotations={s:cs['foot_'+s][0] for s in 'lr'}
    posture(local)
    level_bank(local,'pelvis',.5 if guard else 3.)
    level_bank(local,'spine_05',.5 if guard else 5.)
    for s,q in foot_rotations.items():
        cs=p.to_cs(local);foot='foot_'+s
        local[foot]=(p.qnorm(p.qmul(p.qconj(cs[p.PARENT[foot]][0]),q)),local[foot][1])
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
    if planted:
        cs=p.to_cs(local)
        extension=min(g.limb_angles(cs,s,'pelvis','thigh_'+s,'calf_'+s)[0] for s in 'lr')
        if extension < -23.:
            # A long cross comes from a small pelvic hinge as well as the arm.
            # Keep the filmed upper-body world orientation while letting the
            # pelvis follow the drive, rather than hyperextending the rear hip.
            tilt=min(12.,-23.-extension);spine=cs['spine_01'][0]
            feet={s:(cs['foot_'+s][0],cs['foot_'+s][1]) for s in 'lr'}
            q=p.qnorm(p.qmul(p.qaxis(g.frame(cs,'pelvis')[0],-tilt),cs['pelvis'][0]))
            local['pelvis']=(q,local['pelvis'][1])
            local['spine_01']=(p.qnorm(p.qmul(p.qconj(q),spine)),local['spine_01'][1])
            lower_for_contacts(local,{s:v[1] for s,v in feet.items()})
            for s,(rotation,target) in feet.items():planted_leg(local,s,target,rotation)
    settle_right_shoulder(local,shoulder_cap(local,guard))
    for s in 'lr':
        cs=p.to_cs(local)
        view_arm(local,s,cs['hand_'+s][1],cs['hand_'+s][0],p.sub(cs['lowerarm_'+s][1],cs['upperarm_'+s][1]))
        if g.wrist(p.to_cs(local),s)>55.:straight_wrist(local,s)
    # the cap follows the elbow's height, and solving the arm can lower the elbow: hold the solved pose to its own cap
    for _ in range(4):
        cs=p.to_cs(local);cap=shoulder_cap(local,guard)
        if cs['upperarm_r'][1][2]-cs['upperarm_l'][1][2]<=cap+.03:break
        settle_right_shoulder(local,cap-.1)
    if g.wrist(p.to_cs(local),'r')>55.:straight_wrist(local,'r')
    return follow_ik(local)

def clip(name,rows,keys,**info):
    return dict(name=name,bones=p.BONES,fps=FPS,frames=len(rows),poses=rows,keys=keys,
        source='Visible player punches in ScreenRecording_10-27-2026 05-26-11_1.mov; manual 2D fit with RamsterZ Combo03 3D prior',
        staging=True,certified_accuracy=False,**info)

ELBOW_DEFINED=float(os.environ.get("RPG_REF_ELBOW_DEFINED","10"))

def view_arm(local,s,target,rotation,pole):
    """Choose the nearest elbow plane that keeps the camera-fit arm outside the torso/head."""
    original=copy.deepcopy(local);cs=p.to_cs(local);axis=p.norm(p.sub(target,cs['upperarm_'+s][1]));best=None
    # A fitted elbow can cross the reach axis between sparse keys. Seed the
    # hinge from an anatomical outside/down direction instead of that ambiguous
    # fitted pole, keeping the elbow outside the ribs through extension.
    left,forward,_=g.frame(cs,'spine_05')
    anatomical=p.add(p.add(p.mul(left,30. if s=='l' else -30.),p.mul(forward,-8.)),[0.,0.,-25.])
    # ...but where the arm is clearly bent its own elbow is well defined (the filmed cross cocks it up and out wide,
    # and the fixed outside/down seed tucked it, 100+ px off the filmed elbow): seed from the pose's elbow when it
    # sits ELBOW_DEFINED cm or more off the shoulder-wrist line, the anatomical direction only near full extension.
    elbow=p.sub(cs['lowerarm_'+s][1],cs['upperarm_'+s][1]);off=p.sub(elbow,p.mul(axis,p.dot(elbow,axis)))
    pole=elbow if p.length(off)>=ELBOW_DEFINED else anatomical
    for angle in [0]+[sign*a for a in range(3,181,3) for sign in (1,-1)]:
        trial=copy.deepcopy(original)
        repair_reference_arm(trial,s,target,p.qrot(p.qaxis(axis,angle),pole),rotation)
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
    available=max(0.,g.relative(p.to_cs(local),'spine_05','pelvis')[2]+37.)
    turn=min(55.*weight,available)
    for bone,share in [('spine_01',.2),('spine_03',.3),('spine_05',.5)]:
        cs=p.to_cs(local);q=p.qmul(p.qaxis([0.,0.,1.],-turn*share),cs[bone][0])
        local[bone]=(p.qnorm(p.qmul(p.qconj(cs[p.PARENT[bone]][0]),q)),local[bone][1])

def first_person(local,rest,view_rest,t=0.,side=None):
    view=copy.deepcopy(local);rcs=p.to_cs(rest);vcs0=p.to_cs(view_rest)
    presentation=1. if side is None else 1.-ease((t-.12)/.16) if t<.28 else ease((t-.78)/.27)
    if presentation==0.:return follow_ik(view)  # Exact shared performance through contact/withdrawal.
    camera_torso(view,presentation)
    for s in 'lr':
        cs=p.to_cs(local)
        weight=presentation
        target=p.add(cs['hand_'+s][1],p.mul(p.sub(vcs0['hand_'+s][1],rcs['hand_'+s][1]),weight))
        if side is not None and t<.28:
            # A third-person rear-hand load passes behind the eyes. Keep a readable
            # camera windup through the common charge boundary, then join the exact
            # body strike path before contact. The non-striking hand stays protective.
            loaded=list(vcs0['hand_'+s][1])
            if s==side:loaded=p.add(loaded,p.mul([3. if s=='l' else -3.,-10.,-2.],ease(t/.12)))
            target=p.lerp(cs['hand_'+s][1],loaded,weight)
        if weight>0.:fit_punch_shoulder(view,s,target)
        vs=p.to_cs(view);target=elbow_clearance(vs,s,target)
        relative=p.qmul(p.qconj(rcs['hand_'+s][0]),cs['hand_'+s][0])
        rotation=p._nlerp(cs['hand_'+s][0],p.qmul(vcs0['hand_'+s][0],relative),weight)
        view_arm(view,s,target,rotation,p.sub(vs['lowerarm_'+s][1],vs['upperarm_'+s][1]))
    settle_right_shoulder(view,shoulder_cap(view,side is None))
    return follow_ik(view)

def sampled(rows,t):
    f=max(0.,min(len(rows)-1,t*FPS));a=int(f);b=min(a+1,len(rows)-1)
    return blend(rows[a],rows[b],f-a)

def lean_forward(local,angle):
    for bone,share in [('spine_01',.2),('spine_03',.3),('spine_05',.5)]:
        cs=p.to_cs(local);q=p.qmul(p.qaxis([1.,0.,0.],-angle*share),cs[bone][0])
        local[bone]=(p.qnorm(p.qmul(p.qconj(cs[p.PARENT[bone]][0]),q)),local[bone][1])

def view_coil(coil,view_rest,side):
    view=copy.deepcopy(coil);camera_torso(view,1.);anchor=p.to_cs(view_rest)
    shift=p.mul(p.sub(p.to_cs(coil)['pelvis'][1],p.to_cs(STANCE)['pelvis'][1]),.3)
    for s in 'lr':
        target=p.add(anchor['hand_'+s][1],shift)
        if s==side:target=p.add(target,[3. if s=='l' else -3.,-10.,-2.])
        fit_punch_shoulder(view,s,target);cs=p.to_cs(view);target=elbow_clearance(cs,s,target)
        view_arm(view,s,target,anchor['hand_'+s][0],p.sub(cs['lowerarm_'+s][1],cs['upperarm_'+s][1]))
    return finish(view,True)

def charged_family(result,view_rest):
    """Unseen charged attacks are explicit style adaptations of the visible single strikes.

    Preserve the existing hold/release contract; grade the same body/arm action,
    rather than grafting an unrelated hook onto the new standard cross/jab.
    """
    for side in 'lr':
        standard=result[f'RPG_Punch_{side.upper()}_Light_Reference1027_V1']['poses']
        load=sampled(standard,.12);coil=sampled(standard,.226666667)
        coil['pelvis']=(coil['pelvis'][0],p.add(coil['pelvis'][1],[0.,-3.,-2.]))
        hand='hand_'+side
        coil=finish(coil,True)
        fp_coil=view_coil(coil,view_rest,side)
        fp_load=sampled(result[f'RPG_FP_Punch_{side.upper()}_Light_Reference1027_V1']['poses'],.12)
        for tier,strength,duration in [('Heavy',1.,2.2),('Power',2.,2.4)]:
            for variant,offset in enumerate([0.,1.,-1.],1):
                rows=[];views=[]
                for i in range(round(duration*FPS)+1):
                    t=i/FPS
                    if t<=.12:
                        pose=sampled(standard,t);phase=t
                    elif t<=1.2:
                        pose=blend(load,coil,ease((t-.12)/.48));phase=.12
                    else:
                        phase=.226666667+(.133333333*(t-1.2)/.3) if t<=1.5 else .36+(.30*(t-1.5)/.3) if t<=1.8 else .66+.39*(t-1.8)/(duration-1.8)
                        timing=math.sin(math.pi*(phase-.12)/.24)**2 if phase<.36 else math.sin(math.pi*(phase-.36)/.69)**2
                        pose=sampled(standard,phase+offset*.0025*timing)
                        if t<1.38:pose=blend(coil,pose,ease((t-1.2)/.18))
                        drive=ease((t-1.2)/.3)*(1.-ease((t-1.7)/(duration-1.7)))
                        original_height=p.to_cs(pose)[hand][1][2]
                        pose['pelvis']=(pose['pelvis'][0],p.add(pose['pelvis'][1],[0.,8.*strength*drive,0.]))
                        lean_forward(pose,6.*strength*drive)
                        pose=finish(pose,True)
                        extension=drive*(1.-ease((t-1.53)/.10))
                        if extension>1.e-6:
                            cs=p.to_cs(pose);target=p.add(cs[hand][1],[0.,4.*strength*extension,2.*strength*extension]);rotation=cs[hand][0]
                            # Knees/hips absorb the larger drive, but the fist still delivers
                            # the same head-height straight punch instead of dropping to the waist.
                            target[2]+=(original_height-cs[hand][1][2])*extension
                            fit_punch_shoulder(pose,side,target);cs=p.to_cs(pose);target=elbow_clearance(cs,side,target)
                            pole=p.sub(cs['lowerarm_'+side][1],cs['upperarm_'+side][1])
                            if side=='l':pole=p.add(pole,p.mul(g.frame(cs,'spine_05')[0],30.*extension))
                            view_arm(pose,side,target,rotation,pole)
                    pose=finish(pose,True);rows.append(pose)
                    if .12<t<=1.2:view=finish(blend(fp_load,fp_coil,ease((t-.12)/.48)),True)
                    else:
                        view=first_person(pose,STANCE,view_rest,phase,side)
                        if 1.2<t<1.4:view=finish(blend(fp_coil,view,ease((t-1.2)/.2)),True)
                    views.append(view)
                rows[0]=copy.deepcopy(STANCE);rows[-1]=copy.deepcopy(STANCE)
                views[0]=copy.deepcopy(view_rest);views[-1]=copy.deepcopy(view_rest)
                for first,poses in [(False,rows),(True,views)]:
                    name=f'RPG_{"FP_" if first else ""}Punch_{side.upper()}_{tier}_Reference1027_V{variant}'
                    result[name]=clip(name,poses,[0.,.12,.6,1.2,1.5,1.8,duration],impact=1.5,hand=side,tier=tier,variant=variant-1,
                        charge_switch=.12,hold_start=.6,hold_end=1.2,
                        adaptation='Charged style adaptation; not present in the short reference. Shared coil/hold, graded hip drive, torso extension and reach; same single-strike family.')

def build():
    global STANCE
    assert p.SP.NAME=='uefn'
    p.SOFT_IK=p.SOFT_IK_ARM=1.e-7
    raw=p.to_cs(POSE[128]);raw_height=raw['pelvis'][1][2]
    old=dict(pelvis=yaw_of(g.frame(raw,'pelvis')[1]),chest=yaw_of(g.frame(raw,'spine_05')[1]),
             feet={s:TOE_YAW[s]-toe_yaw(raw,s) for s in 'lr'})
    for pose in POSE.values():square_up(pose,old)
    stand_up(STANCE)
    STANCE=finish(copy.deepcopy(STANCE),guard=True)
    # A real ground-contact footprint under the hips (the filmed front shoe is partly occluded and the rear one
    # hidden), with the pelvis at the height that leaves STANCE_KNEE in the straighter leg.
    cs=p.to_cs(STANCE);centre=p.lerp(cs['thigh_l'][1],cs['thigh_r'][1],.5);targets={};poles={}
    for s,(across,ahead) in {'l':(FEET_HALF_WIDTH,FEET_STAGGER*.5),'r':(-FEET_HALF_WIDTH,-FEET_STAGGER*.5)}.items():
        foot='foot_'+s;ball='ball_'+s;rot=cs[foot][0]
        # ankles about the hips (the weight over mid-foot), the ball on the ground
        target=[centre[0]+across,centre[1]+ahead+ANKLE_AHEAD,max(4.5,3.2-p.qrot(rot,STANCE[ball][1])[2])];targets[s]=target
        toe=p.sub(cs[ball][1],cs[foot][1]);poles[s]=p.norm([toe[0],toe[1],0.])
    rise=sum(t[2]+math.sqrt(max(0.,leg_reach(STANCE,s,STANCE_KNEE)**2-(cs['thigh_'+s][1][0]-t[0])**2-(cs['thigh_'+s][1][1]-t[1])**2))
             -cs['thigh_'+s][1][2] for s,t in targets.items())*.5   # both knees about STANCE_KNEE
    STANCE['pelvis']=(STANCE['pelvis'][0],p.add(STANCE['pelvis'][1],[0.,0.,rise]))
    lower_for_contacts(STANCE,targets)
    for s,target in targets.items():
        cs=p.to_cs(STANCE);foot='foot_'+s;rot=cs[foot][0]
        repair_chain(STANCE,'thigh_'+s,'calf_'+s,foot,target,poles[s],rot)
    follow_ik(STANCE)
    # the strikes keep their filmed rise and fall about the new guard height
    height=p.to_cs(STANCE)['pelvis'][1][2]-raw_height
    for f,pose in POSE.items():
        if f!=128:pose['pelvis']=(pose['pelvis'][0],p.add(pose['pelvis'][1],[0.,0.,height]))
    result={}
    stance_rows=[]
    for i in range(121):
        pose=copy.deepcopy(STANCE)
        # Quiet stationary breathing only, not a weave or an invented attack.
        amount=.22*math.sin(2.*math.pi*i/120.)**2
        pose['pelvis']=(pose['pelvis'][0],p.add(pose['pelvis'][1],[0.,0.,amount]))
        stance_rows.append(finish(pose,True,guard=True))
    STANCE=stance_rows[0];stance_rows[-1]=copy.deepcopy(STANCE)
    POSE[128]=copy.deepcopy(STANCE)
    name='RPG_CombatStance_Reference1027';result[name]=clip(name,stance_rows,[0.,.5,1.,1.5,2.])
    view_rest=copy.deepcopy(STANCE)
    camera_torso(view_rest,1.)
    head=p.to_cs(view_rest)['head'];eye=p.add(head[1],p.qrot(head[0],[15.,13.,0.]))
    camera=eye  # Runtime blends its stationary reference anchor to this guard's real eyes.
    for s in 'lr':
        # The gameplay camera only partially follows the animated skull. A full
        # skull-relative offset hides both fists below the frame during this crouch.
        # Keep guard height/depth relative to the camera as the reference crouch changes.
        target=p.add(camera,[14.,25.,-8.] if s=='l' else [-14.,23.,-6.])
        fit_punch_shoulder(view_rest,s,target);cs=p.to_cs(view_rest);target=elbow_clearance(cs,s,target)
        view_arm(view_rest,s,target,cs['hand_'+s][0],p.sub(cs['lowerarm_'+s][1],cs['upperarm_'+s][1]))
    settle_right_shoulder(view_rest,1.5);view_rest=follow_ik(view_rest)
    views=[first_person(row,STANCE,view_rest) for row in stance_rows]
    view_rest=views[0];views[-1]=copy.deepcopy(view_rest)
    name='RPG_FP_CombatStance_Reference1027';result[name]=clip(name,views,[0.,.5,1.,1.5,2.],presentation='Camera guard fitted for visible fists; reference is third-person only, so this is a presentation adaptation')
    # Keep the original reference pair separate from gameplay single-strike adaptations.
    pairkeys=[((f-72)/60.,POSE[f]) for f in POSE]
    rows=[finish(at(pairkeys,i/FPS)) for i in range(57)]
    name='RPG_Reference1027_PairReview';result[name]=clip(name,rows,[0.,8/60,16/60,20/60,24/60,28/60,40/60,56/60])
    for side in 'lr':
        # Keep .36s impact/input timing; preserve the filmed 60Hz spacing of
        # loading, extension and follow-through around it. Guard entry supplies
        # the remaining time rather than stretching the punch's actual release.
        if side=='l':keys=[(0.,STANCE),(.226666667,POSE[72]),(.293333333,POSE[76]),(.36,POSE[80]),(.426666667,POSE[84]),(.626666667,STANCE),(1.05,STANCE)]
        else:
            load=copy.deepcopy(POSE[88])
            # A single RT must not also throw the preceding filmed left jab.
            for b in p.BONES:
                if b.endswith('_l') and any(t in b for t in ('clavicle','arm','hand','index','middle','ring','pinky','thumb')):load[b]=copy.deepcopy(STANCE[b])
            keys=[(0.,STANCE),(.226666667,load),(.293333333,POSE[92]),(.36,POSE[96]),(.426666667,POSE[100]),(.493333333,POSE[104]),(.626666667,POSE[112]),(.893333333,STANCE),(1.05,STANCE)]
        for variant,offset in enumerate([0.,1.,-1.],1):
            rows=[]
            for i in range(64):
                t=i/FPS
                # All variants share their .12 s loading boundary for smooth charge-tier handoff.
                # Slight acceleration/recovery timing changes keep the same strike and impact pose.
                # Spatial offsets near a fully lateral shoulder can choose a different elbow solution.
                phase=math.sin(math.pi*(t-.12)/.24)**2 if .12<t<.36 else math.sin(math.pi*(t-.36)/.69)**2 if .36<t<1.05 else 0.
                # rest only where the filmed motion turns round: guard, back of the wind-up / deepest coil (f76 / f92),
                # full extension (f80 / f96), back in guard
                stops={0.,.293333333,.36,1.05,.626666667 if side=='l' else .893333333}
                pose=at(keys,t+offset*.0025*phase,stops)
                # The source advances through the jab/cross. Keep a documented body drive
                # when removing capsule/root travel, with planted feet owning the support.
                drive=ease((t-.226666667)/.133333333)*(1.-ease((t-.48)/.5))
                # The rear cross needs the filmed shoulder/hip commitment to
                # reach as far as the lead jab on the shorter target rig.
                pose['pelvis']=(pose['pelvis'][0],p.add(pose['pelvis'][1],[0.,(20. if side=='l' else 30.)*DRIVE*drive,0.]))
                rows.append(finish(pose,True))
            rows[0]=copy.deepcopy(STANCE);rows[-1]=copy.deepcopy(STANCE)
            name=f'RPG_Punch_{side.upper()}_Light_Reference1027_V{variant}'
            result[name]=clip(name,rows,[a for a,_ in keys],impact=.36,hand=side,tier='Light',variant=variant-1,
                             adaptation='Isolated strike with common guard endpoints, planted-ball pivots and in-place hip/shoulder drive replacing source root advance')
            views=[first_person(row,STANCE,view_rest,i/FPS,side) for i,row in enumerate(rows)]
            views[0]=copy.deepcopy(view_rest);views[-1]=copy.deepcopy(view_rest)
            name=name.replace('RPG_Punch_','RPG_FP_Punch_')
            result[name]=clip(name,views,[a for a,_ in keys],impact=.36,hand=side,tier='Light',variant=variant-1,
                             presentation='Camera guard outside the hit interval; shared striking-hand path through impact and initial withdrawal')
    charged_family(result,view_rest)
    # Runtime aim calibration follows each authored fist, not the previous family's reach/height.
    lines=['// Generated by Tools/ProtoAnim/build_reference1027.py; component-space fist centres.',
           'const FVector ReferenceImpactPoints[2][3] = {']
    for side in 'lr':
        points=[]
        for tier in ['Light','Heavy','Power']:
            c=result[f'RPG_Punch_{side.upper()}_{tier}_Reference1027_V1'];cs=p.to_cs(c['poses'][round(c['impact']*FPS)])
            point=p.lerp(cs['hand_'+side][1],cs['middle_01_'+side][1],.5)
            points.append('FVector('+','.join(f'{v:.6f}' for v in point)+')')
        lines.append('    {'+', '.join(points)+'},')
    lines.append('};');calibration='\n'.join(lines)+'\n'
    guard_cs=p.to_cs(result['RPG_FP_CombatStance_Reference1027']['poses'][0]);head=guard_cs['head']
    eye=p.add(head[1],p.qrot(head[0],[15.,13.,0.]))
    calibration+='const FVector ReferenceGuardEye('+','.join(f'{v:.6f}' for v in eye)+');\n'
    path=HERE.parent.parent/'Source/RPG_CODEX/ReferencePunchCalibration.inl'
    if not path.exists() or path.read_text()!=calibration:path.write_text(calibration)
    # (written beside and swapped in, retried: something briefly holding the large file - a scanner - made a direct
    # write fail with Errno 22 now and then)
    import time
    out=HERE/'reference1027_uefn.json';tmp=out.with_suffix('.tmp');tmp.write_text(json.dumps(result))
    for attempt in range(20):
        try:os.replace(tmp,out);break
        except OSError:time.sleep(.5)
    else:raise RuntimeError('could not replace '+str(out))
    print('REFERENCE REVIEW ASSETS',len(result),flush=True)
    for name,c in result.items():
        if 'V2' in name or 'V3' in name:continue
        g.check_clip(name,c)

if __name__=='__main__':build()
