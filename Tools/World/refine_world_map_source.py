"""Reference-guided shoreline refinement and graded river-channel heightmap.
Run with Python + numpy, scipy, Pillow; no 3D application is used.
Dependencies can be supplied with RPG_TERRAIN_PYTHON_LIB.
"""
from pathlib import Path
import sys, os, json, hashlib
if os.environ.get('RPG_TERRAIN_PYTHON_LIB'):
    sys.path.insert(0, os.environ['RPG_TERRAIN_PYTHON_LIB'])
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage as ndi
from scipy.interpolate import CubicSpline

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT/'WorldMapSource'
OUT = ROOT/'WorldMapSourceV2'
OUT.mkdir(exist_ok=True)
REFERENCE = Path(os.environ['RPG_WORLD_MAP_REFERENCE'])
im = np.array(Image.open(REFERENCE).convert('RGB'))
RH,RW = im.shape[:2]
W,H = 4033,2521
SX,SY = 14000/(W-1),9000/(H-1)
spec = json.loads((SOURCE/'editable_coastlines.json').read_text())
def pixel_poly(poly):
    return [(x/14000*(RW-1),y/9000*(RH-1)) for x,y in poly]
prior = Image.new('L',(RW,RH))
for poly in spec['land_polygons'].values():
    ImageDraw.Draw(prior).polygon(pixel_poly(poly), fill=255)
prior = np.asarray(prior)>0
r,g,b = (im[:,:,i].astype(float) for i in range(3))
blue = (g>105)&(b>105)&(r<g*0.86)&(b>g*0.87)
din = ndi.distance_transform_edt(prior)
dout = ndi.distance_transform_edt(~prior)
# Only the strip near the planned coastline is inferred from illustration colour.
# Protect interior snow/ice, labels, the legend, and continental continuation.
coast_band = (din<=26)&(dout<=26)
land = np.where(coast_band, ~blue, prior)
land = ndi.binary_closing(land,iterations=2,border_value=1)
land = ndi.binary_opening(land,iterations=1)
land[0,:] = prior[0,:]
land[:,0] = prior[:,0]
land = ndi.binary_fill_holes(land)
labels,n = ndi.label(land)
sizes = np.bincount(labels.ravel())
supported = np.bincount(labels.ravel(), weights=prior.ravel().astype(float))
# Dark sea-label lettering must not turn into tiny islands. Keep only land
# components supported by the original planned land polygons.
land &= (sizes[labels]>=12)&(supported[labels]>=2)
# The lake's cyan shape is extracted separately; fill lettering and tree holes.
yy,xx=np.mgrid[:RH,:RW]
lake_area=(xx>320)&(xx<535)&(yy>345)&(yy<459)
lake = ndi.binary_opening(blue&lake_area,iterations=2)
ll,nn=ndi.label(lake)
largest = np.argmax(np.bincount(ll.ravel())[1:])+1
lake=ndi.binary_fill_holes(ll==largest)
lake=ndi.binary_closing(lake,iterations=2)
def upscale(mask):
    a=np.array(Image.fromarray(mask.astype('uint8')*255).resize((W,H),Image.Resampling.BILINEAR),dtype=float)/255
    return ndi.gaussian_filter(a,0.75)>0.5
land=upscale(land)
lake=upscale(lake)
di=ndi.distance_transform_edt(land,sampling=(SY,SX))
do=ndi.distance_transform_edt(~land,sampling=(SY,SX))
height=np.where(land,60*(1-np.exp(-di/220)),-60*(1-np.exp(-do/120))).astype('float32')
# Lake and rivers have real elevations; they do not cut 60 m sea-level trenches.
LAKE_Z=22.0
ld=ndi.distance_transform_edt(lake,sampling=(SY,SX))
lo=ndi.distance_transform_edt(~lake,sampling=(SY,SX))
height[lake]=LAKE_Z-2.5-9*(1-np.exp(-ld[lake]/100))
shore=(lo<190)&~lake
t=np.clip(lo[shore]/190,0,1); blend=t*t*(3-2*t)
height[shore]=(LAKE_Z+0.5+lo[shore]*0.045)*(1-blend)+height[shore]*blend

