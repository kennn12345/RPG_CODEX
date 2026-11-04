"""Inspect the Fab pack Mayu's Stylized Trees (/Game/StylizedTreesByMayu, Standard License, added by the user 2026-11-03):
dumps material graphs / parameters and mesh stats, exports the leaf textures to PNG for review.
  UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript=".../Tools/World/inspect_mayu_pack.py" -unattended -RenderOffscreen
Output: log lines MAYU_* and PNGs in outputs/World_R02/mayu_pack/."""
import unreal, os
PACK = '/Game/StylizedTreesByMayu'
OUT = 'C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs/World_R02/mayu_pack'
os.makedirs(OUT, exist_ok=True)
EAL, MEL = unreal.EditorAssetLibrary, unreal.MaterialEditingLibrary

for path in EAL.list_assets(PACK, recursive=True, include_folder=False):
    a = unreal.load_asset(path.split('.')[0]); cls = a.get_class().get_name()
    if isinstance(a, unreal.Material):
        exprs = [e.get_class().get_name().replace('MaterialExpression', '') for e in MEL.get_material_expressions(a)] if hasattr(MEL, 'get_material_expressions') else []
        unreal.log(f'MAYU_MAT {path} blend={a.get_editor_property("blend_mode")} shading={a.get_editor_property("shading_model")} two_sided={a.get_editor_property("two_sided")} exprs={len(exprs)} {sorted(set(exprs))}')
        for kind, getter in (('scalar', MEL.get_scalar_parameter_names), ('vector', MEL.get_vector_parameter_names), ('texture', MEL.get_texture_parameter_names), ('switch', MEL.get_static_switch_parameter_names)):
            try: unreal.log(f'MAYU_PARAMS {path} {kind} {list(getter(a))}')
            except Exception as ex: unreal.log(f'MAYU_PARAMS {path} {kind} ERR {ex}')
    elif isinstance(a, unreal.MaterialInstanceConstant):
        parent = a.get_editor_property('parent')
        sc = {str(p.parameter_info.name): round(p.parameter_value, 3) for p in a.get_editor_property('scalar_parameter_values')}
        vc = {str(p.parameter_info.name): [round(p.parameter_value.r, 3), round(p.parameter_value.g, 3), round(p.parameter_value.b, 3)] for p in a.get_editor_property('vector_parameter_values')}
        tx = {str(p.parameter_info.name): (p.parameter_value.get_path_name() if p.parameter_value else None) for p in a.get_editor_property('texture_parameter_values')}
        unreal.log(f'MAYU_MI {path} parent={parent.get_path_name() if parent else None} scalars={sc} vectors={vc} textures={tx}')
    elif isinstance(a, unreal.StaticMesh):
        mats = [str(m.material_slot_name) + '=' + (m.material_interface.get_name() if m.material_interface else 'None') for m in a.static_materials]
        b = a.get_bounds().box_extent
        unreal.log(f'MAYU_MESH {path} tris={a.get_num_triangles(0)} lods={a.get_num_lods()} size_cm=({b.x*2:.0f},{b.y*2:.0f},{b.z*2:.0f}) mats={mats} nanite={a.get_editor_property("nanite_settings").enabled}')
    elif isinstance(a, unreal.Texture2D):
        unreal.log(f'MAYU_TEX {path} {a.blueprint_get_size_x()}x{a.blueprint_get_size_y()} srgb={a.get_editor_property("srgb")}')
        t = unreal.AssetExportTask(); t.object = a; t.filename = f'{OUT}/{a.get_name()}.png'; t.automated = True; t.prompt = False; t.replace_identical = True
        t.exporter = unreal.TextureExporterPNG()
        ok = unreal.Exporter.run_asset_export_task(t)
        unreal.log(f'MAYU_EXPORT {a.get_name()} {ok}')
    else:
        unreal.log(f'MAYU_OTHER {path} {cls}')
unreal.log('MAYU_DONE')
unreal.SystemLibrary.quit_editor()
