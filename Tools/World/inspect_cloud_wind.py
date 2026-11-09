import unreal,json
from pathlib import Path
mel=unreal.MaterialEditingLibrary
m=unreal.load_asset('/Game/RPG/Environment/World/Materials/MI_Vista_Clouds')
while isinstance(m,unreal.MaterialInstance):m=m.get_editor_property('parent')
out={'base':m.get_path_name(),'docs':str(mel.get_inputs_for_material_expression.__doc__),'nodes':[]}
for e in mel.get_material_expressions(m):
    inputs=mel.get_inputs_for_material_expression(m,e)
    if any(i and i.get_class().get_name()=='MaterialExpressionWorldPosition' for i in inputs):
        out['nodes'].append(dict(node=e.get_name(),kind=e.get_class().get_name(),names=list(mel.get_material_expression_input_names(e)),inputs=[i.get_name() if i else None for i in inputs]))
Path(unreal.Paths.project_dir(),'Saved/CloudFogRevision/wind_graph.json').write_text(json.dumps(out,indent=2))
unreal.log('WIND_GRAPH_INSPECTED')
