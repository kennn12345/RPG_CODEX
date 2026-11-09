"""Refine existing sky/rain/water in the live editor. No map rebuild or editor quit."""
import unreal,json,shutil,re
from pathlib import Path
root=Path(unreal.Paths.project_dir());report=root/'Saved/WeatherRefinement';report.mkdir(parents=True,exist_ok=True)
mel=unreal.MaterialEditingLibrary;eal=unreal.EditorAssetLibrary
paths=['World/Materials/M_Vista_Sky','World/Materials/M_Vista_Mist','Weather/M_Weather_Rain','Water/M_StylizedWater','Water/M_WaterCaustics','Water/M_UnderwaterPP']
for name in paths:
    target=report/(name.split('/')[-1]+'_before.uasset')
    source=root/('Content/RPG/Environment/'+name+'.uasset')
    if not target.exists():shutil.copy2(source,target)
collection_path='/Game/RPG/Environment/Weather/MPC_WeatherLighting'
mpc=unreal.load_asset(collection_path)
if mpc is None:
    mpc=unreal.AssetToolsHelpers.get_asset_tools().create_asset('MPC_WeatherLighting','/Game/RPG/Environment/Weather',unreal.MaterialParameterCollection,unreal.MaterialParameterCollectionFactoryNew())
    entry=unreal.CollectionScalarParameter();entry.set_editor_property('parameter_name','DirectSunlight');entry.set_editor_property('default_value',1.0)
    mpc.set_editor_property('scalar_parameters',[entry])
assert eal.save_loaded_asset(mpc,only_if_is_dirty=False)
parameters=list(mpc.get_editor_property('scalar_parameters'))
if 'RainLight' not in [str(p.get_editor_property('parameter_name')) for p in parameters]:
    p=unreal.CollectionScalarParameter();p.set_editor_property('parameter_name','RainLight');p.set_editor_property('default_value',1.0)
    parameters.append(p);mpc.set_editor_property('scalar_parameters',parameters)
    assert eal.save_loaded_asset(mpc,only_if_is_dirty=False)
if 'WaterLight' not in [str(p.get_editor_property('parameter_name')) for p in parameters]:
    p=unreal.CollectionScalarParameter();p.set_editor_property('parameter_name','WaterLight');p.set_editor_property('default_value',1.0)
    parameters.append(p);mpc.set_editor_property('scalar_parameters',parameters)
    assert eal.save_loaded_asset(mpc,only_if_is_dirty=False)

def sunlight_input(mat,node):
    inputs=list(node.get_editor_property('inputs'))
    if 'SunLight' not in [str(i.get_editor_property('input_name')) for i in inputs]:
        entry=unreal.CustomInput();entry.set_editor_property('input_name','SunLight');inputs.append(entry);node.set_editor_property('inputs',inputs)
    expression=next((e for e in mel.get_material_expressions(mat) if e.get_class().get_name()=='MaterialExpressionCollectionParameter' and str(e.get_editor_property('parameter_name'))=='DirectSunlight'),None)
    if expression is None:expression=mel.create_material_expression(mat,unreal.MaterialExpressionCollectionParameter,-1100,800)
    expression.set_editor_property('collection',mpc);expression.set_editor_property('parameter_name','DirectSunlight')
    assert mel.connect_material_expressions(expression,'',node,'SunLight')

