"""The fighting stance, authored from the user's description (2026-10-28), not from any video or capture.

Boxing stance: feet about standing width, the left (lead) foot a little ahead and the right back with its toe turned
out; a light, even knee bend; shoulders square with the lead side barely forward; fists at chin height, elbows down.
A still pose (every frame identical) so it is easy to adjust by hand in the editor. RPG_FP_FightStance is the same
body with the fists placed where the first-person camera sees them.

Run once to create the clips; after that they belong to the editor (re-running overwrites hand edits).
  RPG_SKEL=uefn python build_fight_stance.py   ->  fight_stance_uefn.json, imported to /Game/RPG/Animations/FightStance
"""
import copy,json,math,os
from pathlib import Path
import protoanim as p
import getupcheck as g
from capture_getups import repair_chain,follow_ik

HERE=Path(__file__).resolve().parent;FPS=30;FRAMES=31

# (Facing +Y, left +X; a yaw above 90 turns the lead side forward.)
PELVIS_TURN=6.;CHEST_TURN=10.        # lead side barely forward
LEAN=6.                              # slight forward lean from the hips, gaze kept level
CHIN_TUCK=4.                         # head tipped down a little
KNEE=18.                             # light, even knee bend
FEET_HALF_WIDTH=15.;FEET_STAGGER=16.;TOE_YAW={'l':84.,'r':105.}
# Fists (the wrist joints) relative to the head joint, facing straight ahead: (across, forward, up). Either side of
# the chin, the lead a few cm further out than the rear; the knuckles sit about 8 cm above and ahead of each wrist,
# at chin height.
FIST={'l':(8.,33.,-17.),'r':(-9.,26.,-15.)}
# First person: relative to the eye, so both fists show in the lower part of the view.
EYE=(15.,13.,0.)
FP_FIST={'l':(12.,30.,-12.),'r':(-12.,26.,-10.)}

def yaw_of(v):return math.degrees(math.atan2(v[1],v[0]))
def flat(v):return p.norm([v[0],v[1],0.])

def set_world(local,bone,world):
    cs=p.to_cs(local);local[bone]=(p.qnorm(p.qmul(p.qconj(cs[p.PARENT[bone]][0]),world)),local[bone][1])

def turn(local,bone,axis,angle):
    set_world(local,bone,p.qmul(p.qaxis(axis,angle),p.to_cs(local)[bone][0]))

def fist(local):
    # The rig's fist, with the four fingers closed around the palm.
    for bone,(rotation,_) in p.FIST.items():
        if any(bone.startswith(f+'_') for f in ('index','middle','ring','pinky')) and bone.split('_')[-2] in ('01','02','03'):
            delta=p.qmul(p.qconj(p.REF[bone][0]),rotation)
            if delta[3]<0:delta=[-v for v in delta]
            angle=math.degrees(2.*math.acos(max(-1.,min(1.,delta[3]))))
            rotation=p.qnorm(p.qmul(p.REF[bone][0],p.qaxis(delta[:3],max(angle,{'01':80.,'02':95.,'03':65.}[bone.split('_')[-2]]))))
        local[bone]=(list(rotation),p.REF[bone][1])

def facing(cs):
    f=flat(g.frame(cs,'spine_05')[1]);return f,[f[1],-f[0],0.]   # forward, left (horizontal)

def place_arm(local,s,target):
    """Two-bone arm to the wrist target, elbow down and a little out; wrist straight, palms facing each other with the
    index knuckle forward and turned a little in (a slight pronation), the way a guard is held."""
    cs=p.to_cs(local);fwd,left=facing(cs);out=left if s=='l' else p.mul(left,-1.)
    pole=p.add(p.add([0.,0.,-1.],p.mul(out,.25)),p.mul(fwd,-.2))
    p.two_bone(cs,local,'upperarm_'+s,'lowerarm_'+s,'hand_'+s,target,pole,max_reach=.995)
    for bone in ('upperarm_'+s,'lowerarm_'+s,'hand_'+s):
        local[bone]=(p.qnorm(p.qmul(p.qconj(cs[p.PARENT[bone]][0]),cs[bone][0])),local[bone][1])
    for _ in range(3):
        cs=p.to_cs(local);hand=cs['hand_'+s][1]
        fore=p.norm(p.sub(hand,cs['lowerarm_'+s][1]))
        finger=p.norm(p.sub(cs['middle_metacarpal_'+s][1] if 'middle_metacarpal_'+s in cs else cs['middle_01_'+s][1],hand))
        q=p.qnorm(p.cross(finger,fore)+[1.+p.dot(finger,fore)])   # straight wrist
        set_world(local,'hand_'+s,p.qmul(q,cs['hand_'+s][0]))
        cs=p.to_cs(local)
        across=p.sub(cs['index_01_'+s][1],cs['pinky_01_'+s][1])   # pinky -> index knuckles
        want=p.add(fwd,p.mul(out,-.25))                            # forward, a little in
        a=p.norm(p.sub(across,p.mul(fore,p.dot(across,fore))));b=p.norm(p.sub(want,p.mul(fore,p.dot(want,fore))))
        angle=math.degrees(math.atan2(p.dot(p.cross(a,b),fore),p.dot(a,b)))
        if abs(angle)<.2:break
        turn(local,'hand_'+s,fore,angle)

