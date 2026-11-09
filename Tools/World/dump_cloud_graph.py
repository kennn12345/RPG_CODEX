"""Read-only: dump M_Weather_Clouds expressions and their input links to Saved/CloudLook/cloud_graph.txt."""
import unreal
from pathlib import Path
mel = unreal.MaterialEditingLibrary
m = unreal.load_asset('/Game/RPG/Environment/Weather/M_Weather_Clouds')
out = []
exprs = list(m.get_editor_property('expressions')) if hasattr(m, 'expressions') else []
try:
    exprs = list(unreal.MaterialEditingLibrary.get_material_expressions(m))  # newer API
except Exception:
    pass
ids = {e: i for i, e in enumerate(exprs)}

def describe(e):
    cls = e.get_class().get_name().replace('MaterialExpression', '')
    bits = [cls]
    for prop in ('parameter_name', 'input_name', 'code', 'description', 'const_a', 'const_b', 'r', 'constant', 'coordinate_index', 'texture', 'material_function', 'mask_r', 'mask_g', 'mask_b', 'mask_a', 'output_name'):
        try:
            v = e.get_editor_property(prop)
        except Exception:
            continue
        if v is None or v == '' or (isinstance(v, bool) and not v and prop.startswith('mask')):
            continue
        s = str(v)
        if hasattr(v, 'get_path_name'):
            s = v.get_path_name()
        bits.append(f'{prop}={s[:300]}')
    return ' '.join(bits)

for e in exprs:
    line = f'[{ids[e]}] ' + describe(e)
    try:
        ins = mel.get_inputs_for_material_expression(m, e)
        names = mel.get_material_expression_input_names(e) if hasattr(mel, 'get_material_expression_input_names') else []
        links = []
        for k, src in enumerate(ins):
            if src is not None and src in ids:
                links.append(f'{names[k] if k < len(names) else k}<-[{ids[src]}]')
        if links:
            line += '  IN: ' + ', '.join(links)
    except Exception as ex:
        line += f'  (inputs unavailable: {ex})'
    out.append(line)

for prop in ('extinction', 'albedo', 'emissive_color', 'ambient_occlusion', 'base_color'):
    pass
try:
    for name in ('MP_SubsurfaceColor', 'MP_BaseColor', 'MP_EmissiveColor', 'MP_AmbientOcclusion'):
        n = mel.get_material_property_input_node(m, getattr(unreal.MaterialProperty, name))
        out.append(f'OUTPUT {name} <- [{ids.get(n, "?")}]')
except Exception as ex:
    out.append(f'outputs unavailable: {ex}')
Path(unreal.Paths.project_saved_dir(), 'CloudLook', 'cloud_graph.txt').write_text('\n'.join(out), encoding='utf-8')
unreal.log(f'CLOUD_GRAPH_DUMPED {len(exprs)} expressions')
