"""Gate reference review candidates before any gameplay activation; not a visual-accuracy score."""
import json,math
from pathlib import Path
import protoanim as p
import getupcheck as g
from build_reference1027 import REFERENCE_FIST

HERE=Path(__file__).resolve().parent
clips=json.loads((HERE/'reference1027_uefn.json').read_text())
stance=clips['RPG_CombatStance_Reference1027']['poses'][0]
fpstance=clips['RPG_FP_CombatStance_Reference1027']['poses'][0]
metrics={}
for name,c in clips.items():
    assert c['fps']==60 and c['frames']==len(c['poses'])
    assert not g.check_clip(name,c),(name,'anatomy')
    worst_floor=1000.;worst_root=0.;worst_ball=0.;max_step=0.;previous=None
    anchor=p.to_cs(stance)
    for i,local in enumerate(c['poses']):
        assert local['root']==[[0.,0.,0.,1.],[0.,0.,0.]],(name,i,'root')
        for b,(q,t) in local.items():
            assert all(math.isfinite(x) for x in q+t),(name,i,b,'non-finite')
            assert abs(sum(x*x for x in q)-1.)<1.e-4,(name,i,b,'quaternion')
            if b not in ('root','pelvis') and not b.startswith('ik_'):
                assert p.length(p.sub(t,p.REF[b][1]))<1.e-5,(name,i,b,'bone length')
            if b in REFERENCE_FIST:
                assert abs(sum(x*y for x,y in zip(q,REFERENCE_FIST[b][0])))>1.-1.e-6,(name,i,b,'open retargeted fingers')
        cs=p.to_cs(local)
        for s in 'lr':
            worst_floor=min(worst_floor,cs['ball_'+s][1][2]-3.2,cs['foot_'+s][1][2]-4.5)
            if 'PairReview' not in name:
                ball=cs['ball_'+s][1];rest=anchor['ball_'+s][1]
                worst_ball=max(worst_ball,math.hypot(ball[0]-rest[0],ball[1]-rest[1]))
        if previous:
            max_step=max(max_step,*[p.length(p.sub(cs['hand_'+s][1],previous['hand_'+s][1])) for s in 'lr'])
        previous=cs
    assert worst_floor>=-.05,(name,'floor clearance',worst_floor)
    if 'PairReview' not in name:assert worst_ball<.05,(name,'planted-ball sliding',worst_ball)
    if 'Punch_' in name:
        rest=fpstance if name.startswith('RPG_FP_') else stance
        assert c['poses'][0]==rest and c['poses'][-1]==rest,(name,'stance seam')
        assert c['impact']==.36,(name,'runtime timing')
    metrics[name]=dict(min_floor_margin=round(worst_floor,4),max_ball_slide_xy=round(worst_ball,4),max_hand_step_cm=round(max_step,3))
for side in 'LR':
    variants=[clips[f'RPG_Punch_{side}_Light_Reference1027_V{v}'] for v in (1,2,3)]
    for other in variants[1:]:
        assert variants[0]['poses'][:8]==other['poses'][:8],(side,'common charge-loading boundary')
        distance=max(p.length(p.sub(p.to_cs(a)['hand_'+side.lower()][1],p.to_cs(b)['hand_'+side.lower()][1]))
                     for a,b in zip(variants[0]['poses'],other['poses']))
        assert .25<distance<5.,(side,'subtle timing variants',distance)
    for variant in (1,2,3):
        body=clips[f'RPG_Punch_{side}_Light_Reference1027_V{variant}']
        view=clips[f'RPG_FP_Punch_{side}_Light_Reference1027_V{variant}']
        for i in range(17,40):
            a=p.to_cs(body['poses'][i]);b=p.to_cs(view['poses'][i])
            for bone in ['hand_'+side.lower(),'middle_01_'+side.lower()]:
                assert p.length(p.sub(a[bone][1],b[bone][1]))<.1,(side,variant,i,'TP/FP hit or withdrawal mismatch')
path=HERE.parent.parent/'Saved/ReferenceCombat1027/review-validation.json'
path.write_text(json.dumps(metrics,indent=2))
print('PASS: TP/FP anatomy, rest bone lengths, neutral roots, stance endpoints, planted pivots, small variants, .36s hit timing and shared striking-hand/knuckle positions through impact and withdrawal. Gameplay validation remains required.')
