"""Unreal: a Fab tree's merged bark and its 3 bark variations (user 2026-11-05; Tools/World/make_bark_textures.py, merge_fab_bark.py).
  RPG_BARK_TREES=A (a Lux letter, or Jabami) UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript=".../Tools/World/import_fab_bark.py" -unattended -RenderOffscreen
- textures -> /Game/RPG/Environment/Textures/Bark; M_Fab_Bark (lit, no emissive, dithered LOD) + MI_Bark_<tree>_1..3
- re-imports /Game/FabPacks/Restyled/SM_Lux_Tree_<c> IN PLACE, so L_WorldVista's copies keep their position, rotation and scale
- each placed copy gets one of its model's 3 barks, evenly (per-actor material override, saved in the map)
- Lux trees still on the pack's trunk get MI_Fab_Trunk_NoGlow: the pack's MM_Atlas has an always-on emissive that makes trunks
  brighter at midnight than at noon. It is a child instance on our meshes; the pack's own materials are not edited.
RPG_BARK_SPOTS_ONLY=1: writes camera spots near this tree's copies and changes nothing. Prints FABBARK lines."""
import unreal, os, math
EAL, MEL = unreal.EditorAssetLibrary, unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools(); SMES = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem); eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
OUTS = 'C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs'
SRC_TEX, MERGED = f'{OUTS}/World_R02/textures/Bark', f'{OUTS}/FabPacks/_bark_merged'
TEX, MATS, RESTYLED = '/Game/RPG/Environment/Textures/Bark', '/Game/RPG/Environment/World/Materials', '/Game/FabPacks/Restyled'
LUX_TRUNK = '/Game/Vegetation_Stylized_Pack_ByLuxArtStudios/Shaders/Material_Instances/MI_Trunk'
SPOTS = os.environ.get('RPG_BARK_SPOTS_FILE', f'{MERGED}/shot_spots.txt')
TREES = [x for x in os.environ.get('RPG_BARK_TREES', 'A').replace(',', ' ').split() if x]
problems = 0
def problem(s):
    global problems; problems += 1; unreal.log_warning('FABBARK_PROBLEM ' + s)
def save(a):
    if not EAL.save_loaded_asset(a, only_if_is_dirty=False): problem('save failed ' + a.get_path_name())

def mesh_of(c): return 'SM_Jabami_Tree_Ours' if c == 'Jabami' else f'SM_Lux_Tree_{c}'
def bark_of(c): return 'Jabami' if c == 'Jabami' else f'Lux_{c}'

def copies(mesh_name):
    out = []
    for a in eas.get_all_level_actors():
        if a.get_actor_label().startswith('FabTree_'):
            c = a.get_component_by_class(unreal.StaticMeshComponent)
            if c and c.static_mesh and c.static_mesh.get_name() == mesh_name: out.append((a.get_actor_label(), a, c))
    return sorted(out, key=lambda x: x[0])

def texture(name, normal):
    t = unreal.AssetImportTask(); t.filename = f'{SRC_TEX}/{name}.png'; t.destination_path = TEX; t.destination_name = 'T_' + name
    t.automated = True; t.replace_existing = True; t.save = True
    if not os.path.exists(t.filename): problem('missing ' + t.filename); return None
    TOOLS.import_asset_tasks([t]); tx = unreal.load_asset(f'{TEX}/T_{name}')
    if not tx: problem('import failed ' + name); return None
    if normal:
        tx.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP); tx.set_editor_property('srgb', False)
    else:
        tx.set_editor_property('srgb', True)
    save(tx); return tx

def bark_master(color, normal):
    path = f'{MATS}/M_Fab_Bark'
    if EAL.does_asset_exist(path): return unreal.load_asset(path)
    m = TOOLS.create_asset('M_Fab_Bark', MATS, unreal.Material, unreal.MaterialFactoryNew())
    c = MEL.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -500, -100)
    c.set_editor_property('parameter_name', 'Bark'); c.set_editor_property('texture', color)
    n = MEL.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -500, 250)
    n.set_editor_property('parameter_name', 'Bark Normal'); n.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL); n.set_editor_property('texture', normal)
    r = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -300, 100)
    r.set_editor_property('parameter_name', 'Roughness'); r.set_editor_property('default_value', .85)
    MEL.connect_material_property(c, 'RGB', unreal.MaterialProperty.MP_BASE_COLOR); MEL.connect_material_property(n, 'RGB', unreal.MaterialProperty.MP_NORMAL)
    MEL.connect_material_property(r, '', unreal.MaterialProperty.MP_ROUGHNESS)
    m.set_editor_property('dithered_lod_transition', True); m.set_editor_property('used_with_instanced_static_meshes', True)
    MEL.recompile_material(m); save(m); return m

def child(name, folder, parent):
    path = f'{folder}/{name}'
    mi = unreal.load_asset(path) if EAL.does_asset_exist(path) else TOOLS.create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, parent); return mi