def body():
    local=copy.deepcopy(p.REF);local['root']=([0.,0.,0.,1.],[0.,0.,0.])
    fist(local)
    up=[0.,0.,1.]
    turn(local,'pelvis',up,PELVIS_TURN)
    for bone in ('spine_01','spine_03'):turn(local,bone,up,(CHEST_TURN-PELVIS_TURN)*.5)
    # Lean forward through the spine; the head keeps its gaze, then tips the chin down a little.
    cs=p.to_cs(local);fwd,left=facing(cs);head=cs['head'][0]
    for bone,share in (('spine_01',.4),('spine_03',.3),('spine_05',.3)):turn(local,bone,left,-LEAN*share)
    set_world(local,'head',head);turn(local,'head',left,-CHIN_TUCK)
    # Feet about the hips: ankles across and staggered, toes turned by TOE_YAW, soles flat.
    cs=p.to_cs(local);centre=p.lerp(cs['thigh_l'][1],cs['thigh_r'][1],.5);targets={};rotations={}
    for s,(across,ahead) in {'l':(FEET_HALF_WIDTH,FEET_STAGGER*.5),'r':(-FEET_HALF_WIDTH,-FEET_STAGGER*.5)}.items():
        rest=p.to_cs(p.REF);toe=yaw_of(p.sub(rest['ball_'+s][1],rest['foot_'+s][1]))
        rotations[s]=p.qmul(p.qaxis(up,TOE_YAW[s]-toe),rest['foot_'+s][0])
        targets[s]=[centre[0]+across,centre[1]+ahead,rest['foot_'+s][1][2]]
    # Pelvis height for an even KNEE in both legs.
    a=p.length(local['calf_l'][1]);b=p.length(local['foot_l'][1])
    reach=math.sqrt(a*a+b*b+2.*a*b*math.cos(math.radians(KNEE)))
    drop=sum(cs['thigh_'+s][1][2]-(t[2]+math.sqrt(max(0.,reach*reach-(cs['thigh_'+s][1][0]-t[0])**2-(cs['thigh_'+s][1][1]-t[1])**2)))
             for s,t in targets.items())*.5
    local['pelvis']=(local['pelvis'][0],p.add(local['pelvis'][1],[0.,0.,-drop]))
    for s in 'lr':
        pole=flat(p.qrot(rotations[s],local['ball_'+s][1]))   # knee over the toes
        repair_chain(local,'thigh_'+s,'calf_'+s,'foot_'+s,targets[s],pole,rotations[s])
    return local

def arms(local,first_person=False):
    cs=p.to_cs(local);fwd,left=[0.,1.,0.],[1.,0.,0.];head=cs['head']
    anchor=p.add(head[1],p.qrot(head[0],list(EYE))) if first_person else head[1]
    for s,(across,ahead,rise) in (FP_FIST if first_person else FIST).items():
        place_arm(local,s,p.add(anchor,p.add(p.add(p.mul(left,across),p.mul(fwd,ahead)),[0.,0.,rise])))
    return follow_ik(local)

def clip(name,pose,**info):
    return dict(name=name,bones=p.BONES,fps=FPS,frames=FRAMES,poses=[copy.deepcopy(pose) for _ in range(FRAMES)],keys=[0.,1.],
                source='Authored from the user\'s description of a boxing stance (2026-10-28); still pose for hand editing',**info)

def build():
    assert p.SP.NAME=='uefn'
    p.SOFT_IK=p.SOFT_IK_ARM=1.e-7   # land exactly on the targets (no soft stop short of a straight limb)
    stance=body()
    result={'RPG_FightStance':clip('RPG_FightStance',arms(copy.deepcopy(stance))),
            'RPG_FP_FightStance':clip('RPG_FP_FightStance',arms(copy.deepcopy(stance),True),presentation='First person: fists placed in view')}
    out=HERE/'fight_stance_uefn.json';out.write_text(json.dumps(result))
    for name,c in result.items():print(name,'ok' if not g.check_clip(name,c) else 'ANATOMY')
    return result

if __name__=='__main__':build()
