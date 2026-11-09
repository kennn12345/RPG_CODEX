import unreal,json
from pathlib import Path
mel=unreal.MaterialEditingLibrary
out=[]
for path in ['/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud','/Game/RPG/Environment/Weather/M_Weather_Clouds']:
 m=unreal.load_asset(path)
 for e in mel.get_material_expressions(m):
  for i in mel.get_inputs_for_material_expression(m,e):
   if i and i.get_class().get_name() in ['MaterialExpressionWorldPosition','MaterialExpressionSubtract']:
    out.append(dict(asset=path,target=e.get_name(),source=i.get_name(),output=str(mel.get_input_node_output_name_for_material_expression(e,i))))
mi=unreal.load_asset('/Game/RPG/Environment/World/Materials/MI_Vista_Clouds')
for prop in ['scalar_parameter_values','vector_parameter_values','static_parameters']:
 try:out.append({prop:str(mi.get_editor_property(prop))})
 except:pass
Path(unreal.Paths.project_dir(),'Saved/CloudFogRevision/wind_connections.json').write_text(json.dumps(out,indent=2))
