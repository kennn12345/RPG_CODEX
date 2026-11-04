"""Unreal: bring the AI-permitted Fab tree packs into RPG_CODEX (both CC BY 4.0 - credits in Docs/CREDITS.md).
  UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript=".../Tools/World/import_fab_trees.py" -unattended -RenderOffscreen
- Lux Art Studios "Vegetation Stylized Kit": copied as-is into /Game/Vegetation_Stylized_Pack_ByLuxArtStudios (UE 5.4 assets,
  load in 5.8); this script only reports its meshes / materials and resaves nothing.
- JABAMI Production "Jabami Anime Tree v4": FBX + textures -> /Game/FabPacks/JabamiAnimeTree (SM_Jabami_Tree, masked
  two-sided leaf materials, bark material). Re-runnable: assets are updated in place.
Both pack folders are git-ignored (licences forbid redistributing the raw assets)."""
import unreal, os
EAL, MEL = unreal.EditorAssetLibrary, unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
SRC = 'C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs/FabPacks/Jabami_AnimeTree_v4/extracted/source/inner'
DST = '/Game/FabPacks/JabamiAnimeTree'
LUX = '/Game/Vegetation_Stylized_Pack_ByLuxArtStudios'
unreal.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')

def imp(path, name, opts=None):
    t = unreal.AssetImportTask(); t.filename = path; t.destination_path = DST; t.destination_name = name
    t.automated = True; t.replace_existing = True; t.save = True
    if opts: t.options = opts
    TOOLS.import_asset_tasks([t]); return unreal.load_asset(f'{DST}/{name}')

texs = {n: imp(f'{SRC}/{f}', n) for n, f in (('T_Jabami_Bark', 'tron_tree_remast_tex.png'), ('T_Jabami_Leaves05', 'leave_tree_05_tex.png'),
                                             ('T_Jabami_Leaves09', 'leave_tree_09_tex.png'))}
o = unreal.FbxImportUI(); o.import_mesh = True; o.import_materials = False; o.import_textures = False; o.import_as_skeletal = False
sm = o.static_mesh_import_data; sm.combine_meshes = True; sm.auto_generate_collision = True; sm.generate_lightmap_u_vs = False
mesh = imp(f'{SRC}/medium01.fbx', 'SM_Jabami_Tree', o)

def mat(name, tex, leaf):
    path = f'{DST}/{name}'
    m = unreal.load_asset(path) if EAL.does_asset_exist(path) else TOOLS.create_asset(name, DST, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(m)
    ts = MEL.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -500, 0)
    ts.set_editor_property('parameter_name', 'Tex'); ts.set_editor_property('texture', tex)
    MEL.connect_material_property(ts, 'RGB', unreal.MaterialProperty.MP_BASE_COLOR)
    rough = MEL.create_material_expression(m, unreal.MaterialExpressionConstant, -300, 200); rough.set_editor_property('r', 0.85)
    MEL.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    if leaf:
        m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED); m.set_editor_property('two_sided', True)
        m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
        MEL.connect_material_property(ts, 'A', unreal.MaterialProperty.MP_OPACITY_MASK)
        MEL.connect_material_property(ts, 'RGB', unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    m.set_editor_property('used_with_instanced_static_meshes', True)
    MEL.recompile_material(m); EAL.save_loaded_asset(m); return m

m_bark = mat('M_Jabami_Bark', texs['T_Jabami_Bark'], False)
m_l0 = mat('M_Jabami_Leaves05', texs['T_Jabami_Leaves05'], True)
m_l1 = mat('M_Jabami_Leaves09', texs['T_Jabami_Leaves09'], True)
for i, s in enumerate(mesh.static_materials):
    n = str(s.material_slot_name).lower()
    mesh.set_material(i, m_bark if 'tronc' in n else (m_l0 if '00' in n else m_l1))
b = mesh.get_bounds().box_extent
unreal.log(f'FAB_JABAMI size_cm=({b.x*2:.0f},{b.y*2:.0f},{b.z*2:.0f}) tris={mesh.get_num_triangles(0)} slots={[str(s.material_slot_name) for s in mesh.static_materials]}')
EAL.save_loaded_asset(mesh)
for path in EAL.list_assets(f'{LUX}/Mesh', recursive=True, include_folder=False):
    a = unreal.load_asset(path.split('.')[0])
    if isinstance(a, unreal.StaticMesh):
        bb = a.get_bounds().box_extent
        unreal.log(f'FAB_LUX {a.get_name()} size_cm=({bb.x*2:.0f},{bb.y*2:.0f},{bb.z*2:.0f}) tris={a.get_num_triangles(0)} lods={a.get_num_lods()} mats={[m.material_interface.get_name() if m.material_interface else None for m in a.static_materials]}')
unreal.log('FAB_IMPORT_DONE')
unreal.SystemLibrary.quit_editor()
