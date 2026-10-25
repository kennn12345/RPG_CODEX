"""Flight-only reflection and level pose construction; never changes the combat authoring rig."""
import protoanim as p

REFERENCE = p.to_cs(p.REF)

def other_side(bone):
    if bone.endswith('_l') and bone[:-1]+'r' in p.PARENT:
        return bone[:-1]+'r'
    if bone.endswith('_r') and bone[:-1]+'l' in p.PARENT:
        return bone[:-1]+'l'
    return bone

def reflect_position(v):
    return [-v[0],v[1],v[2]]

def reflect_rotation(q):
    # Reflection in X changes the two other components of an axial rotation.
    return [q[0],-q[1],-q[2],q[3]]

def mirror_pose(local):
    cs=p.to_cs(local)
    desired={}
    for bone in p.BONES:
        source=other_side(bone)
        correction=p.qmul(p.qconj(reflect_rotation(REFERENCE[source][0])),REFERENCE[bone][0])
        desired[bone]=(p.qnorm(p.qmul(reflect_rotation(cs[source][0]),correction)),reflect_position(cs[source][1]))
    out={}
    for bone in p.BONES:
        parent=p.PARENT[bone];q,t=desired[bone]
        if parent is None:
            out[bone]=(q,t)
        else:
            pq,pt=desired[parent];inv=p.qconj(pq)
            out[bone]=(p.qnorm(p.qmul(inv,q)),p.qrot(inv,p.sub(t,pt)))
    out['root']=([0.,0.,0.,1.],[0.,0.,0.])
    return out

def level_pose(local):
    """Remove the standing idle's twist and lateral bias without shortening the limbs."""
    cs=p.to_cs(local);reflected=p.to_cs(mirror_pose(local))
    rotations={b:p._nlerp(cs[b][0],reflected[b][0],0.5) for b in p.BONES}
    out={}
    for bone in p.BONES:
        parent=p.PARENT[bone]
        q=rotations[bone] if parent is None else p.qmul(p.qconj(rotations[parent]),rotations[bone])
        # Keep the actual skeleton's segment lengths, rather than averaging CS positions.
        t=list(p.REF[bone][1])
        if bone=='pelvis':
            t=list(local[bone][1]);t[0]=0.
        out[bone]=(p.qnorm(q),t)
    out['root']=([0.,0.,0.,1.],[0.,0.,0.])
    return out

def flatten_pose(local):
    """Put the hip-to-chest axis in the horizontal plane, about the hips."""
    out={b:(list(q),list(t)) for b,(q,t) in local.items()}
    cs=p.to_cs(out);direction=p.sub(cs['spine_05'][1],cs['pelvis'][1])
    import math
    angle=math.degrees(math.atan2(direction[2],direction[1]))
    out['pelvis']=(p.qnorm(p.qmul(p.qaxis([1,0,0],-angle),out['pelvis'][0])),out['pelvis'][1])
    return out

def blend_pose(a,b,u):
    # Quintic easing has no velocity or acceleration discontinuity at the ends.
    u=max(0.,min(1.,u));u=u*u*u*(u*(u*6.-15.)+10.)
    return {bone:(p._nlerp(a[bone][0],b[bone][0],u),p.lerp(a[bone][1],b[bone][1],u)) for bone in p.BONES}

