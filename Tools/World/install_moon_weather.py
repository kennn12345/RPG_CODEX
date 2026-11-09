"""Install weather inputs and phased-moon shading without rebuilding the approved sky."""
import unreal,json,shutil
from pathlib import Path
root=Path(unreal.Paths.project_dir());report=root/'Saved/MoonWeather';report.mkdir(parents=True,exist_ok=True)
mel=unreal.MaterialEditingLibrary;eal=unreal.EditorAssetLibrary
mat=unreal.load_asset('/Game/RPG/Environment/World/Materials/M_Vista_Sky')
expressions=mel.get_material_expressions(mat)
node=next(e for e in expressions if e.get_class().get_name()=='MaterialExpressionCustom' and '// stars' in str(e.get_editor_property('code')))
before=str(node.get_editor_property('code'))
if not (report/'custom_before.hlsl').exists():
    (report/'custom_before.hlsl').write_text(before)
    shutil.copy2(root/'Content/RPG/Environment/World/Materials/M_Vista_Sky.uasset',report/'M_Vista_Sky_before.uasset')
marker=before.index('// moon:')
after=before[:marker]+(root/'Tools/World/moon_phases.hlsl').read_text()
node.set_editor_property('code',after)
inputs=list(node.get_editor_property('inputs'))
names=[str(i.get_editor_property('input_name')) for i in inputs]
for name,index in [('Phase',8),('Clouds',9),('Haze',10),('WeatherFog',11),('MoonWarm',12),('Wind',13),('Storm',14),('Flash',15),('VolumeClouds',16)]:
    if name not in names:
        entry=unreal.CustomInput();entry.set_editor_property('input_name',name);inputs.append(entry);names.append(name)
node.set_editor_property('inputs',inputs)
for offset,(name,index) in enumerate([('Phase',8),('Clouds',9),('Haze',10),('WeatherFog',11),('MoonWarm',12),('Wind',13),('Storm',14),('Flash',15),('VolumeClouds',16)]):
    # Scalar CPD inputs have zero defaults, preserving a clear full-moon sky in older editor sessions.
    param_name='Weather_'+name
    expr=next((e for e in expressions if e.get_class().get_name()=='MaterialExpressionScalarParameter' and str(e.get_editor_property('parameter_name'))==param_name),None)
    if expr is None:expr=mel.create_material_expression(mat,unreal.MaterialExpressionScalarParameter,-1900,2200+offset*90)
    expr.set_editor_property('parameter_name',param_name);expr.set_editor_property('default_value',0.0)
    expr.set_editor_property('use_custom_primitive_data',True);expr.set_editor_property('primitive_data_index',index)
    assert mel.connect_material_expressions(expr,'',node,name)
mel.recompile_material(mat);assert eal.save_loaded_asset(mat,only_if_is_dirty=False)
(report/'custom_after.hlsl').write_text(after)

folder='/Game/RPG/Environment/Weather';eal.make_directory(folder)
rain=unreal.load_asset(folder+'/M_Weather_Rain')
if rain is None:
    rain=unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_Weather_Rain',folder,unreal.Material,unreal.MaterialFactoryNew())
for expression in list(mel.get_material_expressions(rain)):mel.delete_material_expression(rain,expression)
rain.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
rain.set_editor_property('blend_mode',unreal.BlendMode.BLEND_ADDITIVE)
rain.set_editor_property('translucency_lighting_mode',unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
rain.set_editor_property('two_sided',True);rain.set_editor_property('used_with_instanced_static_meshes',True)
# Responsive AA aliases the skyline/character behind moving translucent drops.
# After DOF keeps scene depth testing, unlike the After Motion Blur pass.
rain.set_editor_property('enable_responsive_aa',False)
rain.set_editor_property('output_translucent_velocity',False)
rain.set_editor_property('translucency_pass',unreal.MaterialTranslucencyPass.MTP_AFTER_DOF)
color=mel.create_material_expression(rain,unreal.MaterialExpressionConstant3Vector,-500,0)
color.set_editor_property('constant',unreal.LinearColor(.72,.78,.84,1))
mel.connect_material_property(color,'',unreal.MaterialProperty.MP_BASE_COLOR)
uv=mel.create_material_expression(rain,unreal.MaterialExpressionTextureCoordinate,-800,160)
shape=mel.create_material_expression(rain,unreal.MaterialExpressionCustom,-400,160)
shape.set_editor_property('code','float x=(UV.x-.5)*2; return exp(-x*x*18)*smoothstep(0,.12,UV.y)*(1-smoothstep(.6,1,UV.y))*.45;')
ci=unreal.CustomInput();ci.set_editor_property('input_name','UV');shape.set_editor_property('inputs',[ci])
shape.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT1)
mel.connect_material_expressions(uv,'',shape,'UV');mel.connect_material_property(shape,'',unreal.MaterialProperty.MP_OPACITY)
depth=mel.create_material_expression(rain,unreal.MaterialExpressionDepthFade,-100,220);depth.set_editor_property('fade_distance_default',75.0)
mel.connect_material_expressions(shape,'',depth,'Opacity');mel.connect_material_property(depth,'',unreal.MaterialProperty.MP_OPACITY)
lighting=unreal.load_asset(folder+'/MPC_WeatherLighting')
if lighting is None:
    lighting=unreal.AssetToolsHelpers.get_asset_tools().create_asset('MPC_WeatherLighting',folder,unreal.MaterialParameterCollection,unreal.MaterialParameterCollectionFactoryNew())
    parameters=[]
    for name in ['DirectSunlight','WaterLight','RainLight']:
        entry=unreal.CollectionScalarParameter();entry.set_editor_property('parameter_name',name);entry.set_editor_property('default_value',1.0);parameters.append(entry)
    lighting.set_editor_property('scalar_parameters',parameters);assert eal.save_loaded_asset(lighting,only_if_is_dirty=False)
parameters=list(lighting.get_editor_property('scalar_parameters'))
if 'RainLight' not in [str(p.get_editor_property('parameter_name')) for p in parameters]:
    entry=unreal.CollectionScalarParameter();entry.set_editor_property('parameter_name','RainLight');entry.set_editor_property('default_value',1.0)
    parameters.append(entry);lighting.set_editor_property('scalar_parameters',parameters)
    assert eal.save_loaded_asset(lighting,only_if_is_dirty=False)
light=mel.create_material_expression(rain,unreal.MaterialExpressionCollectionParameter,-700,-160)
light.set_editor_property('collection',lighting);light.set_editor_property('parameter_name','RainLight')
brightness=mel.create_material_expression(rain,unreal.MaterialExpressionMultiply,-400,-160);brightness.set_editor_property('const_b',1.1)
mel.connect_material_expressions(light,'',brightness,'A')
highlight=mel.create_material_expression(rain,unreal.MaterialExpressionMultiply,-100,-160)
mel.connect_material_expressions(color,'',highlight,'A');mel.connect_material_expressions(brightness,'',highlight,'B')
mel.connect_material_property(highlight,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
mel.recompile_material(rain);assert eal.save_loaded_asset(rain,only_if_is_dirty=False)
(report/'installed.json').write_text(json.dumps(dict(saved=True,sky_prefix_unchanged=True,weather_cpd=list(range(8,17)),rain_material=True),indent=2))
unreal.log('MOON_WEATHER_MATERIALS_INSTALLED')

# Preserve the current rain realism material when reinstalling moon/sky inputs.
import runpy
runpy.run_path(str(root/'Tools/World/install_rain_realism.py'),run_name='__main__')
