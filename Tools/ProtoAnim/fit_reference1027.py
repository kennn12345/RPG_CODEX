"""Fit visible punch landmarks, with native retargeted motion as the 3D prior.

Offline review candidates only. No runtime activation. Occluded joints retain the
source prior; a small image error does NOT establish an accurate 3D reconstruction.
RPG_SKEL=uefn; scipy may be supplied in RPG_REFERENCE_FIT_DEPS.
"""
import copy,json,math,os,sys
from pathlib import Path
if os.environ.get('RPG_REFERENCE_FIT_DEPS'):sys.path.insert(0,os.environ['RPG_REFERENCE_FIT_DEPS'])
import numpy as np
from scipy.optimize import least_squares
import protoanim as p
import getupcheck as g
from capture_getups import repair_chain,follow_ik

HERE=Path(__file__).resolve().parent
DATA=json.loads((HERE/'captures/reference1027_uefn_sources.json').read_text())['clips']
MARKS=json.loads((HERE/'reference1027_landmarks.json').read_text())['frames']
OUT=HERE/'captures/reference1027_fitted_poses.json'

def blend(a,b,u):
    return {bone:(p._nlerp(a[bone][0],b[bone][0],u),p.lerp(a[bone][1],b[bone][1],u)) for bone in p.BONES}

def sample(name,t):
    c=DATA[name];f=max(0.,min((c['frames']-1),t*c['fps']));a=int(f);b=min(a+1,c['frames']-1)
    pose=blend(c['poses'][a],c['poses'][b],f-a)
    pose['root']=([0.,0.,0.,1.],[0.,0.,0.])
    # Keep rest translations on the target rig. Motion uses rotations, not stretched limbs.
    for bone in p.BONES:
        if bone not in ('root','pelvis') and not bone.startswith('ik_'):pose[bone]=(pose[bone][0],p.REF[bone][1])
    return pose

def point(cs,bone):
    return g.head_top(cs) if bone=='head_centre' else cs[bone][1]

def basis(yaw,pitch):
    y,z=math.radians(yaw),math.radians(pitch)
    f=np.array([math.sin(y)*math.cos(z),math.cos(y)*math.cos(z),-math.sin(z)])
    # Unreal camera right = world up cross forward (+Y projects toward screen right here).
    r=np.array([-f[1],f[0],0.]);r/=np.linalg.norm(r);u=np.cross(f,r)
    return r,u,f

def project(v,cam):
    yaw,pitch,distance,focal,cx,cy=cam;r,u,f=basis(yaw,pitch)
    rel=np.asarray(v)-np.array([0.,0.,85.]);depth=distance+np.dot(rel,f)
    return np.array([cx+focal*np.dot(rel,r)/depth,cy-focal*np.dot(rel,u)/depth])

def camera_fit():
    pose=sample('H2H_Idle',0.);cs=p.to_cs(pose);marks=MARKS['128']
    def residual(x):
        return np.r_[np.concatenate([(project(point(cs,b),x)-uv)/10 for b,uv in marks.items()]),
                     (x[0]-36)/30,(x[1]-12)/20,(x[2]-350)/400,(x[3]-2200)/2500]
    # A single cropped view cannot identify focal length and distance independently.
    # Fix distance at 3.5 m instead of letting a fit choose an implausibly close camera.
    fit=least_squares(residual,[36,16,350,2200,320,650],bounds=([5,8,349.999,700,-200,0],[85,30,350.001,6500,1000,1242]),max_nfev=400)
    return fit.x.tolist()

# Local rotations distributed through the actual spine chain; keep every bone length.
ROT_BONES=['pelvis','spine_01','spine_03','spine_05','neck_01',
           'clavicle_l','upperarm_l','lowerarm_l','clavicle_r','upperarm_r','lowerarm_r',
           'thigh_l','calf_l','thigh_r','calf_r']
NROT=len(ROT_BONES)*3

def changed(base,x):
    local=copy.deepcopy(base)
    for k,b in enumerate(ROT_BONES):
        rx,ry,rz=x[k*3:k*3+3]
        local[b]=(p.qnorm(p.qmul(p.qeuler(yaw=rz,pitch=rx,roll=ry),base[b][0])),base[b][1])
    local['pelvis']=(local['pelvis'][0],p.add(base['pelvis'][1],x[NROT:NROT+3]))
    return local

