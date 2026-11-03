"""Reusable rock library: the 21 Blender rocks (Rocks_R01: 6 size classes x A/B/C + 3 pebble clusters) imported into
/Game/RPG/Environment/Rocks/{Meshes,Materials,Foliage} with their UCX collision hulls, the painted blue-grey stone
material (M_Rock_Stone) and its moss instance (MI_Rock_Stone_Moss on Large A/C, MediumLarge A, Medium B - as built in
Blender), Nanite on, and a Foliage Type per rock for the Foliage tool.
  UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/import_rocks.py" -unattended
Safe to re-run (close the editor first): existing rocks and foliage types are kept and updated in place; missing or
wrong-sized rocks are re-imported and size-checked. The water test's own copies in /Game/RPG/Environment/Props are not touched.
build_vista.py imports the library only when it is missing."""
import unreal, json

SRC = 'C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs/Rocks_R01'
LIB = '/Game/RPG/Environment/Rocks'
MESHES, MATS, FOLIAGE = LIB + '/Meshes', LIB + '/Materials', LIB + '/Foliage'
EAL, MEL = unreal.EditorAssetLibrary, unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MANIFEST = json.load(open(SRC + '/rocks_manifest.json'))
T_STONE = unreal.load_asset('/Game/RPG/Environment/Textures/T_Stone_Painted_BaseColor')
# classic FBX importer (as build_vista.py): Interchange brought the first rock in at 100x without its UCX hulls
unreal.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')

def fresh(path):
    if EAL.does_asset_exist(path): EAL.delete_asset(path)

# ------------------------------------------------------------------ material (matches the Blender stone_material)
def node(m, cls, x, y, **p):
    e = MEL.create_material_expression(m, cls, x, y)
    for k, v in p.items(): e.set_editor_property(k, v)
    return e

