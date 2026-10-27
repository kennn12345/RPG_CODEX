"""Validate the shipped capture transforms, tier progression, small variants and presentation hit agreement."""
import json
import math
from pathlib import Path
import protoanim as p
import getupcheck as g
from getups import CONTACT

root=Path(__file__).resolve().parent
clips=json.loads((root/'capture_punches8630_uefn.json').read_text())
stances=json.loads((root/'capture_stance8619_uefn.json').read_text())
assert len(clips)==36
metrics={}
for name,c in clips.items():
    first=name.startswith('RPG_FP_')
    # Charged clips retain their approved authored stance; this pass refines only the standard punches.
    rest=stances['RPG_FP_CombatStance_Capture8619' if first else 'RPG_CombatStance_Capture8619']['poses'][0] if c['tier']=='Light' else c['poses'][0]
    assert c['frames']==len(c['poses']) and c['fps']==60,(name,'frames')
    assert c['poses'][0]==rest and c['poses'][-1]==rest,(name,'stance seam')
    base=p.to_cs(rest);worst=999.;hips=0.;head=0.;forward=-999.
    for i,local in enumerate(c['poses']):
        assert local['root']==[[0.,0.,0.,1.],[0.,0.,0.]],(name,i,'root')
        for b,(q,t) in local.items():
            assert all(math.isfinite(x) for x in q+t),(name,i,b,'finite')
            assert abs(sum(x*x for x in q)-1.)<1.e-4,(name,i,b,'quaternion')
            if b not in ['root','pelvis'] and not b.startswith('ik_'):assert p.length(p.sub(t,rest[b][1]))<1.e-5,(name,i,b,'bone length')
        cs=p.to_cs(local)
        for bone,skin in CONTACT:
            worst=min(worst,cs[bone][1][2]-skin)
        hips=max(hips,p.length(p.sub(cs['pelvis'][1],base['pelvis'][1])))
        head=max(head,p.length(p.sub(cs['head'][1],base['head'][1])))
        forward=max(forward,cs['hand_'+c['hand']][1][1])
    assert worst>=-.5,(name,'floor',worst)
    issues=g.check_clip(name,c);assert not issues,(name,'anatomy',issues)
    if not first:metrics[name]=dict(hips=round(hips,3),head=round(head,3),forward=round(forward,3))
(root.parent.parent/'Saved/Mocap8630/metrics.json').write_text(json.dumps(metrics,indent=2))
for side in 'LR':
    for tier in ['Light','Heavy','Power']:
        variants=[clips[f'RPG_Punch_{side}_{tier}_V{v}'] for v in [1,2,3]]
        b=variants[0];impact=round(b['impact']*60)
        for other in variants[1:]:
            assert b['poses'][:8]==other['poses'][:8],(side,tier,'shared load')
            distance=p.length(p.sub(p.to_cs(b['poses'][impact])['hand_'+side.lower()][1],p.to_cs(other['poses'][impact])['hand_'+side.lower()][1]))
            assert .25<distance<4.,(side,tier,'subtle variant',distance)
        for v in [1,2,3]:
            body=clips[f'RPG_Punch_{side}_{tier}_V{v}'];view=clips[f'RPG_FP_Punch_{side}_{tier}_V{v}']
            for i in range(round((body['impact']-.07)*60),round((body['impact']+.05)*60)+1):
                a,b=p.to_cs(body['poses'][i]),p.to_cs(view['poses'][i])
                for bone in ['hand_'+side.lower(),'middle_01_'+side.lower()]:
                    assert p.length(p.sub(a[bone][1],b[bone][1]))<.1,(side,tier,v,i,'presentation hit mismatch')
    for prefix in ['RPG_Punch_','RPG_FP_Punch_']:
        light=clips[f'{prefix}{side}_Light_V1'];heavy=clips[f'{prefix}{side}_Heavy_V1'];power=clips[f'{prefix}{side}_Power_V1']
        rest=p.to_cs(light['poses'][0]);load=p.to_cs(light['poses'][round(.12*60)])
        assert load['hand_'+side.lower()][1][1]<rest['hand_'+side.lower()][1][1]-3.,(side,prefix,'standard windup missing')
        assert heavy['poses'][:73]==power['poses'][:73],(side,'charged tier release seam')
    for key in ['hips','head','forward']:
        for v in [1,2,3]:
            values=[metrics[f'RPG_Punch_{side}_{tier}_V{v}'][key] for tier in ['Light','Heavy','Power']]
            assert values[0]<values[1]<values[2],(side,v,key,'tier progression',values)
(root.parent.parent/'Saved/Mocap8630/metrics.json').write_text(json.dumps(metrics,indent=2))
print('PASS: all 36 captured punches; anatomy, floor, root, stance/charged hold seams, standard windup, distinct small variants, identical hit-window hand paths and Light < Heavy < Power body movement/extension')