def action_curve(solved,tension=0.7):
    """Solved rotations with shared key velocities; no IK between flight keys.

    Quintic Hermite segments share key velocities and zero parametric acceleration.
    Quaternion normalization preserves derivative continuity. Entry/exit
    velocity is zero, while intermediate poses no longer produce a brief stop.
    """
    times=[t for t,_ in solved]
    tracks={}
    for bone in p.BONES:
        rotations=[];translations=[]
        for _,pose in solved:
            q=list(pose[bone][0])
            if rotations and sum(a*b for a,b in zip(rotations[-1],q))<0:
                q=[-v for v in q]
            rotations.append(q);translations.append(list(pose[bone][1]))
        def derivatives(values,quaternion=False):
            out=[]
            for i,value in enumerate(values):
                if i==0 or i==len(values)-1:
                    out.append([0.]*len(value));continue
                dt=times[i+1]-times[i-1]
                v=[(b-a)/dt for a,b in zip(values[i-1],values[i+1])]
                if quaternion:
                    radial=sum(a*b for a,b in zip(value,v))
                    v=[b-a*radial for a,b in zip(value,v)]
                out.append([tension*a for a in v])
            return out
        tracks[bone]=(rotations,translations,derivatives(rotations,True),derivatives(translations))
    def sample(t):
        t=max(times[0],min(times[-1],t))
        i=min(max((j for j,kt in enumerate(times) if kt<=t),default=0),len(times)-2)
        dt=times[i+1]-times[i];u=(t-times[i])/dt
        u2=u*u;u3=u2*u;u4=u3*u;u5=u4*u
        h0=1-10*u3+15*u4-6*u5;h1=1-h0
        v0=u-6*u3+8*u4-3*u5;v1=-4*u3+7*u4-3*u5
        out={}
        for bone,(rotations,translations,rvel,tvel) in tracks.items():
            def curve(values,velocities):
                return [h0*a+h1*b+dt*(v0*c+v1*d) for a,b,c,d in zip(values[i],values[i+1],velocities[i],velocities[i+1])]
            out[bone]=(p.qnorm(curve(rotations,rvel)),curve(translations,tvel))
        return out
    return sample

def bank_pose(level,turn):
    """One canonical right turn; the left turn is its exact skeletal reflection."""
    local={b:(list(q),list(t)) for b,(q,t) in level.items()}
    cs=p.to_cs(local)
    forward=p.norm(p.sub(cs['spine_05'][1],cs['pelvis'][1]))
    side=p.norm(p.sub(cs['upperarm_l'][1],cs['upperarm_r'][1]))
    up=p.norm(p.cross(side,forward))
    def rotate(bone,axis,degrees):
        q=p.qmul(p.qaxis(axis,degrees),cs[bone][0]);parent=p.PARENT[bone]
        local[bone]=(p.qnorm(p.qmul(p.qconj(cs[parent][0]),q)),local[bone][1])
        p.descend(cs,local,parent)
    # Gentle curvature; the component supplies the whole-body bank. No shoulder shrug.
    rotate('spine_02',up,-2.5)
    rotate('spine_05',up,5.)
    for s,sign in [('l',1),('r',-1)]:
        upper='upperarm_'+s;elbow='lowerarm_'+s
        direction=p.norm(p.sub(cs[elbow][1],cs[upper][1]))
        rotate(upper,p.cross(direction,p.mul(side,sign)),30. if s=='l' else 2.)
        # Long outside arm, tucked inside elbow, without moving the shoulder pivot.
        direction=p.norm(p.sub(cs[elbow][1],cs[upper][1]))
        forearm=p.norm(p.sub(cs['hand_'+s][1],cs[elbow][1]))
        hinge=p.cross(direction,forearm)
        if p.length(hinge)>0.001:
            rotate(elbow,hinge,-15. if s=='l' else 16.)
        thigh='thigh_'+s;calf='calf_'+s
        direction=p.norm(p.sub(cs[calf][1],cs[thigh][1]))
        rotate(thigh,p.cross(direction,side),-4.)
        direction=p.norm(p.sub(cs[calf][1],cs[thigh][1]))
        shin=p.norm(p.sub(cs['foot_'+s][1],cs[calf][1]))
        hinge=p.cross(direction,shin)
        if p.length(hinge)>0.001:
            rotate(calf,hinge,6.)
    return mirror_pose(local) if turn<0 else local


def release_stride_pose(local):
    """Balanced core with the deliberate airborne running gesture retained.

    Neutral flight is mirrored. Release ends in an opposing arm/leg stride;
    averaging that gesture would pull both feet together before locomotion.
    """
    out=level_pose(local)
    roots={'upperarm_l','upperarm_r','thigh_l','thigh_r'}
    for bone in p.BONES:
        ancestor=bone
        while ancestor:
            if ancestor in roots:
                out[bone]=(list(local[bone][0]),list(out[bone][1]))
                break
            ancestor=p.PARENT[ancestor]
    return out
