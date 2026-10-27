"""Read-only, held-out camera/annotation registration study of the installed punch pass.

No animation, mesh, engine setting or original annotation is changed. Camera
parameters can absorb pose error; reduced residuals do not certify motion accuracy.
"""
import hashlib,json,math,os,sys
from pathlib import Path
if os.environ.get('RPG_REFERENCE_FIT_DEPS'):sys.path.insert(0,os.environ['RPG_REFERENCE_FIT_DEPS'])
import numpy as np
from scipy.optimize import least_squares
import protoanim as p
import getupcheck as g

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
manifest=HERE/'reference1027_uefn.json';annotations=HERE/'reference1027_landmarks.json'
clips=json.loads(manifest.read_text());marks=json.loads(annotations.read_text())['frames']
old=json.loads((HERE/'captures/reference1027_fitted_poses.json').read_text())['camera']
distance=old[2];original=np.array([old[0],old[1],old[3]])
train=[72,80,88,96,112,128];heldout=[76,84,92,100,104,120]
times={72:('L',1),76:('L',2),80:('L',3),84:('L',4),88:('R',1),92:('R',2),96:('R',3),100:('R',4),104:('R',5),112:('R',6),120:('R',None)}

def pose(frame):
    if frame==128:c=clips['RPG_CombatStance_Reference1027'];t=0.
    else:
        side,key=times[frame];c=clips[f'RPG_Punch_{side}_Light_Reference1027_V1']
        t=(c['keys'][6]+c['keys'][7])*.5 if frame==120 else c['keys'][key]
    f=min(c['frames']-1,t*c['fps']);a=int(f);b=min(a+1,c['frames']-1);u=f-a
    local={bone:(p._nlerp(c['poses'][a][bone][0],c['poses'][b][bone][0],u),p.lerp(c['poses'][a][bone][1],c['poses'][b][bone][1],u)) for bone in p.BONES}
    cs=p.to_cs(local);return {bone:g.head_top(cs) if bone=='head_centre' else cs[bone][1] for bone in marks[str(frame)]}
positions={f:pose(f) for f in train+heldout}

def projection(point,camera):
    yaw,pitch,focal=camera;yaw,pitch=map(math.radians,[yaw,pitch])
    forward=np.array([math.sin(yaw)*math.cos(pitch),math.cos(yaw)*math.cos(pitch),-math.sin(pitch)])
    right=np.array([-forward[1],forward[0],0.]);right/=np.linalg.norm(right);up=np.cross(forward,right)
    v=np.array(point)-[0.,0.,85.];depth=distance+v@forward
    return np.array([focal*(v@right)/depth,-focal*(v@up)/depth])

def observations(frame,mode):
    allowed=['head_centre','upperarm_r','lowerarm_r','hand_r','upperarm_l','lowerarm_l','hand_l']
    if mode=='body_centroid':allowed+=['pelvis','calf_l','calf_r']
    if frame==88:allowed=[b for b in allowed if not b.endswith('_l')] # Do not fit the preceding jab into an isolated RT.
    return {b:xy for b,xy in marks[str(frame)].items() if b in allowed}

def sigma(bone):
    return 25. if bone in ['pelvis','calf_l','calf_r'] else 20. if bone.startswith('upperarm') else 18. if bone=='head_centre' else 12.

def residuals(camera,frames,mode,report=False):
    result=[];details=[]
    for frame in frames:
        obs=observations(frame,mode);pred={b:projection(positions[frame][b],camera) for b in obs}
        if mode=='pelvis_anchor':offset=np.array(marks[str(frame)]['pelvis'])-projection(positions[frame]['pelvis'],camera)
        else:
            weights=np.array([1./sigma(b)**2 for b in obs]);offset=np.average([np.array(xy)-pred[b] for b,xy in obs.items()],axis=0,weights=weights)
        errors={b:(pred[b]+offset-np.array(xy)) for b,xy in obs.items()}
        result.extend(np.concatenate([e/sigma(b) for b,e in errors.items()]))
        details.append(dict(frame=frame,rms_px=float(np.sqrt(np.mean(np.concatenate(list(errors.values()))**2))),
                            residual_xy_px={b:[round(float(v),3) for v in e] for b,e in errors.items()}))
    if report:return dict(coordinate_rms_px=float(np.sqrt(np.mean([v*v for row in details for e in row['residual_xy_px'].values() for v in e]))),frames=details)
    return result

def run():
    assert p.SP.NAME=='uefn';studies=[]
    for mode in ['pelvis_anchor','upper_body_centroid','body_centroid']:
        def cost(x):return np.r_[residuals(x,train,mode),(x-original)/[25.,15.,1000.]]
        fit=least_squares(cost,original,bounds=([5.,0.,600.],[85.,35.,6000.]),max_nfev=150,ftol=1.e-9,xtol=1.e-9)
        row=dict(registration=mode,candidate_camera=dict(yaw_deg=float(fit.x[0]),pitch_deg=float(fit.x[1]),focal_px=float(fit.x[2]),distance_cm=distance),
                 original_train=residuals(original,train,mode,True),candidate_train=residuals(fit.x,train,mode,True),
                 original_heldout=residuals(original,heldout,mode,True),candidate_heldout=residuals(fit.x,heldout,mode,True))
        studies.append(row)
        print(mode,'camera',[round(float(v),3) for v in fit.x],
              'train',round(row['original_train']['coordinate_rms_px'],2),'->',round(row['candidate_train']['coordinate_rms_px'],2),
              'heldout',round(row['original_heldout']['coordinate_rms_px'],2),'->',round(row['candidate_heldout']['coordinate_rms_px'],2),flush=True)
    report=dict(method='Fixed installed AG poses. Camera-only fit on six phases; six separate held-out phases. No pose changes, no bone scaling, no foot-tip observations. Per-phase translation uses either the original pelvis marker or weighted visible joint centroids. Candidate cameras are diagnostics, not measured truth or a visual accuracy percentage.',
                manifest_sha256=hashlib.sha256(manifest.read_bytes()).hexdigest(),annotations_sha256=hashlib.sha256(annotations.read_bytes()).hexdigest(),
                training_frames=train,heldout_frames=heldout,original_camera=dict(yaw_deg=old[0],pitch_deg=old[1],distance_cm=old[2],focal_px=old[3]),studies=studies)
    (ROOT/'Saved/ReferenceCombat1027/registration-audit.json').write_text(json.dumps(report,indent=2))
if __name__=='__main__':run()
