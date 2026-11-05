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

CANOPY = 'C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs/FabPacks/_canopy_fbx'   # Tools/World/grow_fab_canopies.py
SMES = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
ONLY = [x for x in os.environ.get('RPG_FAB_TREES', '').replace(',', ' ').split() if x]   # e.g. "I" = only SM_Lux_Tree_I

def canopy_copy(src_path, name, mi):
    """R2 (user 2026-11-04): the tree with its foliage REBUILT as rounded clumps in Blender (pack trunk + branches kept,
    pack leaf quads replaced) - already at its 15-20 m size; LOD1/2 + UCX trunk hull from the same export."""
    src = unreal.load_asset(src_path)
    bark = next((s.material_interface for s in src.static_materials if not is_leaf(str(s.material_slot_name), s.material_interface)), None)
    path = f'{OUT}/{name}'
    if EAL.does_asset_exist(path): EAL.delete_asset(path)
    it = unreal.AssetImportTask(); it.filename = f'{CANOPY}/{name}.fbx'; it.destination_path = OUT; it.destination_name = name
    it.automated = True; it.replace_existing = True; it.save = True
    ui = unreal.FbxImportUI(); ui.import_mesh = True; ui.import_materials = False; ui.import_textures = False; ui.import_as_skeletal = False
    sm = ui.static_mesh_import_data; sm.combine_meshes = True; sm.auto_generate_collision = False
    sm.generate_lightmap_u_vs = False; sm.vertex_color_import_option = unreal.VertexColorImportOption.REPLACE
    sm.normal_import_method = unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS      # keep the clump normals
    sm.set_editor_property('reorder_material_to_fbx_order', True)
    it.options = ui; TOOLS.import_asset_tasks([it])
    m = unreal.load_asset(path)
    for i in (1, 2):
        f = f'{CANOPY}/{name}_LOD{i}.fbx'
        if os.path.exists(f) and SMES.import_lod(m, i, f) != i: unreal.log_warning(f'FABSTYLE {name} LOD{i} import failed')
    try: m.set_editor_property('auto_compute_lod_screen_size', False)
    except Exception: pass
    try: SMES.set_lod_screen_sizes(m, [1.0, 0.15, 0.05][:m.get_num_lods()])   # full leaves ~2x as far (user 2026-11-05, flying)
    except Exception as ex: unreal.log_warning(f'FABSTYLE {name} LOD screen sizes not set: {ex}')
    for i, s in enumerate(m.static_materials):
        m.set_material(i, mi if 'leaves' in str(s.material_slot_name).lower() else bark)
    EAL.save_loaded_asset(m)
    return m

mi_lux = leaf_mi('MI_Fab_OurLeaves_Lux', 1600.0)
mi_jab = leaf_mi('MI_Fab_OurLeaves_Jabami', 1600.0)
TARGET = {c: 1500 + 500 * ((i * 0.618) % 1.0) for i, c in enumerate('ABCDEFGHIJ')}   # 15-20 m, varied per tree
jobs = [(f'{LUX}/S_Tree_{c}', f'SM_Lux_Tree_{c}', mi_lux, TARGET[c]) for c in 'ABCDEFGHIJ'] + \
       [('/Game/FabPacks/JabamiAnimeTree/SM_Jabami_Tree', 'SM_Jabami_Tree_Ours', mi_jab, 1750)]
for src, name, mi, tgt in jobs:
    if not EAL.does_asset_exist(src): unreal.log_warning(f'FABSTYLE missing {src}'); continue
    if ONLY and not any(name.endswith('_' + o) or name == o for o in ONLY): continue
    if os.path.exists(f'{CANOPY}/{name}.fbx'):
        m = canopy_copy(src, name, mi)
        unreal.log(f'FABSTYLE {name} rebuilt canopy -> {m.get_bounds().box_extent.z * 2 / 100:.1f} m tall, lods={m.get_num_lods()}, '
                   f'hulls={unreal.EditorStaticMeshLibrary.get_convex_collision_count(m)}, slots={[str(s.material_slot_name) for s in m.static_materials]}')
        continue
    m, f = scaled_copy(src, name, tgt)
    if not m: continue
    swapped = []
    for i, s in enumerate(m.static_materials):
        if is_leaf(str(s.material_slot_name), s.material_interface):
            m.set_material(i, mi); swapped.append(str(s.material_slot_name))
    EAL.save_loaded_asset(m)
    unreal.log(f'FABSTYLE {name} x{f:.2f} -> {m.get_bounds().box_extent.z * 2 / 100:.1f} m tall, lods={m.get_num_lods()}, '
               f'hulls={unreal.EditorStaticMeshLibrary.get_convex_collision_count(m)}, our leaves on {swapped}')
# our ancient hero tree with the same rebuilt clump foliage + leaves (user 2026-11-05), swapped into the vista's Tree_Hero
ANC_SRC = '/Game/RPG/Environment/World/Meshes/SM_Tree_Ancient'
if (not ONLY or 'Ancient' in ONLY) and os.path.exists(f'{CANOPY}/SM_Tree_Ancient_Clumps.fbx') and EAL.does_asset_exist(ANC_SRC):
    m = canopy_copy(ANC_SRC, 'SM_Tree_Ancient_Clumps', mi_lux)
    unreal.log(f'FABSTYLE SM_Tree_Ancient_Clumps -> {m.get_bounds().box_extent.z * 2 / 100:.1f} m, lods={m.get_num_lods()}, '
               f'hulls={unreal.EditorStaticMeshLibrary.get_convex_collision_count(m)}')
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem); eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level('/Game/RPG/Maps/L_WorldVista')
    hero = next((a for a in eas.get_all_level_actors() if a.get_actor_label() == 'Tree_Hero'), None)
    if hero:
        hero.static_mesh_component.set_static_mesh(m); les.save_current_level()
        unreal.log('FABSTYLE Tree_Hero in L_WorldVista now uses SM_Tree_Ancient_Clumps (same place, rotation, 0.75 scale)')
    else: unreal.log_warning('FABSTYLE Tree_Hero not found in L_WorldVista')

# foliage-check reference (L_AssetReview section 15): Tree I the OLD way (pack leaf quads + our leaves) next to the rebuilt one
if not ONLY or 'I' in ONLY:
    m, f = scaled_copy(f'{LUX}/S_Tree_I', 'SM_Lux_Tree_I_Before', TARGET['I'])
    if m:
        for i, s in enumerate(m.static_materials):
            if is_leaf(str(s.material_slot_name), s.material_interface): m.set_material(i, mi_lux)
        EAL.save_loaded_asset(m); unreal.log(f'FABSTYLE SM_Lux_Tree_I_Before (reference) x{f:.2f}')
unreal.log('FABSTYLE_DONE')
unreal.SystemLibrary.quit_editor()
