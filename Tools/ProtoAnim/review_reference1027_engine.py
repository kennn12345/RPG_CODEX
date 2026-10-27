"""Current engine/source silhouette review and independent exported-pose FK check."""
import csv,json,math,os
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw,ImageFont
import protoanim as p
import getupcheck as g

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
audit=ROOT/'Saved/ReferenceCombat1027/FittedAuditUEFN_CalibratedAGFull'
rows=list(csv.DictReader((audit/'captures.csv').open()))
clips=json.loads((HERE/'reference1027_uefn.json').read_text())
marks=json.loads((HERE/'reference1027_landmarks.json').read_text())['frames']
reference=Path(os.environ['RPG_REFERENCE_FRAMES']);out=Path(os.environ['RPG_REFERENCE_REVIEW_OUT'])
yaw,pitch=map(math.radians,[55.39699,17.81645])
forward=np.array([math.sin(yaw)*math.cos(pitch),math.cos(yaw)*math.cos(pitch),-math.sin(pitch)])
right=np.array([-forward[1],forward[0],0.]);right/=np.linalg.norm(right);up=np.cross(forward,right)
def pose(row):
    c=clips[row['clip']];return p.to_cs(c['poses'][min(c['frames']-1,round(float(row['time'])*60))])
worst=0.;samples=0
for row in rows:
    c=clips[row['clip']]
    if float(row['time'])>(c['frames']-1)/60.+1.e-5:continue
    cs=pose(row)
    for bone,prefix in [('pelvis','pelvis'),('hand_l','left'),('hand_r','right')]:
        actual=[float(row[prefix+'_'+a]) for a in 'xyz']
        worst=max(worst,p.length(p.sub(actual,cs[bone][1])));samples+=1
assert worst<1.,('Engine/exported pose mismatch cm',worst)
def projection(v,row,size):
    centre=np.array([float(row['pelvis_x']),float(row['pelvis_y']),85.]);rel=np.array(v)-centre
    focal=size[1]*2021.2471/1242.;depth=350.+rel@forward
    return np.array([size[0]/2.+focal*(rel@right)/depth,size[1]/2.-focal*(rel@up)/depth])
def find(name,t):
    if name=='RPG_CombatStance_Reference1027' and t==0.:t=.5  # First screenshot can precede PIE viewport activation.
    return min((r for r in rows if r['clip']==name and r['view']=='0'),key=lambda r:abs(float(r['time'])-t))
def masked(path,blue):
    im=Image.open(path).convert('RGB');a=np.asarray(im).astype(float);r,g0,b=a[:,:,0],a[:,:,1],a[:,:,2]
    mask=(b>r*1.5)&(b>g0*.95)&(b>75) if blue else (r>g0*1.12)&(g0>b*1.12)&(r>100)
    if blue:mask[1130:]=False
    return Image.composite(im,Image.new('RGB',im.size,'#20262f'),Image.fromarray((mask*255).astype('uint8')))
guard=find('RPG_CombatStance_Reference1027',0.);guard_cs=pose(guard)
size=Image.open(audit/f"frame_{int(guard['frame']):05d}.png").size
engine_torso=np.linalg.norm(projection(g.head_top(guard_cs),guard,size)-projection(guard_cs['pelvis'][1],guard,size))
ref_torso=np.linalg.norm(np.array(marks['128']['head_centre'])-marks['128']['pelvis'])
ref_scale=.30;engine_scale=ref_scale*ref_torso/engine_torso
cases=[('Guard',128,'RPG_CombatStance_Reference1027',0),('Left load',72,'RPG_Punch_L_Light_Reference1027_V1',1),('Left extension',80,'RPG_Punch_L_Light_Reference1027_V1',3),('Right load',92,'RPG_Punch_R_Light_Reference1027_V1',2),('Right extension',96,'RPG_Punch_R_Light_Reference1027_V1',3),('Right recovery',112,'RPG_Punch_R_Light_Reference1027_V1',6)]
sheet=Image.new('RGB',(1920,1100),'#20262f');d=ImageDraw.Draw(sheet);font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',21)
d.text((16,12),'Reference player / current authored player — punch phases only',font=font,fill='white')
d.text((16,42),'Pelvis aligned; one torso scale from guard. Camera depth, body shapes and hidden joints remain uncertain. Not an accuracy score.',font=font,fill='#c5d1df')
for k,(label,f,name,key) in enumerate(cases):
    row=find(name,clips[name]['keys'][key]);cs=pose(row);x=k%3*640;y=80+k//3*500
    d.text((x+12,y),label,font=font,fill='white')
    sources=[(masked(reference/f'ref_{f:04d}.png',True),np.array(marks[str(f)]['pelvis']),ref_scale),
             (masked(audit/f"frame_{int(row['frame']):05d}.png",False),projection(cs['pelvis'][1],row,size),engine_scale)]
    for j,(im,pelvis,scale) in enumerate(sources):
        canvas=im.transform((320,460),Image.Transform.AFFINE,(1/scale,0,pelvis[0]-135/scale,0,1/scale,pelvis[1]-255/scale),resample=Image.Resampling.BICUBIC,fillcolor='#20262f')
        sheet.paste(canvas,(x+j*320,y+30))
sheet.save(out/'Player-punch-reference-comparison-1027.jpg',quality=94)
report=dict(engine_world_samples=samples,max_engine_export_error_cm=worst,comparison='Pelvis image translation and constant torso scale only; no new pose fitting. Cropped/hidden reference foot detail is not measurable. Authored pose audit, separate from gameplay aim/contact.')
(ROOT/'Saved/ReferenceCombat1027/engine-review-validation.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