# Centerlines digitized from the illustrated reference in its original pixels.
# Shared junctions share an exact water height. Direction is downstream.
routes=[
 dict(name='Highland_Lake_Feeder',width=25,start=49,end=22,points=[(377,289),(364,305),(357,322),(358,338),(371,348),(382,359),(383,373)]),
 dict(name='Wheatford_Stream',width=30,start=36,end=8,points=[(358,338),(350,355),(333,367),(325,387),(316,405),(296,420),(281,439),(269,457),(249,473),(242,489),(231,503),(232,520),(215,529),(202,539),(196,555)]),
 dict(name='Mirewatch_River',width=52,start=8,end=0,points=[(196,555),(216,565),(219,579),(241,586),(259,588),(268,599),(261,611),(265,620),(277,627),(280,638),(277,648),(285,657),(305,666),(319,675),(325,686),(336,691),(347,705),(347,725)]),
 dict(name='Lake_Wooded_Valley_Outlet',width=38,start=22,end=4.0,points=[(423,442),(415,461),(396,474),(381,486),(373,505),(365,521),(348,533),(339,547),(340,559),(350,570),(348,583),(332,594),(323,603),(323,615),(301,622),(277,627)]),
 dict(name='Coastal_Woodland_River',width=64,start=28,end=0,points=[(657,547),(638,554),(625,568),(617,583),(613,594),(601,603),(583,606),(567,611),(548,617),(529,615),(510,617),(491,626),(476,630),(465,642),(460,651),(442,657),(430,668),(426,679),(412,685),(391,684),(372,683),(359,685),(347,696),(347,705),(347,725)]),
 dict(name='West_Marsh_Inlet',width=25,start=15,end=8,points=[(107,518),(122,526),(133,541),(151,546),(173,555),(196,555)]),
 dict(name='Marsh_West_Braid',width=30,start=6,end=1.4,points=[(265,620),(243,630),(219,638),(199,650),(184,667),(209,675),(236,681),(261,676),(282,682),(303,690),(325,686)]),
 dict(name='Marsh_North_Channel',width=19,start=12,end=8,points=[(86,555),(111,551),(132,550),(155,555),(176,557),(196,555)]),
 dict(name='Marsh_South_Channel',width=22,start=6,end=2.8,points=[(155,594),(178,600),(193,617),(219,625),(243,630)]),
]
# Heights at junctions are assigned by trunk water profiles, not guessed twice.
# Use a monotone profile and correct junction station values after densification.
def smooth_route(route):
    pts=np.array(route['points'],dtype=float)
    pts[:,0]*=14000/(RW-1);pts[:,1]*=9000/(RH-1)
    d=np.r_[0,np.cumsum(np.linalg.norm(np.diff(pts,axis=0),axis=1))]
    tt=np.linspace(0,d[-1],max(3,int(d[-1]/8)+1))
    xy=np.column_stack([CubicSpline(d,pts[:,k],bc_type='natural')(tt) for k in range(2)])
    z=np.linspace(route['start'],route['end'],len(tt))
    return xy,z,tt

dense={r['name']:smooth_route(r) for r in routes}
def station(name,p):
    xy,z,t=dense[name]
    m=np.array([p[0]*14000/(RW-1),p[1]*9000/(RH-1)])
    return float(z[np.argmin(np.linalg.norm(xy-m,axis=1))])
for r in routes:
    if r['name']=='Wheatford_Stream':r['start']=station('Highland_Lake_Feeder',(358,338))
    if r['name']=='Lake_Wooded_Valley_Outlet':r['end']=station('Mirewatch_River',(277,627))
    if r['name']=='Marsh_West_Braid':
        r['start']=station('Mirewatch_River',(265,620));r['end']=station('Mirewatch_River',(325,686))
    if r['name']=='Marsh_South_Channel':r['end']=station('Marsh_West_Braid',(243,630))
    dense[r['name']]=smooth_route(r)

