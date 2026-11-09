"""Install depth-layered rain, lit ripples, small ballistic splashes and reversible wetness links."""
import unreal,shutil,json
from pathlib import Path
root=Path(unreal.Paths.project_dir());report=root/'Saved/RainRealism';report.mkdir(parents=True,exist_ok=True)
mel=unreal.MaterialEditingLibrary;eal=unreal.EditorAssetLibrary;folder='/Game/RPG/Environment/Weather'
mpc=unreal.load_asset(folder+'/MPC_WeatherLighting')
params=list(mpc.get_editor_property('scalar_parameters'))
for name in ['RainIntensity','Wetness']:
 if name not in [str(p.get_editor_property('parameter_name')) for p in params]:
  p=unreal.CollectionScalarParameter();p.set_editor_property('parameter_name',name);p.set_editor_property('default_value',0);params.append(p)
mpc.set_editor_property('scalar_parameters',params);assert eal.save_loaded_asset(mpc,only_if_is_dirty=False)
def node(m,cls,**props):
 e=mel.create_material_expression(m,cls)
 for k,v in props.items():e.set_editor_property(k,v)
 return e
def link(a,b,pin):assert mel.connect_material_expressions(a,'',b,pin)
def output(a,prop):assert mel.connect_material_property(a,'',prop)
def collection(m,name):return node(m,unreal.MaterialExpressionCollectionParameter,collection=mpc,parameter_name=name)
def custom(m,code,inputs,kind=unreal.CustomMaterialOutputType.CMOT_FLOAT1,desc=''):
 ins=[]
 for name in inputs:
  i=unreal.CustomInput();i.set_editor_property('input_name',name);ins.append(i)
 e=node(m,unreal.MaterialExpressionCustom,code=code,inputs=ins,output_type=kind,desc=desc)
 for name,source in inputs.items():link(source,e,name)
 return e