for name in ['M_StylizedWater','M_WaterCaustics','M_UnderwaterPP']:
    mat=unreal.load_asset('/Game/RPG/Environment/Water/'+name)
    for i,node in enumerate(mel.get_material_expressions(mat)):
        if node.get_class().get_name()!='MaterialExpressionCustom':continue
        code=str(node.get_editor_property('code'));old=code
        if name=='M_StylizedWater' and 'Emiss = white' in code:
            code=re.sub(r'Emiss = white \* float3\([^;]+;', 'Emiss = float3(0,0,0); // Water and foam receive actual scene lighting.',code)
        elif name=='M_WaterCaustics' and 'float lines =' in code:
            if '* SunLight' not in code:code=code.replace(' * Strength);',' * Strength) * SunLight;')
            sunlight_input(mat,node)
        elif name=='M_UnderwaterPP' and 'float f = 1.0 - exp' in code:
            if 'litFogColor' not in code:
                code=code.replace('float f =','float3 litFogColor = FogColor * (.025 + .975 * SunLight);\nfloat f =',1)
                code=code.replace('Col * Tint, FogColor,','Col * Tint, litFogColor,').replace('c + FogColor * 2.2 * r','c + litFogColor * 2.2 * r * SunLight')
            sunlight_input(mat,node)
        if code!=old:node.set_editor_property('code',code)
        (report/(name+'_after_'+str(i)+'.hlsl')).write_text(code)
    mel.recompile_material(mat);assert eal.save_loaded_asset(mat,only_if_is_dirty=False)
    if name=='M_StylizedWater':
        scalar=next((e for e in mel.get_material_expressions(mat) if e.get_class().get_name()=='MaterialExpressionCollectionParameter' and str(e.get_editor_property('parameter_name'))=='WaterLight'),None)
        if scalar is None:
            scalar=mel.create_material_expression(mat,unreal.MaterialExpressionCollectionParameter,-700,1000)
            scalar.set_editor_property('collection',mpc);scalar.set_editor_property('parameter_name','WaterLight')
            factor=mel.create_material_expression(mat,unreal.MaterialExpressionMultiply,-400,1000);factor.set_editor_property('const_b',.6)
            mel.connect_material_expressions(scalar,'',factor,'A');mel.connect_material_property(factor,'',unreal.MaterialProperty.MP_SPECULAR)
        mel.recompile_material(mat);assert eal.save_loaded_asset(mat,only_if_is_dirty=False)
        # Restore the original volume output; the remaining lake glow came from mist sheets.
        for expression in list(mel.get_material_expressions(mat)):
            if expression.get_class().get_name()=='MaterialExpressionSingleLayerWaterMaterialOutput':mel.delete_material_expression(mat,expression)
        mel.create_material_expression(mat,unreal.MaterialExpressionSingleLayerWaterMaterialOutput,400,200)
        mel.recompile_material(mat);assert eal.save_loaded_asset(mat,only_if_is_dirty=False)
installer=root/'Tools/World/install_moon_weather.py'
exec(compile(installer.read_text(),str(installer),'exec'),{'__file__':str(installer),'__name__':'__main__'})
sky=unreal.load_asset('/Game/RPG/Environment/World/Materials/M_Vista_Sky')
source=next(e for e in mel.get_material_expressions(sky) if e.get_class().get_name()=='MaterialExpressionCustom' and '// stars' in str(e.get_editor_property('code')))
switch=next((e for e in mel.get_material_expressions(sky) if e.get_class().get_name()=='MaterialExpressionReflectionCapturePassSwitch'),None)
mel.connect_material_property(source,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
if switch is not None:mel.delete_material_expression(sky,switch)
mel.recompile_material(sky);assert eal.save_loaded_asset(sky,only_if_is_dirty=False)
mist=unreal.load_asset('/Game/RPG/Environment/World/Materials/M_Vista_Mist')
source=max((e for e in mel.get_material_expressions(mist) if e.get_class().get_name()=='MaterialExpressionCustom'),key=lambda e:sum(x is not None for x in mel.get_inputs_for_material_expression(mist,e)))
lit=next((e for e in mel.get_material_expressions(mist) if str(e.get_editor_property('desc'))=='Weather-lit lake mist'),None)
if lit is None:
    light=mel.create_material_expression(mist,unreal.MaterialExpressionCollectionParameter,0,400)
    light.set_editor_property('collection',mpc);light.set_editor_property('parameter_name','WaterLight')
    lit=mel.create_material_expression(mist,unreal.MaterialExpressionMultiply,300,0);lit.set_editor_property('desc','Weather-lit lake mist')
    mel.connect_material_expressions(source,'',lit,'A');mel.connect_material_expressions(light,'',lit,'B')
mel.connect_material_property(lit,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
assert mel.connect_material_expressions(source,'',lit,'A')
light=next(e for e in mel.get_material_expressions(mist) if e.get_class().get_name()=='MaterialExpressionCollectionParameter' and str(e.get_editor_property('parameter_name'))=='WaterLight')
opacity=next((e for e in mel.get_material_expressions(mist) if str(e.get_editor_property('desc'))=='Weather-lit mist opacity'),None)
if opacity is None:
    opacity=mel.create_material_expression(mist,unreal.MaterialExpressionMultiply,300,200);opacity.set_editor_property('desc','Weather-lit mist opacity')
    mel.connect_material_expressions(source,'Op',opacity,'A');mel.connect_material_expressions(light,'',opacity,'B')
mel.connect_material_property(opacity,'',unreal.MaterialProperty.MP_OPACITY)
assert mel.connect_material_expressions(source,'Op',opacity,'A')
mel.recompile_material(mist);assert eal.save_loaded_asset(mist,only_if_is_dirty=False)
(report/'applied.json').write_text(json.dumps(dict(saved=True,water_foam_emission_removed=True,solar_caustics=True,solar_underwater_rays=True,soft_moon=True,fine_rain=True),indent=2))
unreal.log('WEATHER_REFINEMENT_MATERIALS_SAVED')
