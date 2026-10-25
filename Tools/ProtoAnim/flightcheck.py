"""Check flight clips without changing assets. Use RPG_SKEL=uefn for the GASP player.
Run: <UE Python> Tools/ProtoAnim/flightcheck.py
"""
import json, math
import skel_profile as SP

clips=json.load(open(SP.path('clips','.json')))
actions={'RPG_Proto_FlyTakeoff','RPG_Proto_FlyAccelerate','RPG_Proto_FlyBoost','RPG_Proto_FlyRelease'}
for name in [n for n in clips if n.startswith('RPG_Proto_Fly')]:
    clip=clips[name]
    assert clip['frames']==len(clip['poses'])
    assert (clip['frames']-1)%(clip['fps']//30)==0, 'Compression requires a whole 30 fps frame count'
    seam=angular_speed=0.0
    for bone in clip['bones']:
        for pose in clip['poses']:
            q,t=pose[bone]
            assert all(math.isfinite(v) for v in q+t),(name,bone,'non-finite transform')
            assert abs(sum(v*v for v in q)-1)<1e-5,(name,bone,'unnormalized quaternion')
            assert pose['root']==[[0.,0.,0.,1.],[0.,0.,0.]],(name,'moving root')
        a,b=clip['poses'][0][bone],clip['poses'][-1][bone]
        seam=max(seam,abs(1-abs(sum(x*y for x,y in zip(a[0],b[0])))),max(abs(x-y) for x,y in zip(a[1],b[1])))
        for a,b in zip(clip['poses'],clip['poses'][1:]):
            dot=min(1.0,abs(sum(x*y for x,y in zip(a[bone][0],b[bone][0]))))
            angular_speed=max(angular_speed,math.degrees(2*math.acos(dot))*clip['fps'])
    if name not in actions:assert seam<1e-6,(name,'loop seam',seam)
    limit=750 if name=='RPG_Proto_FlyRelease' else (150 if name in actions else 45)
    assert angular_speed<limit,(name,'rotation hitch',angular_speed)
    print(f'{name}: PASS; locked root, finite normalized transforms, validated motion; max angular speed {angular_speed:.2f} deg/s')

# Geometric regression for the raised-shoulder/right-turn defect. Compare actual
# evaluated bone positions, independently of the authored channel names.
import protoanim as p
bones=['upperarm_l','upperarm_r','lowerarm_l','lowerarm_r','hand_l','hand_r',
       'thigh_l','thigh_r','calf_l','calf_r','foot_l','foot_r']
def other(b):return b[:-1]+('r' if b[-1]=='l' else 'l')
def error(a,b):
    return max(p.length(p.sub([-a[k][1][0],a[k][1][1],a[k][1][2]],b[other(k)][1])) for k in bones)
for name,c in clips.items():
    if not name.startswith('RPG_Proto_Fly'):continue
    if 'Bank' not in name:
        neutral_frames=c['poses']
        # Release deliberately develops an opposing arm/leg stride after catch.
        if name=='RPG_Proto_FlyRelease':neutral_frames=neutral_frames[:int(0.22*c['fps'])+1]
        worst=max(error(p.to_cs(frame),p.to_cs(frame)) for frame in neutral_frames)
        # Preserve Manny's slightly different reference segment offsets rather
        # than deforming the rig to eliminate its sub-millimetre differences.
        assert worst<0.01,(name,'uneven neutral limbs/shoulders',worst)
    for frame in c['poses']:
        for bone in bones:
            assert abs(p.length(frame[bone][1])-p.length(p.REF[bone][1]))<0.001,(name,bone,'changed segment length')
    if name in ['RPG_Proto_FlyForward','RPG_Proto_FlyFast','RPG_Proto_FlyAccelerate','RPG_Proto_FlyBoost']:
        for frame in c['poses']:
            cs=p.to_cs(frame);axis=p.sub(cs['spine_05'][1],cs['pelvis'][1])
            pitch=math.degrees(math.atan2(axis[2],math.hypot(axis[0],axis[1])))
            assert abs(pitch)<0.05,(name,'non-flat entry/cruise',pitch)
    if name in ['RPG_Proto_FlyHover','RPG_Proto_FlyTakeoff']:
        for frame in c['poses']:
            cs=p.to_cs(frame);axis=p.sub(cs['spine_05'][1],cs['pelvis'][1])
            pitch=math.degrees(math.atan2(axis[2],math.hypot(axis[0],axis[1])))
            assert pitch>75,(name,'hover is not upright',pitch)
for left,right in [('FlyBankL','FlyBankR'),('FlyBankFastL','FlyBankFastR')]:
    a,b=clips['RPG_Proto_'+left],clips['RPG_Proto_'+right]
    worst=max(error(p.to_cs(x),p.to_cs(y)) for x,y in zip(a['poses'],b['poses']))
    assert worst<0.001,(left,right,'unmatched turn directions',worst)
print('PASS: upright hover, flat cruise, level shoulders, mirrored turns and preserved segment lengths')

# Preserve the reference's intentional stride without reintroducing core bias.
stride=p.to_cs(clips['RPG_Proto_FlyRelease']['poses'][-1])
assert abs(stride['upperarm_l'][1][2]-stride['upperarm_r'][1][2])<0.01,'release shoulder pivot bias'
assert abs(stride['foot_l'][1][2]-stride['foot_r'][1][2])>15,'release lost leg stride'
assert abs(stride['foot_l'][1][1]-stride['foot_r'][1][1])>15,'release feet collapse together'
assert abs(stride['hand_l'][1][2]-stride['hand_r'][1][2])>10,'release lost arm stride'
print('PASS: balanced release core and intentional opposing limb stride')
