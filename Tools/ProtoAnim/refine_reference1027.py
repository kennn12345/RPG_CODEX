"""Refine current exported upper-body mechanics against visible reference punch phases.

Keeps target-rig lengths/anatomy and the native depth prior. Hidden ankles are not
fitted to shoe-tip annotations. No visual accuracy percentage is inferred.
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
DATA=json.loads(Path(os.environ['RPG_REFERENCE_BASE_MANIFEST']).read_text())
assert abs(DATA['RPG_Punch_L_Light_Reference1027_V1']['keys'][1]-.12)<1.e-6,'Use the pinned f1be251 baseline-T manifest for this reproducible refinement pass.'
MARKS=json.loads((HERE/'reference1027_landmarks.json').read_text())['frames']
CAM=json.loads((HERE/'captures/reference1027_fitted_poses.json').read_text())['camera']
BONES=['spine_01','spine_03','spine_05','neck_01','head','clavicle_l','upperarm_l','lowerarm_l','clavicle_r','upperarm_r','lowerarm_r']
Y,P,D,F,CX,CY=CAM;Y,P=math.radians(Y),math.radians(P)
FORWARD=np.array([math.sin(Y)*math.cos(P),math.cos(Y)*math.cos(P),-math.sin(P)])
RIGHT=np.array([-FORWARD[1],FORWARD[0],0.]);RIGHT/=np.linalg.norm(RIGHT);UP=np.cross(FORWARD,RIGHT)
def projected(v):
    v=np.array(v)-[0.,0.,85.];depth=D+v@FORWARD
    return np.array([CX+F*(v@RIGHT)/depth,CY-F*(v@UP)/depth])
def point(cs,b):return g.head_top(cs) if b=='head_centre' else cs[b][1]
def changed(base,x):
    local=copy.deepcopy(base)
    for k,b in enumerate(BONES):
        rx,ry,rz=x[k*3:k*3+3]
        local[b]=(p.qnorm(p.qmul(p.qeuler(yaw=rz,pitch=rx,roll=ry),base[b][0])),base[b][1])
    return local
def fit(frame,base):
    marks={b:xy for b,xy in MARKS[str(frame)].items() if b=='head_centre' or any(b.startswith(v) for v in ['upperarm_','lowerarm_','hand_'])}
    if frame==88:marks={b:xy for b,xy in marks.items() if not b.endswith('_l')}  # The preceding filmed jab is excluded from a single RT strike.
    pelvis=np.array(MARKS[str(frame)]['pelvis'])
    def evaluate(x,report=False):
        local=changed(base,x)
        # Evaluate the same hinge repair used by export. A valid raw Euler pose
        # can otherwise choose an invalid elbow plane after target-rig IK.
        for s in 'lr':
            cs=p.to_cs(local)
            repair_chain(local,'upperarm_'+s,'lowerarm_'+s,'hand_'+s,cs['hand_'+s][1],p.sub(cs['lowerarm_'+s][1],cs['upperarm_'+s][1]),cs['hand_'+s][0])
        cs=p.to_cs(local);offset=pelvis-projected(cs['pelvis'][1])
        visible=np.concatenate([(projected(point(cs,b))+offset-xy)/12. for b,xy in marks.items()])
        anatomy=[]
        for label,fn,lo,hi in g.CHECKS:
            value=fn(cs);anatomy.extend([max(0.,lo+1.-value)*5.,max(0.,value-(hi-1.))*5.])
        for s in 'lr':
            flex,side=g.hinge(cs,s,'upperarm','lowerarm','hand','elbow')
            anatomy.extend([side*2.,max(0.,-flex)*2.,max(0.,flex-143.)*2.,max(0.,g.wrist(cs,s)-54.)])
        for pair,over in g.overlaps(cs).items():anatomy.append(max(0.,over-max(0.,g.IDLE_OVER[pair])-.5)*3.)
        return (local,visible) if report else np.r_[visible,np.asarray(x)/16.,anatomy]
    zero=np.zeros(len(BONES)*3);before=evaluate(zero,True)[1]
    fit=least_squares(evaluate,zero,bounds=(-22.,22.),max_nfev=65,ftol=2.e-5,xtol=2.e-5,diff_step=1.e-4)
    local,_=evaluate(fit.x,True)
    for s in 'lr':
        cs=p.to_cs(local)
        repair_chain(local,'upperarm_'+s,'lowerarm_'+s,'hand_'+s,cs['hand_'+s][1],p.sub(cs['lowerarm_'+s][1],cs['upperarm_'+s][1]),cs['hand_'+s][0])
    follow_ik(local)
    cs=p.to_cs(local);offset=pelvis-projected(cs['pelvis'][1])
    after=np.concatenate([(projected(point(cs,b))+offset-xy)/12. for b,xy in marks.items()])
    result=dict(frame=frame,rotations={b:local[b][0] for b in BONES},before_rms_px=float(np.sqrt(np.mean(before**2))*12.),after_rms_px=float(np.sqrt(np.mean(after**2))*12.),evaluations=fit.nfev)
    print('REFINE',frame,round(result['before_rms_px'],2),round(result['after_rms_px'],2),flush=True)
    return result
def run():
    assert p.SP.NAME=='uefn'
    tasks=[(128,'RPG_CombatStance_Reference1027',0.)]+[(f,'RPG_Punch_L_Light_Reference1027_V1',t) for f,t in [(72,.12),(76,.20),(80,.36),(84,.43)]]+[(f,'RPG_Punch_R_Light_Reference1027_V1',t) for f,t in [(88,.12),(92,.24),(96,.36),(100,.44),(104,.54),(112,.72)]]
    rows=[];out=HERE/'captures/reference1027_refined_upper_body.json'
    for frame,name,t in tasks:
        c=DATA[name];rows.append(fit(frame,c['poses'][round(t*c['fps'])]))
        out.write_text(json.dumps(dict(method='Current exported upper-body refinement. Fixed calibrated camera and pelvis-only image registration; original visible landmarks and target anatomy. Hidden foot markers excluded. No certified accuracy.',rows=rows)))
if __name__=='__main__':run()
