"""Additional get-up routes. Preserve the existing 14 clips and their matching starts.

RPG_SKEL=uefn python getup_variety.py [filter]
Writes getup_variety_uefn.json separately so the existing large manifest stays untouched.
Import with RPG_CLIP_FILE=<absolute manifest> and RPG_IMPORT_ONLY=RPG_GetUp_.
"""
import json, math, os, sys
import protoanim as pa
import getups as g
from flight_symmetry import mirror_pose, blend_pose, action_curve


def closing_steps(start):
    """Left-knee recovery leaves the other foot forward. Make two small real steps into GASP's idle."""
    cs = pa.to_cs(start)
    p = cs['pelvis'][1]
    hands = (pa.add(cs['hand_l'][1],[8,0,0]),pa.add(cs['hand_r'][1],[-8,0,0]))
    left, right = cs['foot_l'][1], cs['foot_r'][1]
    final_l, final_r = pa.IDLE_CS['foot_l'][1], pa.IDLE_CS['foot_r'][1]
    def standing(hips, feet, head=-4):
        channels=g.pose(hips, [0, 0, 0], [0, 0, 0], [0, 0, 0], [0, head, 0], hands, feet,
            ([.4, -.3, -1], [-.4, -.3, -1]), ([.1, 1, 0], [-.1, 1, 0]), turn=(0, 0))
        channels['square']=[0]
        return pa.build_pose(channels)
    # Load the planted right foot, lift/plant the left, then gather the right foot under the hips.
    load = standing([p[0]-5, p[1]+3, p[2]-1], (left, right))
    lift_l = standing([p[0]-4, p[1]+5, p[2]], ([final_l[0], final_l[1]-8, final_l[2]+10], right))
    plant_l = standing([2, 4, p[2]], (final_l, right))
    lift_r = standing([7, 2, p[2]], (final_l, [final_r[0], final_r[1]+10, final_r[2]+9]))
    settle = standing(pa.IDLE_CS['pelvis'][1], (final_l, final_r), head=0)
    return [(0., start), (.24, load), (.56, lift_l), (.82, plant_l), (1.15, lift_r), (1.47, settle)]


def left_lead(name, source, keys):
    branch = next(k[0] for k in keys if k[1] is g.FD_FOURS)
    finish = keys[-1][0]
    stop = round(finish * source['fps'])
    frames = []
    for i, local in enumerate(source['poses'][:stop+1]):
        t = i / source['fps']
        # All candidate matching frames are byte-for-byte the existing start.
        if t <= branch: frames.append(local)
        else: frames.append(blend_pose(local, mirror_pose(local), (t-branch)/.35))
    curve_keys = closing_steps(frames[-1])
    curve = action_curve(curve_keys, tension=.55)
    count = round(curve_keys[-1][0] * source['fps'])
    for i in range(1, count+1): frames.append(curve(i/source['fps']))
    end_pose=frames[-1]
    closing_blend=.55
    for i in range(1,round(closing_blend*source['fps'])+1):
        frames.append(blend_pose(end_pose,pa.IDLE,i/(closing_blend*source['fps'])))
    while (len(frames)-1) % (source['fps']//30): frames.append(pa.IDLE)
    return dict(source, name=name, frames=len(frames), poses=frames,
                keys=[k[0] for k in keys] + [finish+t for t,_ in curve_keys[1:]] + [finish+curve_keys[-1][0]+closing_blend])


def right_roll(name, source):
    # Reflect the left-roll recovery through actual skeleton frames; rejoin the original
    # finish while both knees are still down, before the forward foot is lifted.
    branch = next(k[0] for k in g.FACE_UP if k[1] is g.FD_FOURS)
    frames=[]
    for i,local in enumerate(source['poses']):
        t=i/source['fps']
        reflected=mirror_pose(local)
        frames.append(reflected if t<=branch else blend_pose(reflected,local,(t-branch)/.35))
    return dict(source,name=name,poses=frames)


if __name__ == '__main__':
    raise SystemExit('Procedural get-up variants are retired; gameplay uses only Capture8614 and Capture8611.')
    if pa.SP.NAME != 'uefn': raise SystemExit('These contact targets require RPG_SKEL=uefn')
    from preview import render
    baseline=json.load(open(pa.SP.path('clips','.json')))
    path=pa.SP.path('getup_variety','.json')
    out=json.load(open(path)) if os.path.exists(path) else {}
    filters=sys.argv[1:]
    for original,keys in g.SPECS.items():
        name=original+'_LeftLead'
        if filters and not any(f in name for f in filters): continue
        clip=left_lead(name,baseline[original],keys)
        out[name]=clip; g.report(name,clip)
        render(clip,os.path.join(pa.HERE,'previews',name+'.png'),clip['keys'])
    name='RPG_GetUp_FaceUp_RollRight'
    if not filters or any(f in name for f in filters):
        clip=right_roll(name,baseline['RPG_GetUp_FaceUp']); out[name]=clip; g.report(name,clip)
        render(clip,os.path.join(pa.HERE,'previews',name+'.png'),clip['keys'])
    with open(path,'w') as f: json.dump(out,f)
    print('VARIETY MANIFEST',path)
