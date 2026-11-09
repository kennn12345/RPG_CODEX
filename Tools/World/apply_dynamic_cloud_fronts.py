"""Create a project-owned volumetric cloud material with continuous XY advection."""
import unreal,json,runpy
from pathlib import Path
root=Path(unreal.Paths.project_dir());mel=unreal.MaterialEditingLibrary;eal=unreal.EditorAssetLibrary
path='/Game/RPG/Environment/Weather/M_Weather_Clouds'
changed=False
m=unreal.load_asset(path)
if m is None:
    base=unreal.load_asset('/Game/RPG/Environment/World/Materials/MI_Vista_Clouds')
    while isinstance(base,unreal.MaterialInstance):base=base.get_editor_property('parent')
    m=unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset('M_Weather_Clouds','/Game/RPG/Environment/Weather',base)
    assert m
    changed=True
expressions=list(mel.get_material_expressions(m))
if not any(e.get_class().get_name()=='MaterialExpressionVectorParameter' and str(e.get_editor_property('parameter_name'))=='WeatherCloudOffset' for e in expressions):
    changed=True
    offset=mel.create_material_expression(m,unreal.MaterialExpressionVectorParameter,-3000,0)
    offset.set_editor_property('parameter_name','WeatherCloudOffset');offset.set_editor_property('default_value',unreal.LinearColor(0,0,0,0))
    count=0
    for target in expressions:
        # Only advect the texture coordinates; leave planetary altitude/radius calculation stationary.
        if target.get_class().get_name()!='MaterialExpressionDivide':continue
        inputs=list(mel.get_inputs_for_material_expression(m,target));names=list(mel.get_material_expression_input_names(target))
        assert len(inputs)==len(names)
        for name,source in zip(names,inputs):
            if source is None or source.get_class().get_name()!='MaterialExpressionWorldPosition':continue
            sub=mel.create_material_expression(m,unreal.MaterialExpressionSubtract,-2800,0)
            assert mel.connect_material_expressions(source,'',sub,'A')
            assert mel.connect_material_expressions(offset,'',sub,'B')
            assert mel.connect_material_expressions(sub,'',target,name)
            count+=1
    assert count==1,count
if not m.get_editor_property('used_with_volumetric_cloud'):
    m.set_editor_property('used_with_volumetric_cloud',True);changed=True
if changed:
    mel.recompile_material(m);assert eal.save_loaded_asset(m,only_if_is_dirty=False)
# Preserve all inherited instance settings (including texture/static parameters).
source=unreal.load_asset('/Game/RPG/Environment/World/Materials/MI_Vista_Clouds')
instance=unreal.load_asset('/Game/RPG/Environment/Weather/MI_Weather_Clouds')
if instance is None:instance=unreal.AssetToolsHelpers.get_asset_tools().create_asset('MI_Weather_Clouds','/Game/RPG/Environment/Weather',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
mel.set_material_instance_parent(instance,m)
settings={}
for kind in ['scalar','vector','texture','static_switch']:
    names=getattr(mel,'get_'+kind+'_parameter_names')(source)
    settings[kind]={}
    for name in names:
        value=getattr(mel,'get_material_instance_'+kind+'_parameter_value')(source,name)
        settings[kind][str(name)]=str(value)
        if value is not None:
            result=getattr(mel,'set_material_instance_'+kind+'_parameter_value')(instance,name,value)
            if not result:unreal.log_warning('Cloud inherited parameter unchanged or unavailable: '+kind+' '+str(name))
mel.update_material_instance(instance);assert eal.save_loaded_asset(instance,only_if_is_dirty=False)
(root/'Saved/CloudFogRevision/cloud_inherited_settings.json').write_text(json.dumps(settings,indent=2))
runpy.run_path(str(root/'Tools/World/install_moon_weather.py'),run_name='__main__')
(root/'Saved/CloudFogRevision/dynamic_fronts_applied.json').write_text(json.dumps(dict(cloud_material=path,integrated_wind=True,moon_disc_scale=.45,moon_halo_scale=.35),indent=2))
unreal.log('DYNAMIC_CLOUD_FRONTS_SAVED')