def make(name,lit=False):
 m=unreal.load_asset(folder+'/'+name)
 if m:
  disk=root/('Content/RPG/Environment/Weather/'+name+'.uasset');backup=report/(name+'_before.uasset')
  if disk.exists() and not backup.exists():shutil.copy2(disk,backup)
 else:m=unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,folder,unreal.Material,unreal.MaterialFactoryNew())
 for e in list(mel.get_material_expressions(m)):mel.delete_material_expression(m,e)
 m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_DEFAULT_LIT if lit else unreal.MaterialShadingModel.MSM_UNLIT)
 m.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT if lit else unreal.BlendMode.BLEND_ADDITIVE)
 m.set_editor_property('two_sided',True);m.set_editor_property('used_with_instanced_static_meshes',True)
 m.set_editor_property('enable_responsive_aa',False);m.set_editor_property('output_translucent_velocity',False)
 m.set_editor_property('translucency_pass',unreal.MaterialTranslucencyPass.MTP_AFTER_DOF)
 m.set_editor_property('translucency_lighting_mode',unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
 return m
def value(m,v):return node(m,unreal.MaterialExpressionConstant,r=v)
def color(m,r,g,b):return node(m,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(r,g,b,1))
def data(m,index,default=0):return node(m,unreal.MaterialExpressionPerInstanceCustomData,data_index=index,const_default_value=default)
def save(m):mel.recompile_material(m);assert eal.save_loaded_asset(m,only_if_is_dirty=False)
m=make('M_Weather_Rain');uv=node(m,unreal.MaterialExpressionTextureCoordinate)
shape=custom(m,'''float x=(UV.x-.5)*2;
float streakMask=exp(-x*x*18)*smoothstep(0,.07,UV.y)*(1-smoothstep(.55,1,UV.y));
float glint=.65+Sunlight*3*pow(saturate(dot(normalize(Sun),-normalize(View))),18);
float strength=.2+.8*pow(Variation,3);
return streakMask*strength*glint*(1-smoothstep(2500,6500,Depth))*.9;''',dict(UV=uv,Variation=data(m,0,.5),Depth=node(m,unreal.MaterialExpressionPixelDepth),Sun=node(m,unreal.MaterialExpressionSkyAtmosphereLightDirection),View=node(m,unreal.MaterialExpressionCameraVectorWS),Sunlight=collection(m,'DirectSunlight')))
depth=node(m,unreal.MaterialExpressionDepthFade,fade_distance_default=25.0);link(shape,depth,'Opacity');output(depth,unreal.MaterialProperty.MP_OPACITY)
emission=custom(m,'return float3(.65,.7,.75)*Light*2.25;',dict(Light=collection(m,'RainLight')),unreal.CustomMaterialOutputType.CMOT_FLOAT3)
output(emission,unreal.MaterialProperty.MP_EMISSIVE_COLOR);save(m)
m=make('M_Rain_Splash');uv=node(m,unreal.MaterialExpressionTextureCoordinate);age=data(m,0);fade=data(m,1,1)
shape=custom(m,'''float2 p=float2(UV.x-.5,.5-UV.y);float mask=0;
for(int i=0;i<5;i++){float vx=(i-2)*.31;float vy=1.45+frac(i*.371)*.6;
float2 q=float2(vx*Age,-.35+vy*Age-2.1*Age*Age);
mask+=exp(-dot(p-q,p-q)*2200);}
return saturate(mask)*smoothstep(0,.07,Age)*(1-smoothstep(.65,1,Age))*Fade*.32;''',dict(UV=uv,Age=age,Fade=fade))
output(shape,unreal.MaterialProperty.MP_OPACITY)
emission=custom(m,'return float3(.4,.45,.5)*Light;',dict(Light=collection(m,'RainLight')),unreal.CustomMaterialOutputType.CMOT_FLOAT3);output(emission,unreal.MaterialProperty.MP_EMISSIVE_COLOR);save(m)
m=make('M_Rain_Ripple',True);uv=node(m,unreal.MaterialExpressionTextureCoordinate);age=data(m,0);fade=data(m,1,1)
shape=custom(m,'float r=length((UV-.5)*2);return exp(-pow((r-(.08+Age*.8))*35,2))*(1-Age)*Fade*.22;',dict(UV=uv,Age=age,Fade=fade));output(shape,unreal.MaterialProperty.MP_OPACITY)
normal=custom(m,'''float2 p=(UV-.5)*2;float r=length(p);float d=r-(.08+Age*.8);
float wave=sin(d*85)*exp(-d*d*300)*(1-Age);
return normalize(float3(p/max(r,.001)*wave*.35,1));''',dict(UV=uv,Age=age),unreal.CustomMaterialOutputType.CMOT_FLOAT3)
output(normal,unreal.MaterialProperty.MP_NORMAL);output(color(m,.025,.03,.035),unreal.MaterialProperty.MP_BASE_COLOR)
output(value(m,.12),unreal.MaterialProperty.MP_ROUGHNESS);output(value(m,.65),unreal.MaterialProperty.MP_SPECULAR);save(m)
# Damp existing ground/stone progressively, preserving dry inputs and art direction.
for name in ['M_Vista_Terrain','M_Vista_Rock']:
 m=unreal.load_asset('/Game/RPG/Environment/World/Materials/'+name)
 if not m:continue
 disk=root/('Content/RPG/Environment/World/Materials/'+name+'.uasset');backup=report/(name+'_before.uasset')
 if not backup.exists():shutil.copy2(disk,backup)
 if any(str(e.get_editor_property('desc'))=='RainRealismWetness' for e in mel.get_material_expressions(m)):continue
 wet=collection(m,'Wetness');normal=node(m,unreal.MaterialExpressionPixelNormalWS)
 mask=custom(m,'return saturate(Wetness)*smoothstep(.2,.85,Normal.z);',dict(Wetness=wet,Normal=normal),desc='RainRealismWetness')
 for prop in [unreal.MaterialProperty.MP_BASE_COLOR,unreal.MaterialProperty.MP_EMISSIVE_COLOR]:
  old=mel.get_material_property_input_node(m,prop)
  if old:
   pin=mel.get_material_property_input_node_output_name(m,prop)
   damp=custom(m,'return Dry*lerp(1,.76,Wet);',dict(Wet=mask),unreal.CustomMaterialOutputType.CMOT_FLOAT3)
   ins=list(damp.get_editor_property('inputs'));entry=unreal.CustomInput();entry.set_editor_property('input_name','Dry');ins.append(entry);damp.set_editor_property('inputs',ins)
   assert mel.connect_material_expressions(old,pin,damp,'Dry');output(damp,prop)
 old=mel.get_material_property_input_node(m,unreal.MaterialProperty.MP_ROUGHNESS)
 pin=mel.get_material_property_input_node_output_name(m,unreal.MaterialProperty.MP_ROUGHNESS)
 damp=custom(m,'return lerp(Dry,.38,Wet*.8);',dict(Wet=mask))
 ins=list(damp.get_editor_property('inputs'));entry=unreal.CustomInput();entry.set_editor_property('input_name','Dry');ins.append(entry);damp.set_editor_property('inputs',ins)
 if old:assert mel.connect_material_expressions(old,pin,damp,'Dry')
 else:link(value(m,.9),damp,'Dry')
 output(damp,unreal.MaterialProperty.MP_ROUGHNESS);save(m)
(report/'installed.json').write_text(json.dumps(dict(rain=True,splashes=True,lit_water_ripples=True,wetness=True),indent=2))
unreal.log('RAIN_REALISM_MATERIALS_SAVED')