network=[]
watermask=lake.copy()
river_checks=[]
for route in routes:
    xy,z,dist=dense[route['name']]
    px=np.clip(np.rint(xy[:,0]/SX).astype(int),0,W-1)
    py=np.clip(np.rint(xy[:,1]/SY).astype(int),0,H-1)
    # Nearest centerline station gives a continuous channel elevation.
    seed=np.ones((H,W),dtype=bool);seed[py,px]=False
    zseed=np.zeros((H,W),dtype='float32');zseed[py,px]=z
    dd,indices=ndi.distance_transform_edt(seed,sampling=(SY,SX),return_indices=True)
    zz=zseed[indices[0],indices[1]]
    half=route['width']/2
    # Broad valley sides transition into the terrain over 100-180 m.
    bank=half+max(100,route['width']*2.6)
    active=dd<bank
    bed=zz-3.5+3.2*np.clip(dd/half,0,1)**2
    outer=np.clip((dd-half)/(bank-half),0,1)
    target=np.where(dd<=half,bed,zz+outer*outer*18)
    blend=outer*outer*(3-2*outer)
    desired=target*(1-blend)+height*blend
    height[active]=np.minimum(height[active],desired[active])
    watermask |= dd<half
    # Water strips go slightly into the banks, leaving no visible dry seam.
    sel=list(range(0,len(xy),6))
    if sel[-1]!=len(xy)-1:sel.append(len(xy)-1)
    pts=[[round(float(xy[i,0]*100-700000),3),round(float(xy[i,1]*100-450000),3),round(float(z[i]*100),3)] for i in sel]
    network.append(dict(name=route['name'],width_cm=(route['width']+5)*100,points_cm=pts,length_m=float(dist[-1]),start_water_m=route['start'],end_water_m=route['end']))
    for i in np.linspace(0,len(px)-1,9).astype(int):
        river_checks.append(dict(river=route['name'],vertex=[int(px[i]),int(py[i])],water_z_cm=round(float(z[i]*100),3)))
    del dd,indices,zz

# A gentle final filter removes sample-grid terraces without blurring the channels.
height=ndi.gaussian_filter(height,0.45)
raw=np.rint(32768+height*128).clip(0,65535).astype('<u2')
raw.tofile(OUT/'heightmap.r16')
Image.fromarray(raw).save(OUT/'heightmap_4033x2521_16bit.png')
Image.fromarray(land.astype('uint8')*255).save(OUT/'land_mask.png')
Image.fromarray(lake.astype('uint8')*255).save(OUT/'lake_mask.png')
Image.fromarray(watermask.astype('uint8')*255).save(OUT/'waterway_mask.png')
ys,xs=np.where(lake)
lake_bounds=[float(xs.min()*SX*100-700000),float(ys.min()*SY*100-450000),float(xs.max()*SX*100-700000),float(ys.max()*SY*100-450000)]
meta=dict(version=2,map='/Game/RPG/Maps/L_WorldMap_Refined',extent_m=[14000,9000],resolution=[W,H],lake_level_m=LAKE_Z,lake_bounds_cm=lake_bounds,
    shoreline_method='Reference ocean-colour refinement within 26 source pixels of the v1 planned coast; protected interior and continental continuation.',
    reference_sha256=hashlib.sha256(REFERENCE.read_bytes()).hexdigest(),heightmap_sha256=hashlib.sha256(raw.tobytes()).hexdigest(),rivers=network,river_checks=river_checks)
(OUT/'refinement.json').write_text(json.dumps(meta,indent=2))
print('REFINED_SOURCE_READY',len(network),'waterways',round(sum(r['length_m'] for r in network)), 'metres')
