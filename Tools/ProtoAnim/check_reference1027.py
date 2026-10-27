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
guard_cs=p.to_cs(fpstance)
guard_eye=p.add(guard_cs['head'][1],p.qrot(guard_cs['head'][0],[15.,13.,0.]))
metrics={}
for name,c in clips.items():
    assert c['fps']==60 and c['frames']==len(c['poses'])
    assert not g.check_clip(name,c),(name,'anatomy')
    worst_floor=1000.;worst_ball=0.;max_step=0.;previous=None;hips=0.;head=0.;forward=-1.e6
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
        hips=max(hips,p.length(p.sub(cs['pelvis'][1],anchor['pelvis'][1])))
        head=max(head,p.length(p.sub(cs['head'][1],anchor['head'][1])))
        if 'hand' in c:forward=max(forward,cs['hand_'+c['hand']][1][1])
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
        assert c['impact']==(.36 if c['tier']=='Light' else 1.5),(name,'runtime timing')
        if name.startswith('RPG_FP_') and c['tier']!='Light':
            for i in range(18,73):
                cs=p.to_cs(c['poses'][i]);head_eye=p.add(cs['head'][1],p.qrot(cs['head'][0],[15.,13.,0.]))
                eye=[guard_eye[j]+(head_eye[j]-guard_eye[j])*(.4 if j==2 else .2) for j in range(3)]
                scores=[]
                for s in 'lr':
                    fist=p.lerp(cs['hand_'+s][1],cs['middle_01_'+s][1],.5);v=p.sub(fist,eye)
                    scores.append(max(abs(v[0])/max(.01,v[1]),abs(v[2])/(max(.01,v[1])*(688./1280.))))
                assert min(scores)<.95,(name,i,'both loaded fists outside nominal first-person view',scores)
    metrics[name]=dict(min_floor_margin=round(worst_floor,4),max_ball_slide_xy=round(worst_ball,4),max_hand_step_cm=round(max_step,3),hips=round(hips,3),head=round(head,3),forward=round(forward,3))
for side in 'LR':
    for tier in ['Light','Heavy','Power']:
        variants=[clips[f'RPG_Punch_{side}_{tier}_Reference1027_V{v}'] for v in (1,2,3)]
        for other in variants[1:]:
            prefix=8 if tier=='Light' else 73
            assert variants[0]['poses'][:prefix]==other['poses'][:prefix],(side,tier,'common loading/hold boundary')
            distance=max(p.length(p.sub(p.to_cs(a)['hand_'+side.lower()][1],p.to_cs(b)['hand_'+side.lower()][1]))
                         for a,b in zip(variants[0]['poses'],other['poses']))
            assert .25<distance<5.,(side,tier,'subtle timing variants',distance)
        for variant in (1,2,3):
            body=clips[f'RPG_Punch_{side}_{tier}_Reference1027_V{variant}']
            view=clips[f'RPG_FP_Punch_{side}_{tier}_Reference1027_V{variant}']
            for i in (range(17,40) if tier=='Light' else range(86,108)):
                a=p.to_cs(body['poses'][i]);b=p.to_cs(view['poses'][i])
                for bone in ['hand_'+side.lower(),'middle_01_'+side.lower()]:
                    assert p.length(p.sub(a[bone][1],b[bone][1]))<.1,(side,tier,variant,i,'TP/FP hit or withdrawal mismatch')
    for variant in (1,2,3):
        for prefix in ['RPG_','RPG_FP_']:
            heavy=clips[f'{prefix}Punch_{side}_Heavy_Reference1027_V{variant}']
            power=clips[f'{prefix}Punch_{side}_Power_Reference1027_V{variant}']
            assert heavy['poses'][:73]==power['poses'][:73],(side,prefix,'heavy/power charge selection seam')
            assert all(row==heavy['poses'][36] for row in heavy['poses'][36:73]),(side,prefix,'stationary hold loop')
        for key in ['hips','head','forward']:
            values=[metrics[f'RPG_Punch_{side}_{tier}_Reference1027_V{variant}'][key] for tier in ['Light','Heavy','Power']]
            assert values[0]<values[1]<values[2],(side,variant,key,'charged motion/extension grading',values)
path=HERE.parent.parent/'Saved/ReferenceCombat1027/review-validation.json'
path.write_text(json.dumps(metrics,indent=2))
print('PASS: all39 candidates; TP/FP anatomy, rest bone lengths, neutral roots, stance/hold seams, planted pivots, variants, existing hit timing, common strike/withdrawal paths and Light<Heavy<Power body motion/reach. Gameplay validation remains required.')
