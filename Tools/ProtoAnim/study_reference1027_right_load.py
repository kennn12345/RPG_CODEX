"""Read-only right-load IK candidate; never writes installed animations or authoring inputs.

Uses head/shoulder registration to avoid forcing the fist to compensate for an
uncertain pelvis marker. A candidate is not approved for gameplay by this study.
"""
import copy,json,os,sys,math
from pathlib import Path
if os.environ.get('RPG_REFERENCE_FIT_DEPS'):sys.path.insert(0,os.environ['RPG_REFERENCE_FIT_DEPS'])
import numpy as np
from scipy.optimize import least_squares
import protoanim as p
import getupcheck as g
import build_reference1027 as builder
from audit_reference1027_registration import projection,clips,marks,original

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
def sample(c,t):
    f=t*c['fps'];a=int(f);b=min(a+1,c['frames']-1);u=f-a
    return {bone:(p._nlerp(c['poses'][a][bone][0],c['poses'][b][bone][0],u),p.lerp(c['poses'][a][bone][1],c['poses'][b][bone][1],u)) for bone in p.BONES}
def errors(local,camera):
    cs=p.to_cs(local);obs=marks['92'];pairs=[('head_centre',g.head_top(cs)),('upperarm_r',cs['upperarm_r'][1])]
    offset=np.mean([np.array(obs[b])-projection(v,camera) for b,v in pairs],axis=0)
    return {b:projection(g.head_top(cs) if b=='head_centre' else cs[b][1],camera)+offset-np.array(obs[b]) for b in ['head_centre','upperarm_r','lowerarm_r','hand_r']}
def run():
    assert p.SP.NAME=='uefn';p.SOFT_IK=p.SOFT_IK_ARM=1.e-7
    c=clips['RPG_Punch_R_Light_Reference1027_V1'];time=c['keys'][2];base=sample(c,time);cs=p.to_cs(base)
    pole=p.sub(cs['lowerarm_r'][1],cs['upperarm_r'][1]);hand=cs['hand_r'];camera=original
    def changed(x):
        local=copy.deepcopy(base)
        builder.view_arm(local,'r',p.add(hand[1],x[:3].tolist()),hand[0],p.add(pole,x[3:].tolist()))
        return local
    def residual(x):
        local=changed(x);err=errors(local,camera);cs0=p.to_cs(local);constraints=[]
        for label,fn,lo,hi in g.CHECKS:
            value=fn(cs0);constraints.extend([max(0.,lo+1.-value)*5.,max(0.,value-hi+1.)*5.])
        for pair,over in g.overlaps(cs0).items():constraints.append(max(0.,over-max(0.,g.IDLE_OVER[pair])-.5)*3.)
        return np.r_[err['lowerarm_r']/12.,err['hand_r']/12.,x/8.,constraints]
    fit=least_squares(residual,np.zeros(6),bounds=(-10.,10.),max_nfev=100,diff_step=1.e-3,ftol=1.e-7,xtol=1.e-7)
    candidate=changed(fit.x);bones=['upperarm_r','lowerarm_r','hand_r'];proposed=copy.deepcopy(c)
    # Local-arm delta fades out before either sample bracketing .36s impact.
    start=c['keys'][1];end=c['impact']-1./c['fps']
    for i,local in enumerate(proposed['poses']):
        t=i/c['fps'];w=builder.ease((t-start)/(time-start)) if t<=time else builder.ease((end-t)/(end-time))
        for bone in bones:
            delta=p.qmul(p.qconj(base[bone][0]),candidate[bone][0]);q=p._nlerp([0.,0.,0.,1.],delta,w)
            local[bone]=(p.qnorm(p.qmul(local[bone][0],q)),local[bone][1])
    issues=g.check_clip('READ_ONLY_R_LOAD_CANDIDATE',proposed)
    report=dict(method='Single right-loading pose only. Hand/forearm fitted after head/right-shoulder translation registration. Fixed original camera and bone lengths; six bounded IK target/pole deltas with priors. Candidate arm delta tested on current R-Light-V1 but not imported or applied.',
                source_commit='66575c6',frame=92,time=time,parameters_cm=fit.x.tolist(),
                before_residual_xy_px={b:v.tolist() for b,v in errors(base,camera).items()},after_residual_xy_px={b:v.tolist() for b,v in errors(candidate,camera).items()},
                delta_bones=bones,candidate_rotations={b:candidate[b][0] for b in bones},
                right_light_v1_anatomy_issues=issues,gameplay_approved=False,
                unchanged_impact_samples=all(proposed['poses'][i]==c['poses'][i] for i in [21,22]),
                next='Evaluate robustness to held-out camera/annotation choices, then full39 generation/checks before any import. Requires later engine/input/contact/visual verification; do not replace the installed reviewed pass from this single-pose study.')
    (ROOT/'Saved/ReferenceCombat1027/right-load-study.json').write_text(json.dumps(report,indent=2))
    (ROOT/'Saved/ReferenceCombat1027/right-load-candidate.json').write_text(json.dumps(proposed))
    print(json.dumps({k:v for k,v in report.items() if k not in ['candidate_rotations','next']},indent=2))
if __name__=='__main__':run()