def fit_pose(frame,base,cam):
    marks=MARKS[str(frame)];basecs=p.to_cs(base)
    def evaluate(x,report=False):
        pose=changed(base,x);cs=p.to_cs(pose);view=list(cam);view[4]+=x[-2];view[5]+=x[-1]
        visible=np.concatenate([(project(point(cs,b),view)-uv)/10 for b,uv in marks.items()])
        # Regularise 3D ambiguity to the native source. Image annotations have finite uncertainty.
        prior=np.asarray(x[:NROT])/np.repeat([14,10,10,10,15,20,35,35,20,35,35,20,20,20,20],3)
        anatomy=[]
        for s in 'lr':
            for upper,lower,end,key in [('upperarm','lowerarm','hand','elbow'),('thigh','calf','foot','knee')]:
                flex,side=g.hinge(cs,s,upper,lower,end,key)
                anatomy.extend([side/3.,max(0.,-3.-flex)/2.,max(0.,flex-(144. if key=='elbow' else 149.))/2.])
            anatomy.append(max(0.,g.wrist(cs,s)-70.)/5.)
            # Preserve believable foot placement. Hidden ankles are source-prior constrained.
            foot=cs['foot_'+s][1]
            anatomy.extend([(foot[j]-basecs['foot_'+s][1][j])/12. for j in (0,1)])
            anatomy.append(max(0.,4.-foot[2])/2.)
        for label,fn,lo,hi in g.CHECKS:
            value=fn(cs)
            # This footage shows horizontal punches, not overhead shoulder lifts.
            if label.startswith('shoulder_') and label.endswith('flex'):lo=-51.;hi=118.
            if label=='spine twist':lo=-38.;hi=38.
            anatomy.extend([max(0.,lo-value)/1.5,max(0.,value-hi)/1.5])
        for pair,overlap in g.overlaps(cs).items():
            excess=overlap-max(0.,g.IDLE_OVER[pair])
            anatomy.append(max(0.,excess-1.)/1.5)
        # Joint positions are allowed to change without silently advancing the capsule.
        priort=np.asarray(x[NROT:NROT+3])/np.array([8.,8.,6.])
        result=np.r_[visible,prior,priort,anatomy,np.asarray(x[-2:])/300.]
        return (pose,view,visible) if report else result
    x=np.zeros(NROT+5)
    initial=evaluate(x,True)[2]
    lower=np.r_[np.full(NROT,-95.),[-12,-12,-14],[-500,-350]]
    upper=np.r_[np.full(NROT,95.),[12,12,10],[500,350]]
    fit=least_squares(evaluate,x,bounds=(lower,upper),max_nfev=220,ftol=2.e-6,xtol=2.e-6,diff_step=1.e-4)
    pose,view,visible=evaluate(fit.x,True)
    # Re-express small retarget/fit hinge-plane errors using target-rig hinges and unchanged endpoints.
    old=p.SOFT_IK,p.SOFT_IK_ARM;p.SOFT_IK=p.SOFT_IK_ARM=1.e-7
    try:
        for s in 'lr':
            for a,b,c in [('upperarm','lowerarm','hand'),('thigh','calf','foot')]:
                cs=p.to_cs(pose)
                # Preserve the source foot's world-space pivot; fitting a knee must not tip the shoe up.
                rotation=basecs[c+'_'+s][0] if c=='foot' else cs[c+'_'+s][0]
                repair_chain(pose,a+'_'+s,b+'_'+s,c+'_'+s,cs[c+'_'+s][1],p.sub(cs[b+'_'+s][1],cs[a+'_'+s][1]),rotation)
        follow_ik(pose)
    finally:p.SOFT_IK,p.SOFT_IK_ARM=old
    return dict(frame=frame,pose=pose,camera=view,landmark_rms_px=round(float(np.sqrt(np.mean(visible**2))*10),2),
                initial_rms_px=round(float(np.sqrt(np.mean(initial**2))*10),2),evaluations=fit.nfev,
                uncertainty='Manual 2D visible landmarks only; source-prior depth and hidden joints, no certified 3D accuracy')

def run():
    assert p.SP.NAME=='uefn'
    cam=camera_fit();print('CAMERA',cam,flush=True)
    # Three native sources were reviewed. Combo03 has the nearest jab-to-cross peak separation (~.27 s).
    times={72:.25,76:.36,80:.42,84:.46,88:.52,92:.60,96:.68,100:.72,104:.77}
    rows=[]
    for frame in map(int,MARKS):
        if frame in times:base=sample('H2H_PunchCombo03',times[frame])
        elif frame==112:base=blend(sample('H2H_PunchCombo03',.76),sample('H2H_Idle',0.),.6)
        else:base=sample('H2H_Idle',0.)
        row=fit_pose(frame,base,cam);rows.append(row)
        print('FIT',frame,row['initial_rms_px'],row['landmark_rms_px'],row['evaluations'],flush=True)
        OUT.write_text(json.dumps(dict(source='Reference screen recording visible punch landmarks + directly retargeted RamsterZ Combo03 prior',
            staging=True,not_certified_accuracy=True,camera=cam,rows=rows)))

if __name__=='__main__':run()
