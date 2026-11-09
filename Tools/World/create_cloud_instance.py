import unreal,json
from pathlib import Path
root=Path(unreal.Paths.project_dir());mel=unreal.MaterialEditingLibrary;eal=unreal.EditorAssetLibrary
m=unreal.load_asset('/Game/RPG/Environment/Weather/M_Weather_Clouds')
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
