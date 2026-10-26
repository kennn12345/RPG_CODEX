"""Structural and distinct-motion checks; anatomy/smoothness reports remain separate, unchanged validators."""
import json, math
import protoanim as p
import getups as g
from getupsmooth import analyse

base=json.load(open(p.SP.path('clips','.json')))
extra=json.load(open(p.SP.path('getup_variety','.json')))
assert len(extra)==15, len(extra)
primary=['pelvis','spine_05','head','upperarm_l','lowerarm_l','hand_l','upperarm_r','lowerarm_r','hand_r','thigh_l','calf_l','foot_l','thigh_r','calf_r','foot_r']
for name,clip in extra.items():
    assert clip['frames']==len(clip['poses'])
    for pose in clip['poses']:
        assert pose['root']==[[0.,0.,0.,1.],[0.,0.,0.]],name
        for bone,(q,t) in pose.items():
            assert all(math.isfinite(v) for v in q+t),(name,bone)
            assert abs(sum(x*x for x in q)-1)<1e-5,(name,bone,'quaternion')
        for bone in primary:
            # The sampled UEFN idle differs slightly from bind pose, and its paired calf
            # lengths differ by .012cm. Reflection must stay within .025cm of the real idle.
            assert abs(p.length(pose[bone][1])-p.length(p.IDLE[bone][1]))<.025 or bone=='pelvis',(name,bone,'segment')
    for bone,(q,t) in clip['poses'][-1].items():
        assert max(abs(a-b) for a,b in zip(t,p.IDLE[bone][1]))<1e-4,(name,bone,'idle translation')
        assert 1-abs(sum(a*b for a,b in zip(q,p.IDLE[bone][0])))<1e-6,(name,bone,'idle rotation')
    if name.endswith('_LeftLead'):
        original=name[:-len('_LeftLead')]
        assert clip['poses'][:37]==base[original]['poses'][:37],(name,'matching starts changed')
        kneel=next(k[0] for k in g.SPECS[original] if k[1] is g.FD_KNEEL)
        cs=p.to_cs(clip['poses'][round(kneel*clip['fps'])])
        assert cs['calf_l'][1][2]+15<cs['calf_r'][1][2],(name,'left knee did not support')
        rise=next(k[0] for k in g.SPECS[original] if k[1] is g.FD_RISE)
        original_cs=p.to_cs(base[original]['poses'][round(rise*clip['fps'])])
        alternate_cs=p.to_cs(clip['poses'][round(rise*clip['fps'])])
        def support_error(pose,foot):
            com=p.center_of_mass(pose);f=pose[foot][1]
            return math.hypot(com[0]-f[0],com[1]-f[1])
        assert support_error(alternate_cs,'foot_r')<=support_error(original_cs,'foot_l')+1.,(name,'rise support worsened')
    flips=[x for x in analyse(name,clip,report=False) if x[2]=='flip']
    assert not flips,(name,flips)
    print(name,'PASS: finite/root/lengths/idle/start/alternate knee/rise support/no flips')
