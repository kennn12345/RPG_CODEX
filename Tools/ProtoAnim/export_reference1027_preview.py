"""Export saved TP/FP gameplay review captures without claiming a reference-match score."""
import csv,os,sys
from fractions import Fraction
from pathlib import Path
if os.environ.get('RPG_VIDEO_DEPS'):sys.path.insert(0,os.environ['RPG_VIDEO_DEPS'])
import av
from PIL import Image,ImageDraw,ImageFont

root=Path(__file__).resolve().parents[2]
tag=os.environ.get('RPG_REFERENCE_PLAY_TAG','Default')
tp=root/f'Saved/ReferenceCombat1027/GameplayTP_{tag}'
fp=root/f'Saved/ReferenceCombat1027/GameplayFP_{tag}'
out=Path(os.environ['RPG_REFERENCE_REVIEW_OUT']);out.mkdir(parents=True,exist_ok=True)
rows=lambda d:list(csv.DictReader((d/'captures.csv').open()))
third,first=rows(tp),rows(fp)
def releases(data):
    result={}
    for r in data:
        if int(r['phase'])==2:result.setdefault(r['case'],float(r['time']))
    return result
third_release,first_release=releases(third),releases(first)
font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',21)
small=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',17)
path=out/(os.environ.get('RPG_REFERENCE_PREVIEW_NAME','Player-punch-preview-1027')+'.mp4')
video=av.open(str(path),'w');stream=video.add_stream('libx264',rate=30)
stream.width=1280;stream.height=408;stream.pix_fmt='yuv420p';stream.options={'crf':'20','preset':'fast'}
start=float(third[0]['time']);last=None
def encode(canvas,t):
    frame=av.VideoFrame.from_image(canvas);frame.pts=round(t*60000);frame.time_base=Fraction(1,60000)
    for packet in stream.encode(frame):video.mux(packet)
for row in third:
    t=float(row['time']);case=row['case']
    # Separate PIE sessions can accumulate a few frames of setup delay. Match
    # each hand/tier by its actual release, never show a different case beside it.
    target=t-third_release.get(case,t)+first_release.get(case,t)
    same_case=[r for r in first if r['case']==case]
    other=min(same_case,key=lambda r:abs(float(r['time'])-target))
    canvas=Image.new('RGB',(1280,408),'#20262f');d=ImageDraw.Draw(canvas)
    tiers=['Standard','Heavy','Power'];tier=int(row['tier'])
    side='Left' if int(row['left']) else 'Right'
    d.text((14,9),f'{tiers[tier]} — {side.lower()} punch, windup and recovery',font=font,fill='white')
    for x,folder,r in [(0,tp,row),(640,fp,other)]:
        image=Image.open(folder/f"frame_{int(r['frame']):05d}.png").convert('RGB').resize((640,344))
        canvas.paste(image,(x,40))
    d.text((14,388),'Third person',font=small,fill='white');d.text((652,388),'First person',font=small,fill='white')
    d.text((890,388),'Charged style adaptation' if tier else 'Reference match in progress',font=small,fill='#c5d1df')
    encode(canvas,t-start);last=canvas
for n in range(1,21):encode(last,float(third[-1]['time'])-start+n/20.)
for packet in stream.encode():video.mux(packet)
video.close()
last.save(path.with_suffix('.png'))
with av.open(str(path)) as check:
    decoded=list(check.decode(video=0));assert len(decoded)==len(third)+20
    assert all(f.width==1280 and f.height==408 for f in decoded)
    print('PREVIEW',path,'frames',len(decoded),'seconds',round(float(decoded[-1].time),3))
