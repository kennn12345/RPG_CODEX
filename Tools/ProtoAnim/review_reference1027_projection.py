"""Compare current exported poses with visible punch landmarks; no fitting or accuracy score."""
import json, math, os
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFont
import protoanim as p
import getupcheck as g

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
marks = json.loads((HERE / 'reference1027_landmarks.json').read_text())['frames']
camera = json.loads((HERE / 'captures/reference1027_fitted_poses.json').read_text())['camera']
clips = json.loads((HERE / 'reference1027_uefn.json').read_text())
reference = Path(os.environ['RPG_REFERENCE_FRAMES'])
out = Path(os.environ['RPG_REFERENCE_REVIEW_OUT'])
out.mkdir(parents=True, exist_ok=True)
yaw, pitch, distance, focal, cx, cy = camera
yaw, pitch = math.radians(yaw), math.radians(pitch)
forward = np.array([math.sin(yaw)*math.cos(pitch), math.cos(yaw)*math.cos(pitch), -math.sin(pitch)])
right = np.array([-forward[1], forward[0], 0.]); right /= np.linalg.norm(right)
up = np.cross(forward, right)

def project(point):
    v = np.asarray(point)-[0., 0., 85.]
    depth = distance+v@forward
    return np.array([cx+focal*(v@right)/depth, cy-focal*(v@up)/depth])

cases = [('Guard',128,'RPG_CombatStance_Reference1027',0.),
         ('Left load',72,'RPG_Punch_L_Light_Reference1027_V1',.12),
         ('Left extension',80,'RPG_Punch_L_Light_Reference1027_V1',.36),
         ('Right load',92,'RPG_Punch_R_Light_Reference1027_V1',.24),
         ('Right extension',96,'RPG_Punch_R_Light_Reference1027_V1',.36),
         ('Right recovery',112,'RPG_Punch_R_Light_Reference1027_V1',.72)]
sheet = Image.new('RGB',(1224,1302),'#20262f');draw=ImageDraw.Draw(sheet)
font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',18)
draw.text((12,8),'Current exported poses: orange / filmed player: blue',font=font,fill='white')
draw.text((12,32),'Fixed calibrated camera; pelvis translation aligned only. Diagnostic, not an accuracy score.',font=font,fill='white')
report=[]
for k,(label,frame,name,t) in enumerate(cases):
    # Follow the authored phase keys after reference-timing refinement.
    t=clips[name]['keys'][[0,1,3,2,3,6][k]]
    clip=clips[name];pose=clip['poses'][round(t*clip['fps'])];cs=p.to_cs(pose)
    observed=marks[str(frame)]
    projected={bone:project(g.head_top(cs) if bone=='head_centre' else cs[bone][1]) for bone in observed}
    offset=np.array(observed['pelvis'])-projected['pelvis']
    projected={b:v+offset for b,v in projected.items()}
    errors={b:round(float(np.linalg.norm(v-observed[b])),2) for b,v in projected.items() if b!='pelvis'}
    im=Image.open(reference/f'ref_{frame:04d}.png').convert('RGB');a=np.asarray(im).astype(float)
    red,green,blue=a[:,:,0],a[:,:,1],a[:,:,2]
    mask=(blue>red*1.5)&(blue>green*.95)&(blue>75)
    mask[1130:]=False  # Exclude the social caption below the motion reference.
    im=Image.composite(im,Image.new('RGB',im.size,'#20262f'),Image.fromarray((mask*255).astype('uint8')))
    d=ImageDraw.Draw(im)
    for b,v in projected.items():
        target=observed[b];d.line([tuple(target),tuple(v)],fill='#cccccc',width=2)
        d.ellipse((v[0]-7,v[1]-7,v[0]+7,v[1]+7),fill='#ffad36')
    im=im.resize((408,621));x=k%3*408;y=60+k//3*621;sheet.paste(im,(x,y))
    draw.text((x+8,y+4),f'{label}: reference f{frame}',font=font,fill='white')
    report.append(dict(label=label,frame=frame,clip=name,time=t,pelvis_translation_px=offset.tolist(),joint_discrepancy_px=errors))
sheet.save(out/'Player-punch-projection-review-1027.jpg',quality=94)
(ROOT/'Saved/ReferenceCombat1027/current-projection-review.json').write_text(json.dumps(dict(method='Fixed Fit-D camera and pelvis-only image translation; no pose/scale fitting. Rig proportions, occlusion and camera uncertainty remain. Discrepancies are diagnostics, not certified accuracy.',cases=report),indent=2))
print(json.dumps(report,indent=2))
