"""Make an honestly labelled draft comparison from the saved engine audit and reference frames."""
import os
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw,ImageFont,ImageFilter

root=Path(__file__).resolve().parents[2]
reference=Path(os.environ['RPG_REFERENCE_FRAMES'])
audit=root/'Saved/ReferenceCombat1027/FittedAuditUEFN_Yaw56'
out=Path(os.environ['RPG_REFERENCE_REVIEW_OUT']);out.mkdir(parents=True,exist_ok=True)
font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',24)
small=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',18)

def player(path,blue):
    im=Image.open(path).convert('RGB')
    if blue:im=im.crop((0,0,im.width,1130)) # Social caption/UI below the player is not reference motion.
    a=np.asarray(im).astype(float);r,g,b=a[:,:,0],a[:,:,1],a[:,:,2]
    mask=((b>r*1.5)&(b>g*.95)&(b>75)) if blue else ((r-g>25)&(g-b>22)&(r>140))
    y,x=np.where(mask);box=(max(0,int(x.min())-8),max(0,int(y.min())-8),min(im.width,int(x.max())+9),min(im.height,int(y.max())+9))
    matte=Image.fromarray((mask*255).astype('uint8')).filter(ImageFilter.MaxFilter(5))
    isolated=Image.composite(im,Image.new('RGB',im.size,(36,42,50)),matte).crop(box)
    isolated.thumbnail((360,440));return isolated

sheet=Image.new('RGB',(1600,1250),(27,32,39));d=ImageDraw.Draw(sheet)
d.text((24,18),'REFERENCE PUNCH DRAFT — PLAYER ONLY',font=font,fill='white')
d.text((24,53),'Staging review. Camera/proportions differ; no certified accuracy percentage. Gameplay is still using the approved baseline.',font=small,fill='#c7d0db')
cases=[('Stationary guard',128,1),('Left extension',80,23),('Right loading arc',92,91),('Right cross',96,95)]
for k,(label,ref,frame) in enumerate(cases):
    x=(k%2)*800;y=100+(k//2)*570
    d.text((x+18,y),label,font=font,fill='white')
    for j,(file,blue,caption) in enumerate([(reference/f'ref_{ref:04d}.png',True,f'Reference f{ref}'),(audit/f'frame_{frame:05d}.png',False,f'UEFN draft f{frame}')]):
        im=player(file,blue);left=x+j*390+(390-im.width)//2;top=y+45
        sheet.paste(im,(left,top));d.text((x+j*390+18,y+505),caption,font=small,fill='#c7d0db')
sheet.save(out/'Player-punch-reference-draft.jpg',quality=94)
print(out/'Player-punch-reference-draft.jpg')
