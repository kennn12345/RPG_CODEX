import unreal,json
from pathlib import Path
mel=unreal.MaterialEditingLibrary
out=[]
for path in ['/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud','/Game/RPG/Environment/Weather/M_Weather_Clouds','/Game/RPG/Environment/Weather/MI_Weather_Clouds']:
 m=unreal.load_asset(path)
 d=dict(asset=path,scalar=list(map(str,mel.get_scalar_parameter_names(m))),vector=list(map(str,mel.get_vector_parameter_names(m))))
 if isinstance(m,unreal.Material):
  d['domain']=str(m.get_editor_property('material_domain'));d['cloud_usage']=m.get_editor_property('used_with_volumetric_cloud')
  d['properties']={str(prop):str(mel.get_material_property_input_node(m,prop)) for prop in [unreal.MaterialProperty.MP_EMISSIVE_COLOR,unreal.MaterialProperty.MP_BASE_COLOR,unreal.MaterialProperty.MP_SUBSURFACE_COLOR]}
 out.append(d)
Path(unreal.Paths.project_dir(),'Saved/CloudFogRevision/cloud_cache.json').write_text(json.dumps(out,indent=2))