def stone_material():
    fresh(f'{MATS}/M_Rock_Stone')
    m = TOOLS.create_asset('M_Rock_Stone', MATS, unreal.Material, unreal.MaterialFactoryNew())
    # painted stone projected from the three world axes (any rock size / rotation / placement keeps the same texel scale),
    # softened toward its mean, tinted; moss on up-facing faces (Moss = 1 in the moss instance)
    code = r'''
float3 an = abs(N);
float3 Tex = (TexXY * an.z + TexXZ * an.y + TexYZ * an.x) / max(an.x + an.y + an.z, 0.001);
float h = frac(sin(dot(floor(WP.xy / 37.0), float2(127.1, 311.7))) * 43758.5453);
float3 soft = lerp(dot(Tex, float3(0.333, 0.333, 0.334)).xxx, Tex, 1.2) * (0.75 + 0.5 * dot(Tex, float3(0.333, 0.333, 0.334)));   // painted detail, a little more contrast
float3 c = soft * Tint * Gain * (0.9 + 0.2 * h);
float mossMask = saturate((N.z - 0.45) / 0.3) * smoothstep(0.35, 0.65, frac(sin(dot(floor(WP.xy / 23.0), float2(41.3, 289.1))) * 9631.7) * 0.6 + 0.4 * N.z);
return lerp(c, MossC, mossMask * Moss);
'''
    c = node(m, unreal.MaterialExpressionCustom, -300, 0, code=code, output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    ins = []
    for n_ in ('TexXY', 'TexXZ', 'TexYZ', 'WP', 'N', 'Tint', 'Gain', 'MossC', 'Moss'):
        ci = unreal.CustomInput(); ci.set_editor_property('input_name', n_); ins.append(ci)
    c.set_editor_property('inputs', ins)
    wp = node(m, unreal.MaterialExpressionWorldPosition, -1500, 0)
    for i, (r_, g_, b_, nm) in enumerate(((True, True, False, 'TexXY'), (True, False, True, 'TexXZ'), (False, True, True, 'TexYZ'))):
        mk = node(m, unreal.MaterialExpressionComponentMask, -1300, i * 160, r=r_, g=g_, b=b_, a=False); MEL.connect_material_expressions(wp, '', mk, '')
        dv = node(m, unreal.MaterialExpressionDivide, -1150, i * 160, const_b=160.0); MEL.connect_material_expressions(mk, '', dv, 'A')   # 1.6 m tile
        ts = node(m, unreal.MaterialExpressionTextureSample, -1000, i * 160, texture=T_STONE)
        MEL.connect_material_expressions(dv, '', ts, 'UVs'); MEL.connect_material_expressions(ts, 'RGB', c, nm)
    MEL.connect_material_expressions(wp, '', c, 'WP')
    MEL.connect_material_expressions(node(m, unreal.MaterialExpressionPixelNormalWS, -900, 100), '', c, 'N')
    # Blender tint was (.47, .57, .80); it read purple under the warm vista sky, so this one is closer to neutral
    for i, (n_, v) in enumerate((('Tint', (.64, .62, .6)), ('MossC', (.16, .24, .05)))):
        MEL.connect_material_expressions(node(m, unreal.MaterialExpressionVectorParameter, -900, 200 + i * 100, parameter_name=n_,
                                              default_value=unreal.LinearColor(*v, 1)), '', c, n_)
    for i, (n_, v) in enumerate((('Gain', 1.35), ('Moss', 0.0))):
        MEL.connect_material_expressions(node(m, unreal.MaterialExpressionScalarParameter, -900, 400 + i * 100, parameter_name=n_,
                                              default_value=v), '', c, n_)
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(node(m, unreal.MaterialExpressionConstant, -300, 250, r=0.9), '', unreal.MaterialProperty.MP_ROUGHNESS)
    m.set_editor_property('used_with_instanced_static_meshes', True); m.set_editor_property('used_with_nanite', True)
    MEL.recompile_material(m); EAL.save_loaded_asset(m)
    fresh(f'{MATS}/MI_Rock_Stone_Moss')
    mi = TOOLS.create_asset('MI_Rock_Stone_Moss', MATS, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, m); MEL.set_material_instance_scalar_parameter_value(mi, 'Moss', 1.0)
    EAL.save_loaded_asset(mi)
    return m, mi

def import_rock(name, scale=100.0):
    fresh(f'{MESHES}/SM_{name}')
    t = unreal.AssetImportTask(); t.filename = f'{SRC}/exports/SM_{name}.fbx'; t.destination_path = MESHES
    t.automated = True; t.replace_existing = True; t.save = True
    o = unreal.FbxImportUI(); o.import_mesh = True; o.import_materials = False; o.import_textures = False; o.import_as_skeletal = False
    sm = o.static_mesh_import_data; sm.combine_meshes = True; sm.auto_generate_collision = False   # keeps the UCX hulls
    sm.generate_lightmap_u_vs = False
    sm.import_uniform_scale = scale               # Rocks_R01 FBXs usually come in at 1/100 (unit setting of that export)
    t.options = o; TOOLS.import_asset_tasks([t])
    return unreal.load_asset(list(t.imported_object_paths)[0])

def size_ratio(mesh, info):
    """imported size / Blender size (1 = right)."""
    b = mesh.get_bounds().box_extent
    return max(b.x, b.y, b.z) * 2 / (max(info['dimensions_m']) * 100)

def rock_mesh(name, info):
    """existing library mesh if it is right (placed copies keep working), else a fresh import checked against the Blender
    dimensions - one re-import produced a rock 100x too big without its hulls, so the scale is verified, not assumed."""
    path = f'{MESHES}/SM_{name}'
    mesh = unreal.load_asset(path) if EAL.does_asset_exist(path) else None
    if mesh and abs(size_ratio(mesh, info) - 1) < .05 and unreal.EditorStaticMeshLibrary.get_convex_collision_count(mesh) > 0:
        return mesh
    mesh = import_rock(name)
    r = size_ratio(mesh, info)
    if abs(r - 1) > .05:
        unreal.log_warning(f'ROCK {name} imported at {r:.3f}x - re-importing at scale {100.0 / r:.3f}')
        mesh = import_rock(name, 100.0 / r)
    return mesh

def foliage_type(mesh, density, scale, sink, collision):
    """FT_<rock>: created once, then updated in place (re-creating a deleted foliage type in one session returns None)."""
    name = 'FT_' + mesh.get_name()[3:]; path = f'{FOLIAGE}/{name}'
    ft = unreal.load_asset(path) if EAL.does_asset_exist(path) else \
        TOOLS.create_asset(name, FOLIAGE, unreal.FoliageType_InstancedStaticMesh, unreal.FoliageType_InstancedStaticMeshFactory())
    fr = unreal.FloatInterval(); fr.min, fr.max = scale
    for k, v in (('mesh', mesh), ('density', density), ('scale_x', fr), ('align_to_normal', True), ('random_yaw', True),
                 ('cast_shadow', collision), ('z_offset', unreal.FloatInterval(-sink, -sink * .5))):
        try: ft.set_editor_property(k, v)
        except Exception as ex: unreal.log_warning(f'{name} {k}: {ex}')
    bi = ft.get_editor_property('body_instance')
    bi.set_editor_property('collision_enabled', unreal.CollisionEnabled.QUERY_AND_PHYSICS if collision else unreal.CollisionEnabled.NO_COLLISION)
    ft.set_editor_property('body_instance', bi)
    EAL.save_loaded_asset(ft)

STONE, MOSSY = stone_material()
for name, info in MANIFEST.items():
    mesh = rock_mesh(name, info)
    mesh.set_material(0, MOSSY if info.get('material') == 'M_Rock_Stone_Moss' else STONE)
    ns = mesh.get_editor_property('nanite_settings'); ns.set_editor_property('enabled', True); mesh.set_editor_property('nanite_settings', ns)
    EAL.save_loaded_asset(mesh)
    size = max(info['dimensions_m'])
    big = size > 0.6                                             # small stones / pebbles: no collision or shadows
    foliage_type(mesh, 0.5 if size > 2.5 else (2 if size > 0.6 else 10), (0.8, 1.25), 0.04 * info['dimensions_m'][2] * 100, big)
    unreal.log(f'ROCK {name} hulls={info.get("collision_hulls")} imported_convex={unreal.EditorStaticMeshLibrary.get_convex_collision_count(mesh)} size={size}')
unreal.log('ROCKS_IMPORTED')