def reimport(name):
    """in place, as restyle_fab_trees.canopy_copy; leaves keep their instance"""
    path = f'{RESTYLED}/{name}'; old = unreal.load_asset(path); leaf = next(s.material_interface for s in old.static_materials if 'leaves' in str(s.material_slot_name).lower())
    it = unreal.AssetImportTask(); it.filename = f'{MERGED}/{name}.fbx'; it.destination_path = RESTYLED; it.destination_name = name
    it.automated = True; it.replace_existing = True; it.save = True
    ui = unreal.FbxImportUI(); ui.import_mesh = True; ui.import_materials = False; ui.import_textures = False; ui.import_as_skeletal = False
    sm = ui.static_mesh_import_data; sm.combine_meshes = True; sm.auto_generate_collision = False; sm.generate_lightmap_u_vs = False
    sm.vertex_color_import_option = unreal.VertexColorImportOption.REPLACE; sm.normal_import_method = unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS
    sm.set_editor_property('reorder_material_to_fbx_order', True)
    it.options = ui; TOOLS.import_asset_tasks([it]); m = unreal.load_asset(path)
    for i in (1, 2):
        if SMES.import_lod(m, i, f'{MERGED}/{name}_LOD{i}.fbx') != i: problem(f'{name} LOD{i} import failed')
    try: SMES.set_lod_screen_sizes(m, [1.0, .15, .05][:m.get_num_lods()])
    except Exception as ex: problem(f'{name} LOD screen sizes: {ex}')
    return m, leaf

unreal.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')
les.load_level('/Game/RPG/Maps/L_WorldVista')
if os.environ.get('RPG_BARK_SPOTS_ONLY') == '1':
    d = (math.cos(math.radians(-40)), math.sin(math.radians(-40))); spots = []
    for c in TREES:
        for lab, a, comp in copies(mesh_of(c))[:3]:
            l = a.get_actor_location(); spots.append(f'{l.x + d[0] * 380:.0f} {l.y + d[1] * 380:.0f} {l.z + 170:.0f} 8 140')
            unreal.log(f'FABBARK spot {lab} {spots[-1]}')
    open(SPOTS, 'w').write(';'.join(spots)); unreal.log('FABBARK_DONE spots'); unreal.SystemLibrary.quit_editor(); raise SystemExit
master = None; barks = {}
for c in TREES:
    tree = bark_of(c); barks[c] = []
    for i in (1, 2, 3):
        col = texture(f'Bark_{tree}_{i}_BaseColor', False); nor = texture(f'Bark_{tree}_{i}_Normal', True)
        master = master or bark_master(col, nor)
        mi = child(f'MI_Bark_{tree}_{i}', MATS, master)
        MEL.set_material_instance_texture_parameter_value(mi, 'Bark', col); MEL.set_material_instance_texture_parameter_value(mi, 'Bark Normal', nor)
        MEL.update_material_instance(mi); save(mi); barks[c].append(mi)
    m, leaf = reimport(mesh_of(c))
    for i, s in enumerate(m.static_materials):
        m.set_material(i, leaf if 'leaves' in str(s.material_slot_name).lower() else barks[c][0])
    save(m)
    unreal.log(f'FABBARK {mesh_of(c)}: lods={m.get_num_lods()}, hulls={unreal.EditorStaticMeshLibrary.get_convex_collision_count(m)}, slots={[(str(s.material_slot_name), s.material_interface.get_name()) for s in m.static_materials]}, tris={[SMES.get_number_of_triangles(m, l) if hasattr(SMES, "get_number_of_triangles") else "?" for l in range(m.get_num_lods())]}')
    for k, (lab, a, comp) in enumerate(copies(mesh_of(c))):
        a.modify(); l0 = a.get_actor_location()
        for i, s in enumerate(m.static_materials):                     # every bark slot, so no LOD can show another bark
            if 'bark' in str(s.material_slot_name).lower(): comp.set_material(i, barks[c][k % 3])
        unreal.log(f'FABBARK {lab} at ({l0.x:.0f},{l0.y:.0f},{l0.z:.0f}) -> bark {k % 3 + 1}')
# no night glow on the Lux trunks not yet given their barks
ng = child('MI_Fab_Trunk_NoGlow', RESTYLED, unreal.load_asset(LUX_TRUNK))
MEL.set_material_instance_scalar_parameter_value(ng, 'Emissive Intensity', 0.0); MEL.update_material_instance(ng); save(ng)
for c in 'ABCDEFGHIJ':
    m = unreal.load_asset(f'{RESTYLED}/SM_Lux_Tree_{c}')
    if not m: problem(f'SM_Lux_Tree_{c} missing'); continue
    changed = False
    for i, s in enumerate(m.static_materials):
        if s.material_interface and s.material_interface.get_name() == 'MI_Trunk': m.set_material(i, ng); changed = True
    if changed: save(m); unreal.log(f'FABBARK SM_Lux_Tree_{c}: trunk glow off')
if not les.save_current_level(): problem('map save failed')
unreal.log(f'FABBARK_DONE problems={problems}')
unreal.SystemLibrary.quit_editor()
