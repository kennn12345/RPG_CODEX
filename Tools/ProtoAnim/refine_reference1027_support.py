"""Fit a shared grounded support footprint to visible knee/ankle observations.

Shoe-tip/occluded ankle markers are excluded. Ground contact, fixed lengths and
anatomy constrain the single-view ambiguity; this is not certified reconstruction.
"""
import copy,json,os,sys,math
from pathlib import Path
if os.environ.get('RPG_REFERENCE_FIT_DEPS'):sys.path.insert(0,os.environ['RPG_REFERENCE_FIT_DEPS'])
import numpy as np
from scipy.optimize import least_squares
import protoanim as p
import getupcheck as g
import build_reference1027 as b
from refine_reference1027 import projected,MARKS
HERE=Path(__file__).resolve().parent
BASE=json.loads(Path(os.environ['RPG_REFERENCE_BASE_MANIFEST']).read_text())
CASES=[(128,'RPG_CombatStance_Reference1027',0.,['calf_r']),
       (80,'RPG_Punch_L_Light_Reference1027_V1',.36,['calf_r','calf_l','foot_l']),
       (84,'RPG_Punch_L_Light_Reference1027_V1',.43,['calf_r','calf_l','foot_l']),
       (92,'RPG_Punch_R_Light_Reference1027_V1',.24,['calf_r','calf_l']),
       (96,'RPG_Punch_R_Light_Reference1027_V1',.36,['calf_r'])]
def evaluate(x,report=False):
    visible=[];constraints=[];posed=[]
    for f,name,t,bones in CASES:
        c=BASE[name];local=copy.deepcopy(c['poses'][round(t*60)])
        local['pelvis']=(local['pelvis'][0],p.add(local['pelvis'][1],x[4:7].tolist()))
        cs=p.to_cs(local);targets={};rot={}
        for k,s in enumerate('lr'):
            rot[s]=cs['foot_'+s][0];targets[s]=p.sub([x[k*2],x[k*2+1],3.2],p.qrot(rot[s],local['ball_'+s][1]))
            targets[s][2]=max(4.5,targets[s][2])
        b.lower_for_contacts(local,targets)
        for s in 'lr':b.planted_leg(local,s,targets[s],rot[s])
        cs=p.to_cs(local);offset=np.array(MARKS[str(f)]['pelvis'])-projected(cs['pelvis'][1])
        for bone in bones:visible.extend((projected(cs[bone][1])+offset-MARKS[str(f)][bone])/(25. if bone.startswith('foot') else 15.))
        for label,fn,lo,hi in g.CHECKS:
            if label.startswith(('hip_','knee_','ankle_')):
                v=fn(cs);constraints.extend([max(0.,lo+1.-v)*3.,max(0.,v-hi+1.)*3.])
        posed.append(local)
    prior=(np.asarray(x)-[12.,30.,-20.,-18.,0.,0.,0.])/np.array([25.,25.,20.,20.,12.,12.,12.])
    if report:return dict(parameters=[float(v) for v in x],weighted_joint_rms=float(np.sqrt(np.mean(np.asarray(visible)**2))),poses=posed)
    return np.r_[visible,constraints,prior]
def run():
    p.SOFT_IK=p.SOFT_IK_ARM=1.e-7
    start=np.array([12.,30.,-20.,-18.,0.,0.,0.]);before=evaluate(start,True)
    fit=least_squares(evaluate,start,bounds=([-2.,15.,-45.,-40.,-15.,-18.,-20.],[40.,70.,-5.,10.,15.,18.,5.]),max_nfev=45,diff_step=1.e-3,ftol=1.e-4,xtol=1.e-4)
    after=evaluate(fit.x,True);after.pop('poses')
    after.update(method='Visible knee centres and clearly visible front ankles f80/84 only. Fixed camera, pelvis image translation aligned. Grounded shared support; anatomy and native support prior. Other shoe markers excluded.',before_weighted_joint_rms=before['weighted_joint_rms'],evaluations=fit.nfev)
    (HERE/'captures/reference1027_refined_support.json').write_text(json.dumps(after,indent=2));print(json.dumps(after,indent=2),flush=True)
if __name__=='__main__':run()
