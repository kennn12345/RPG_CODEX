"""Restrained region albedo and seamless ground detail for the world blockout.
Python + numpy/scipy/Pillow only. Does not change height data.
"""
from pathlib import Path
import os,sys,json,hashlib
if os.environ.get('RPG_TERRAIN_PYTHON_LIB'):
    sys.path.insert(0,os.environ['RPG_TERRAIN_PYTHON_LIB'])
import numpy as np
from scipy import ndimage as ndi
from PIL import Image,ImageDraw

ROOT=Path(__file__).resolve().parent
OUT=ROOT/'WorldMapLookSource';OUT.mkdir(exist_ok=True)
SOURCE=ROOT/'WorldMapSourceV2'
W,H=2048,1317
yy,xx=np.mgrid[:H,:W]
x=xx/(W-1)*1617;y=yy/(H-1)*973
def polygon(points,feather=12):
    mask=Image.new('L',(W,H))
    ImageDraw.Draw(mask).polygon([(a/1617*(W-1),b/973*(H-1)) for a,b in points],fill=255)
    return ndi.gaussian_filter(np.array(mask,dtype=float)/255,feather)
def blend(rgb,mask):
    global color
    color=color*(1-mask[:,:,None])+np.array(rgb)[None,None,:]*mask[:,:,None]

palette=dict(forest=[77,108,51],foothills=[104,126,71],plains=[158,147,87],marsh=[76,88,59],
    warm_woodlands=[119,136,66],snow=[204,218,223],mountain_stone=[111,116,116],
    tundra=[141,144,124],tropical=[96,125,59],volcanic_stone=[79,73,66],ice=[164,204,218])
color=np.zeros((H,W,3),dtype=float)+palette['forest']
warm=polygon([(318,654),(430,628),(551,599),(640,539),(774,553),(847,544),(975,551),(962,647),(823,679),(730,704),(760,785),(710,830),(650,790),(582,720),(543,701),(440,750),(350,720),(314,710)],18)
blend(palette['warm_woodlands'],warm)
plains=polygon([(0,325),(143,299),(221,314),(316,337),(351,382),(310,420),(263,452),(199,492),(90,520),(0,500)],20)
blend(palette['plains'],plains)
marsh=polygon([(35,518),(151,513),(224,540),(249,582),(300,633),(325,675),(281,723),(226,739),(174,756),(95,774),(28,750),(25,670),(43,610)],17)
blend(palette['marsh'],marsh)
ridge=np.array([(116,317),(202,279),(291,263),(384,243),(476,212),(552,185),(636,163),(714,157),(778,140)])
ridge_y=np.interp(x,ridge[:,0],ridge[:,1])
foothill=np.exp(-((y-ridge_y-43)/39)**2)*((x>75)&(x<830))
blend(palette['foothills'],ndi.gaussian_filter(foothill,8)*0.85)
snow_boundary=np.interp(x,[0,120,300,480,650,780,900,1050,1160,1617],[345,280,230,180,125,155,235,280,200,160])
snow=1/(1+np.exp(np.clip((y-snow_boundary)/9,-50,50)))
snow*=1/(1+np.exp(np.clip((x-1145)/8,-50,50)))
blend(palette['snow'],snow)
mountain=np.exp(-((y-ridge_y)/23)**4)*((x>95)&(x<800))
mountain=ndi.gaussian_filter(mountain,7)
blend(palette['mountain_stone'],mountain*0.94)
frost=polygon([(1087,278),(1110,225),(1195,156),(1280,80),(1350,0),(1617,0),(1617,412),(1285,410),(1175,345)],10)
blend(palette['snow'],frost)
tundra=polygon([(1097,270),(1160,242),(1230,255),(1315,270),(1390,268),(1490,215),(1500,245),(1430,297),(1378,333),(1330,364),(1285,396),(1230,370),(1175,341),(1105,315)],12)
blend(palette['tundra'],tundra)
ice=np.exp(-((x-(1472-(y-137)*0.46))/24)**4)*np.exp(-((y-182)/63)**4)*frost
blend(palette['ice'],ice*0.7)
tropic=polygon([(750,820),(790,758),(868,722),(972,618),(1020,527),(1120,510),(1230,598),(1345,716),(1360,973),(750,973)],12)
blend(palette['tropical'],tropic)
volcano=np.exp(-(((x-1108)/141)**2+((y-691)/133)**2)**2)*tropic
blend(palette['volcanic_stone'],volcano*0.96)
# Warm southern islands, without painting ocean text or copying the illustration.
south=polygon([(0,780),(740,780),(760,973),(0,973)],12)
blend(palette['warm_woodlands'],south*0.8)

