"""Check engine-evaluated get-up arms, excluding the unconstrained raw fall.
Diagnostic bounds: elbow >= -5 deg, hinge sideways <= 15, shoulder -60..180.
These are regression bounds, not a clinical anatomical model or clip-wide certification."""
import csv,json,sys
from pathlib import Path
# Run with RPG_SKEL=uefn; input is arms.csv from RPG_GETUP_ARM_TRACE=1.
import getupcheck as g

rows=list(csv.DictReader(Path(sys.argv[1]).open()))
frames={}
for r in rows:
    if float(r['montage_time'])<0 or (r['state']=='2' and r['returning']=='0'):continue
    k=(r['episode'],r['time'])
    cs=frames.setdefault(k,dict(meta=r,pose={}))['pose']
    cs[r['bone']]=([float(r[x]) for x in ['qx','qy','qz','qw']],[float(r[x]) for x in ['x','y','z']])
summary={};violations=[]
for f in frames.values():
    cs,r=f['pose'],f['meta'];stage='fade' if r['returning']=='1' else 'clip'
    for side in ['l','r']:
        flex,swivel=g.hinge(cs,side,'upperarm','lowerarm','hand','elbow')
        shoulder,abduct=g.limb_angles(cs,side,'spine_05','upperarm_'+side,'lowerarm_'+side)
        key=stage+'_'+side
        v=summary.setdefault(key,dict(elbow_min=999,elbow_max=-999,sideways_max=0,shoulder_min=999,shoulder_max=-999,samples=0))
        v['elbow_min']=min(v['elbow_min'],flex);v['elbow_max']=max(v['elbow_max'],flex)
        v['sideways_max']=max(v['sideways_max'],abs(swivel));v['shoulder_min']=min(v['shoulder_min'],shoulder);v['shoulder_max']=max(v['shoulder_max'],shoulder);v['samples']+=1
        if flex < -5 or abs(swivel)>15 or shoulder < -60 or shoulder > 180:
            violations.append(dict(time=r['time'],episode=r['episode'],clip=r['clip'],stage=stage,side=side,elbow=flex,sideways=swivel,shoulder=shoulder,montage_time=r['montage_time']))
out=dict(summary=summary,violations=violations)
Path(sys.argv[1]).with_suffix('.analysis.json').write_text(json.dumps(out,indent=2))
print(json.dumps(summary,indent=2));print('violations',len(violations));print(json.dumps(violations[:12],indent=2))


if not summary: raise SystemExit('No get-up samples found')
raise SystemExit(1 if violations else 0)

