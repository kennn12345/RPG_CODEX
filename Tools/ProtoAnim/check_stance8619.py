"""Check the actual captured stance bake: facing, planted feet, anatomy, and periodicity."""
import json
import math
from pathlib import Path
import protoanim as p
import getupcheck as g

root=Path(__file__).resolve().parent
clips=json.loads((root/'capture_stance8619_uefn.json').read_text())
for name,clip in clips.items():
    poses=clip['poses'];assert clip['frames']==len(poses) and clip['fps']==60
    assert poses[0]==poses[-1],(name,'loop seam')
    ref=p.to_cs(poses[0]);feet=[];hands=[]
    for i,local in enumerate(poses):
        assert set(local)==set(p.BONES)
        assert local['root']==[[0.,0.,0.,1.],[0.,0.,0.]],(name,i,'root')
        assert local['pelvis']==poses[0]['pelvis'],(name,i,'pelvis turning or travel')
        for b,(q,t) in local.items():
            assert all(math.isfinite(v) for v in q+t),(name,i,b)
            assert abs(sum(v*v for v in q)-1.)<1.e-4,(name,i,b,'quaternion')
        cs=p.to_cs(local)
        hands.append(cs['hand_l'][1])
        for side in 'lr':
            for b in ['foot_'+side,'ball_'+side]:
                assert p.length(p.sub(cs[b][1],ref[b][1]))<.05,(name,i,b,'foot slide')
            assert cs['ball_'+side][1][2]>=2.7,(name,i,side,'toe floor')
            for upper,lower,end,key in [('upperarm','lowerarm','hand','elbow'),('thigh','calf','foot','knee')]:
                bend,swivel=g.hinge(cs,side,upper,lower,end,key)
                assert -5.<=bend<=150. and abs(swivel)<10.,(name,i,side,key,bend,swivel)
    motion=max(p.length(p.sub(v,hands[0])) for v in hands)
    assert motion>.05,(name,'frozen hands')
    print('PASS',name,len(poses),'frames; identity root, fixed hips/facing, planted feet, clean hinges and exact loop seam; hand travel',round(motion,3),'cm')
    g.check_clip(name,clip)