raw=np.fromfile(SOURCE/'heightmap.r16',dtype='<u2').reshape(2521,4033)
height=(raw.astype('float32')-32768)/128
z=np.array(Image.fromarray(height).resize((W,H),Image.Resampling.BILINEAR))
# Small concavity darkening follows actual terrain, rather than invented relief.
local=ndi.gaussian_filter(z,8)
ao=np.clip(1+(z-local)*0.007,0.82,1.055)
rng=np.random.default_rng(4198)
macro=ndi.gaussian_filter(rng.standard_normal((H,W)),13)
macro=macro/max(float(macro.std()),0.0001)
macro=np.clip(macro,-2,2)*0.023+1
color*=ao[:,:,None]*macro[:,:,None]
water=np.array(Image.open(SOURCE/'waterway_mask.png').resize((W,H),Image.Resampling.NEAREST))>127
wet_distance=ndi.distance_transform_edt(~water,sampling=(9000/(H-1),14000/(W-1)))
wet=(1-np.clip(wet_distance/20,0,1))*0.13
color*=1-wet[:,:,None]
beach=(1-np.clip((z-0.5)/3.8,0,1))*0.62
beach[z<0]=0.25
beach_rgb=np.zeros_like(color)+[169,156,123]
cold=np.maximum(snow,frost)
beach_rgb=beach_rgb*(1-cold[:,:,None])+np.array([134,147,146])*cold[:,:,None]
color=color*(1-beach[:,:,None])+beach_rgb*beach[:,:,None]
Image.fromarray(np.clip(color,0,255).astype('uint8')).save(OUT/'T_WorldMap_RegionColor.png')

# Seamless low-contrast ground grain. Eight-metre tile; no busy grass blades.
N=512
noise=rng.standard_normal((N,N))
detail=ndi.gaussian_filter(noise,2,mode='wrap')+ndi.gaussian_filter(noise,11,mode='wrap')*1.8
detail=detail/max(float(detail.std()),0.0001)
gray=np.clip(128+detail*10,96,160).astype('uint8')
Image.fromarray(np.repeat(gray[:,:,None],3,axis=2)).save(OUT/'T_WorldMap_GroundGrain.png')
dy,dx=np.gradient(ndi.gaussian_filter(detail,1,mode='wrap'))
nx=-dx*0.45;ny=-dy*0.45;nz=np.ones_like(nx)
norm=np.sqrt(nx*nx+ny*ny+nz*nz)
normal=np.stack([nx/norm,ny/norm,nz/norm],axis=2)
Image.fromarray(np.clip((normal*0.5+0.5)*255,0,255).astype('uint8')).save(OUT/'T_WorldMap_GroundNormal.png')
manifest=dict(palette_srgb=palette,region_color_resolution=[W,H],detail_tile_m=8,
    height_source_sha256=hashlib.sha256((SOURCE/'heightmap.r16').read_bytes()).hexdigest(),
    scope='Material look only: region colours, restrained grain/normal, real-slope rock blend. No elevation changes.')
(OUT/'land_look.json').write_text(json.dumps(manifest,indent=2))
print('WORLD_MAP_REGION_TEXTURES_READY',manifest)
