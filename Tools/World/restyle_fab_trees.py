"""Unreal: our hero-tree leaves on the AI-permitted Fab trees (Lux Vegetation Stylized Kit, JABAMI Anime Tree - CC BY 4.0),
scaled up to 15-20 m tall (user 2026-11-03).
  UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript=".../Tools/World/restyle_fab_trees.py" -unattended -RenderOffscreen
Each tree is exported to FBX (all LODs + collision) and re-imported at the target scale into
/Game/FabPacks/Restyled/SM_<pack>_<tree> (git-ignored with the packs; originals untouched) - LOD build-scale settings are
ignored by these pack meshes, a scaled re-import is reliable. Leaf slots get MI_Fab_OurLeaves_* = the hero tree's leaf
material (Mayu pack M_StylizedTree, our 2x2 tiled leaf-spray mask, video palette) with the height gradient set for
15-20 m trees; bark slots keep the pack's own material. Re-runnable. Prints FABSTYLE lines."""
import unreal, os
EAL, MEL = unreal.EditorAssetLibrary, unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
OUT = '/Game/FabPacks/Restyled'
TMP = 'C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs/FabPacks/_restyle_fbx'
LUX = '/Game/Vegetation_Stylized_Pack_ByLuxArtStudios/Mesh/Trees'
HERO_MI = '/Game/RPG/Environment/World/Materials/MI_Hero_MayuLeaves'
os.makedirs(TMP, exist_ok=True)
unreal.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')

def leaf_mi(name, height):
    path = f'{OUT}/{name}'
    mi = unreal.load_asset(path) if EAL.does_asset_exist(path) else \
        TOOLS.create_asset(name, OUT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, unreal.load_asset(HERO_MI))  # inherits our texture + palette
    MEL.set_material_instance_scalar_parameter_value(mi, 'Height', height)
    EAL.save_loaded_asset(mi); return mi

def is_leaf(slot, mat):
    s = (slot + ' ' + (mat.get_name() if mat else '')).lower()
    return any(k in s for k in ('leaf', 'leave', 'bush'))

def scaled_copy(src_path, name, target_cm):
    src = unreal.load_asset(src_path)
    slots = [(str(s.material_slot_name), s.material_interface) for s in src.static_materials]
    f = target_cm / max(src.get_bounds().box_extent.z * 2, 1.0)
    fbx = f'{TMP}/{name}.fbx'
    t = unreal.AssetExportTask(); t.object = src; t.filename = fbx; t.automated = True; t.prompt = False; t.replace_identical = True
    t.exporter = unreal.StaticMeshExporterFBX()
    o = unreal.FbxExportOption(); o.level_of_detail = True; o.collision = True; o.vertex_color = True
    t.options = o
    if not unreal.Exporter.run_asset_export_task(t): unreal.log_warning(f'FABSTYLE export failed {name}'); return None, f
    path = f'{OUT}/{name}'
    if EAL.does_asset_exist(path): EAL.delete_asset(path)
    it = unreal.AssetImportTask(); it.filename = fbx; it.destination_path = OUT; it.destination_name = name
    it.automated = True; it.replace_existing = True; it.save = True
    ui = unreal.FbxImportUI(); ui.import_mesh = True; ui.import_materials = False; ui.import_textures = False; ui.import_as_skeletal = False
    sm = ui.static_mesh_import_data; sm.combine_meshes = True; sm.auto_generate_collision = False; sm.import_mesh_lo_ds = True
    sm.generate_lightmap_u_vs = False; sm.import_uniform_scale = f
    sm.vertex_color_import_option = unreal.VertexColorImportOption.REPLACE
    it.options = ui; TOOLS.import_asset_tasks([it])
    m = unreal.load_asset(path)
    for i, (sn, mat) in enumerate(slots):                              # export keeps slot order
        if i < len(m.static_materials): m.set_material(i, mat)
    return m, f

mi_lux = leaf_mi('MI_Fab_OurLeaves_Lux', 1600.0)
mi_jab = leaf_mi('MI_Fab_OurLeaves_Jabami', 1600.0)
TARGET = {c: 1500 + 500 * ((i * 0.618) % 1.0) for i, c in enumerate('ABCDEFGHIJ')}   # 15-20 m, varied per tree
jobs = [(f'{LUX}/S_Tree_{c}', f'SM_Lux_Tree_{c}', mi_lux, TARGET[c]) for c in 'ABCDEFGHIJ'] + \
       [('/Game/FabPacks/JabamiAnimeTree/SM_Jabami_Tree', 'SM_Jabami_Tree_Ours', mi_jab, 1750)]
for src, name, mi, tgt in jobs:
    if not EAL.does_asset_exist(src): unreal.log_warning(f'FABSTYLE missing {src}'); continue
    m, f = scaled_copy(src, name, tgt)
    if not m: continue
    swapped = []
    for i, s in enumerate(m.static_materials):
        if is_leaf(str(s.material_slot_name), s.material_interface):
            m.set_material(i, mi); swapped.append(str(s.material_slot_name))
    EAL.save_loaded_asset(m)
    unreal.log(f'FABSTYLE {name} x{f:.2f} -> {m.get_bounds().box_extent.z * 2 / 100:.1f} m tall, lods={m.get_num_lods()}, '
               f'hulls={unreal.EditorStaticMeshLibrary.get_convex_collision_count(m)}, our leaves on {swapped}')
unreal.log('FABSTYLE_DONE')
unreal.SystemLibrary.quit_editor()
