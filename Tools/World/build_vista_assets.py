"""Creates / updates the stylized-world ASSETS only (meshes, materials, foliage types) - never a map. Every asset is
updated IN PLACE by name (materials are cleared and rebuilt, meshes are re-imported into the existing asset), so maps
that reference them (L_WorldVista, L_AssetReview, ...) pick the change up without being rebuilt.
  UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/build_vista_assets.py <names>" -unattended -RenderOffscreen
<names>: comma/space separated builder names (see BUILDERS at the bottom, e.g. "grass,trees_round"), or "all" (default).
Also read from the environment variable RPG_VISTA_ASSETS. Close the Unreal Editor first (locked files).
Never rebuilt here unless missing: the sky (M_Vista_Sky, MI_Vista_Clouds) and the water - they are frozen (user decision).
Every mesh import is checked: size against the previous asset / the Blender manifest, and collision hull count.
Style target: Dreamscape: Tower video, palette in outputs/World_R01/reference/PALETTE.md."""
import unreal, math, json, os, sys

SRC = 'C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs'
EXP1 = SRC + '/World_R01/exports'                 # R01 world meshes (terrain, far land, ... - layout pieces)
EXP2 = SRC + '/World_R02/exports'                 # restyled world assets (Tools/World/export_vista_assets.py)
TREES = SRC + '/Trees_R02/exports'                # restyled Blender trees (Trees_R02/source/trees_build.py)
ROOT = '/Game/RPG/Environment/World'
MESHES, MATS, FOLIAGE = ROOT + '/Meshes', ROOT + '/Materials', ROOT + '/Foliage'
EAL, MEL = unreal.EditorAssetLibrary, unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
SMES = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
PROBLEMS = []                                     # printed at the end (ASSETS_PROBLEM lines)

def lin(c): return [((v / 255) / 12.92) if v / 255 <= .04045 else ((v / 255 + .055) / 1.055) ** 2.4 for v in c]
def problem(msg): PROBLEMS.append(msg); unreal.log_warning('ASSETS_PROBLEM ' + msg)
def manifest(folder):
    p = folder + '/manifest.json'
    return json.load(open(p)) if os.path.exists(p) else {}

# ------------------------------------------------------------------ meshes (in place + checked)
unreal.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')   # classic importer (Interchange: 100x, no hulls)

def _import(fbx, dest, name, scale, normals):
    t = unreal.AssetImportTask(); t.filename = fbx; t.destination_path = dest; t.destination_name = name
    t.automated = True; t.replace_existing = True; t.replace_existing_settings = True; t.save = True
    o = unreal.FbxImportUI(); o.import_mesh = True; o.import_materials = False; o.import_textures = False; o.import_as_skeletal = False
    o.static_mesh_import_data.set_editor_property('reorder_material_to_fbx_order', True)
    sm = o.static_mesh_import_data; sm.combine_meshes = True; sm.auto_generate_collision = False   # keeps UCX hulls
    sm.vertex_color_import_option = unreal.VertexColorImportOption.REPLACE; sm.generate_lightmap_u_vs = False
    sm.import_uniform_scale = scale
    sm.normal_import_method = unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS if normals else unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS
    t.options = o; TOOLS.import_asset_tasks([t])
    return unreal.load_asset(f'{dest}/{name}')

def extent(mesh):
    b = mesh.get_bounds().box_extent; return max(b.x, b.y, b.z) * 2

def hulls(mesh):
    try: return unreal.EditorStaticMeshLibrary.get_convex_collision_count(mesh)
    except Exception: return SMES.get_convex_collision_count(mesh)

def mesh_asset(name, fbx, dest=MESHES, normals=False, size_cm=None, min_hulls=None, keep_size=True):
    """Imports fbx into dest/name IN PLACE (the existing asset object is kept, so placed copies follow).
    Size check: against size_cm (Blender manifest) or the asset's size before the import (keep_size: restyles keep the
    shape); re-imports at a corrected scale when off by > 5% (an import once came in 100x). Hull check: min_hulls, or at
    least as many convex hulls as before."""
    path = f'{dest}/{name}'
    old = unreal.load_asset(path) if EAL.does_asset_exist(path) else None
    before = extent(old) if old else None
    hulls_before = hulls(old) if old else 0
    if not os.path.exists(fbx):
        problem(f'{name}: missing {fbx}'); return old
    mesh = _import(fbx, dest, name, 1.0, normals)
    want = size_cm or (before if keep_size else None)
    if want:
        r = extent(mesh) / want
        if abs(r - 1) > .05:
            unreal.log_warning(f'MESH {name} imported at {r:.3f}x of {want:.0f} cm - re-importing at scale {1 / r:.4f}')
            mesh = _import(fbx, dest, name, 1.0 / r, normals)
            r = extent(mesh) / want
            if abs(r - 1) > .05: problem(f'{name}: size {extent(mesh):.0f} cm, expected {want:.0f}')
    need = hulls_before if min_hulls is None else min_hulls
    if hulls(mesh) < need: problem(f'{name}: {hulls(mesh)} collision hulls, expected >= {need}')
    unreal.log(f'MESH {name} size={extent(mesh):.0f}cm (before {before}) hulls={hulls(mesh)} (before {hulls_before}) lods={mesh.get_num_lods()}')
    return mesh

def set_lods(mesh, lod_fbx, screen):
    """LOD0 = the mesh; lod_fbx[i] -> LOD i+1 (hand-made in Blender: fewer, bigger cards / simplified forms);
    screen = screen sizes for LOD0.., dithered LOD transitions come from the materials."""
    for i, f in enumerate(lod_fbx):
        if not os.path.exists(f): problem(f'{mesh.get_name()}: missing LOD file {f}'); return
        got = SMES.import_lod(mesh, i + 1, f)
        if got != i + 1: problem(f'{mesh.get_name()}: LOD{i + 1} import returned {got}')
    while mesh.get_num_lods() > len(lod_fbx) + 1:                     # drop stale extra LODs
        SMES.remove_lods(mesh); break
    try:
        mesh.set_editor_property('auto_compute_lod_screen_size', False)
    except Exception: pass
    try:
        SMES.set_lod_screen_sizes(mesh, screen[:mesh.get_num_lods()])
    except Exception as ex:
        problem(f'{mesh.get_name()}: LOD screen sizes not set ({ex})')
    EAL.save_loaded_asset(mesh)

def assign(mesh, by_slot):
    """materials by FBX slot name (Bark/Leaves/Fill/Rock), falling back to slot order."""
    for i, sm in enumerate(mesh.static_materials):
        nm = str(sm.material_slot_name)
        mat = next((v for k, v in by_slot.items() if k.lower() in nm.lower()), list(by_slot.values())[min(i, len(by_slot) - 1)])
        mesh.set_material(i, mat)
    EAL.save_loaded_asset(mesh)

def nanite(mesh, on=True):
    ns = mesh.get_editor_property('nanite_settings'); ns.set_editor_property('enabled', on)
    mesh.set_editor_property('nanite_settings', ns); EAL.save_loaded_asset(mesh)

def variant(src, name, mat):
    """a mesh asset of its own (same geometry, own material), updated in place: e.g. daisies from the flower mesh."""
    path = f'{MESHES}/{name}'
    if EAL.does_asset_exist(path):
        m = unreal.load_asset(path)
        ok = SMES.set_lod_from_static_mesh(m, 0, src, 0, True) if hasattr(SMES, 'set_lod_from_static_mesh') else -1
        if ok < 0: problem(f'{name}: could not copy geometry from {src.get_name()}')
    else:
        m = EAL.duplicate_asset(src.get_path_name().split('.')[0], path)
    m.set_material(0, mat); EAL.save_loaded_asset(m); return m

# ------------------------------------------------------------------ materials (in place)
def new_material(name):
    """existing material cleared and rebuilt (same object - meshes and maps keep pointing at it), or a new one."""
    path = f'{MATS}/{name}'
    if EAL.does_asset_exist(path):
        m = unreal.load_asset(path); MEL.delete_all_material_expressions(m)
        for k, v in (('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT), ('blend_mode', unreal.BlendMode.BLEND_OPAQUE),
                     ('two_sided', False), ('dithered_lod_transition', False)):
            m.set_editor_property(k, v)
        return m
    return TOOLS.create_asset(name, MATS, unreal.Material, unreal.MaterialFactoryNew())
def node(mat, cls, x, y, **p):
    e = MEL.create_material_expression(mat, cls, x, y)
    for k, v in p.items(): e.set_editor_property(k, v)
    return e
def link(a, ao, b, bi): MEL.connect_material_expressions(a, ao, b, bi)
def scalar(m, n, v, x, y): return node(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=n, default_value=v)
def vector(m, n, rgb, x, y): return node(m, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=n, default_value=unreal.LinearColor(*rgb, 1.0))
F1, F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT1, unreal.CustomMaterialOutputType.CMOT_FLOAT3
def custom(m, code, ins, out, extra=(), x=0, y=0):
    c = node(m, unreal.MaterialExpressionCustom, x, y, code=code, output_type=out)
    lst = []
    for n_ in ins:
        ci = unreal.CustomInput(); ci.set_editor_property('input_name', n_); lst.append(ci)
    c.set_editor_property('inputs', lst)
    eo = []
    for n_, t_ in extra:
        co = unreal.CustomOutput(); co.set_editor_property('output_name', n_); co.set_editor_property('output_type', t_); eo.append(co)
    c.set_editor_property('additional_outputs', eo)
    return c
def feed(m, c, inputs, x=-900):
    """inputs: name -> expression | ('scalar', v) | ('vector', rgb) | ('node', cls) | ('night',) = 0 day .. 1 night"""
    y = -600
    for n_, v in inputs.items():
        if isinstance(v, tuple) and v[0] == 'scalar': e = scalar(m, n_, v[1], x, y)
        elif isinstance(v, tuple) and v[0] == 'vector': e = vector(m, n_, v[1], x, y)
        elif isinstance(v, tuple) and v[0] == 'node': e = node(m, v[1], x, y)
        elif isinstance(v, tuple) and v[0] == 'night': e = night_node(m, x, y)
        else: e = v
        out = ''
        if isinstance(v, tuple) and len(v) > 2: out = v[2]
        link(e, out, c, n_); y += 90
def finish(m, dither=False):
    # scattered meshes are instanced (ARPGScatter / foliage); without this usage flag they render with the default grey material
    m.set_editor_property('used_with_instanced_static_meshes', True)
    m.set_editor_property('used_with_nanite', True)                    # terrain / far hills / rocks are Nanite
    if dither: m.set_editor_property('dithered_lod_transition', True)  # smooth LOD switches on trees / foliage
    MEL.recompile_material(m); EAL.save_loaded_asset(m); return m

def cpd(m, name, index, x, y, default=0.0):
    """scalar read from the component's custom primitive data (written by ARPGSkyCycle for its SkyReceivers)."""
    e = scalar(m, name, default, x, y)
    e.set_editor_property('use_custom_primitive_data', True); e.set_editor_property('primitive_data_index', index)
    return e
def night_node(m, x, y): return cpd(m, 'NightAmount', 6, x, y)    # 0 = day .. 1 = night (ARPGSkyCycle custom primitive data 6)

def instance(name, parent, vectors=None, scalars=None):
    path = f'{MATS}/{name}'
    if EAL.does_asset_exist(path):
        mi = unreal.load_asset(path); MEL.clear_all_material_instance_parameters(mi)
    else:
        mi = TOOLS.create_asset(name, MATS, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, parent)
    for k, rgb in (vectors or {}).items(): MEL.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(*rgb, 1))
    for k, v in (scalars or {}).items(): MEL.set_material_instance_scalar_parameter_value(mi, k, v)
    EAL.save_loaded_asset(mi); return mi

TEXTURES = '/Game/RPG/Environment/Textures'
def tex(name):
    p = f'{TEXTURES}/{name}'
    return unreal.load_asset(p) if EAL.does_asset_exist(p) else None

HELP = r'''
struct WF {
  float h(float2 p){ return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
  float n(float2 p){ float2 i = floor(p), f = frac(p); float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(h(i), h(i + float2(1, 0)), u.x), lerp(h(i + float2(0, 1)), h(i + float2(1, 1)), u.x), u.y); }
  float fbm(float2 p){ return 0.55 * n(p) + 0.30 * n(p * 2.03 + 7.1) + 0.15 * n(p * 4.1 + 3.3); }
  float h3(float3 p){ return frac(sin(dot(p, float3(127.1, 311.7, 74.7))) * 43758.5453); }
  float n3(float3 p){ float3 i = floor(p), f = frac(p); float3 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(lerp(h3(i), h3(i + float3(1, 0, 0)), u.x), lerp(h3(i + float3(0, 1, 0)), h3(i + float3(1, 1, 0)), u.x), u.y),
                lerp(lerp(h3(i + float3(0, 0, 1)), h3(i + float3(1, 0, 1)), u.x), lerp(h3(i + float3(0, 1, 1)), h3(i + float3(1, 1, 1)), u.x), u.y), u.z); }
  float fbm3(float3 p){ return 0.5 * n3(p) + 0.28 * n3(p * 2.1 + 5.2) + 0.14 * n3(p * 4.3 + 1.7) + 0.08 * n3(p * 8.7 + 9.1); }
};
WF w;
'''
# wind shared by grass / flowers / wheat (H = 0 at the root .. 1 at the tip, from vertex colour R)
WIND = HELP + r'''
float ph = T * WindSpeed + dot(WP.xy, float2(0.0045, 0.0031));
float gust = w.fbm(WP.xy / 1800.0 + float2(T * 0.09, T * 0.05));
float2 dir = normalize(float2(1.0, 0.35));
float a = WindStrength * (0.35 + gust) * H * H;
return float3(dir * (sin(ph) * 0.6 + 0.55) * a, -a * 0.2);
'''
# tree sway (whole tree, by height above its base) + leaf flutter (Flut = per-vertex flutter weight, 0 on bark)
SWAY = HELP + r'''
float hh = saturate((WP.z - OP.z) / Height);
float ph = T * 0.7 + dot(OP.xy, float2(0.001, 0.0013));
float gust = 0.6 + 0.4 * w.n(OP.xy / 3000.0 + T * 0.05);
float3 o = float3(normalize(float2(1.0, 0.35)) * (sin(ph) + 0.35 * sin(ph * 2.3 + 1.0)) * SwayAmp * gust * hh * hh, 0);
o += Flutter * Flut * float3(sin(T * 2.7 + dot(WP, float3(0.031, 0.027, 0.02))), cos(T * 2.3 + dot(WP, float3(0.022, 0.035, 0.018))), sin(T * 3.1 + WP.x * 0.03) * 0.5) * (0.4 + 0.6 * hh);
return o;
'''
DRIFT = r'''
float3 L = WP - OP;
float3 sz = Box * 2.0;
float sp = Speed * (0.5 + Seed.g);
float2 c = Ctr + float2(T * sp, T * sp * 0.35);
c = frac((c + Box.xy) / sz.xy) * sz.xy - Box.xy;
float3 q = float3(c + (L.xy - Ctr), L.z);
float ph = Seed.r * 6.283;
q.z = L.z + sin(T * (0.6 + Seed.b) + ph) * Bob + sin(T * 0.23 + ph * 2.0) * Bob * 2.0;
q.x += sin(T * 1.3 + ph * 3.0) * Bob;
return q - L;
'''

def wind_wpo(m, strength, speed=1.6):
    c = custom(m, WIND, ['WP', 'T', 'H', 'WindStrength', 'WindSpeed'], F3, (), -400, 500)
    vc = node(m, unreal.MaterialExpressionVertexColor, -900, 500)
    feed(m, c, {'WP': ('node', unreal.MaterialExpressionWorldPosition), 'T': ('node', unreal.MaterialExpressionTime),
                'WindStrength': ('scalar', strength), 'WindSpeed': ('scalar', speed)})
    link(vc, 'R', c, 'H')
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

def sway_wpo(m, flutter, amp=18.0, height=1500.0, flut=None):
    """flut: expression giving the per-vertex flutter weight (None = 1 everywhere)."""
    c = custom(m, SWAY, ['WP', 'OP', 'T', 'SwayAmp', 'Flutter', 'Flut', 'Height'], F3, (), -400, 600)
    feed(m, c, {'WP': ('node', unreal.MaterialExpressionWorldPosition), 'OP': ('node', unreal.MaterialExpressionObjectPositionWS),
                'T': ('node', unreal.MaterialExpressionTime), 'SwayAmp': ('scalar', amp), 'Flutter': ('scalar', flutter),
                'Height': ('scalar', height)})
    link(flut if flut is not None else node(m, unreal.MaterialExpressionConstant, -900, 900, r=1.0), '', c, 'Flut')
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

def drift_wpo(m, speed, bob, box=(1500.0, 1500.0, 500.0)):
    c = custom(m, DRIFT, ['WP', 'OP', 'T', 'Seed', 'Ctr', 'Box', 'Speed', 'Bob'], F3, (), -400, 600)
    link(node(m, unreal.MaterialExpressionTextureCoordinate, -900, 800, coordinate_index=1), '', c, 'Ctr')
    vc = node(m, unreal.MaterialExpressionVertexColor, -900, 700)
    feed(m, c, {'WP': ('node', unreal.MaterialExpressionWorldPosition), 'OP': ('node', unreal.MaterialExpressionObjectPositionWS),
                'T': ('node', unreal.MaterialExpressionTime), 'Box': ('vector', box), 'Speed': ('scalar', speed), 'Bob': ('scalar', bob)})
    link(vc, '', c, 'Seed')
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

# ------------------------------------------------------------------ foliage types (in place)
def foliage_type(mesh, density, scale, align=False, shadow=True, cull=0, collision=False, sink=0.0, up_deg=45.0, folder=FOLIAGE):
    """FT_<mesh>: paintable with the Foliage tool. Created once, then updated in place (re-creating a deleted foliage type
    in one session returns None). cull = end cull distance (cm); small things get small values."""
    name = 'FT_' + mesh.get_name()[3:]; path = f'{folder}/{name}'
    try:
        ft = unreal.load_asset(path) if EAL.does_asset_exist(path) else \
            TOOLS.create_asset(name, folder, unreal.FoliageType_InstancedStaticMesh, unreal.FoliageType_InstancedStaticMeshFactory())
    except Exception as ex:
        problem(f'foliage type {name}: {ex}'); return None
    fr = unreal.FloatInterval(); fr.min, fr.max = scale
    for k, v in (('mesh', mesh), ('density', density), ('scale_x', fr), ('align_to_normal', align), ('cast_shadow', shadow),
                 ('random_yaw', True), ('ground_slope_angle', unreal.FloatInterval(0.0, up_deg))):
        try: ft.set_editor_property(k, v)
        except Exception as ex: unreal.log_warning(f'foliage type {name} {k}: {ex}')
    if cull:
        ci = unreal.Int32Interval(); ci.min, ci.max = int(cull * .75), int(cull)
        try: ft.set_editor_property('cull_distance', ci)
        except Exception as ex: unreal.log_warning(f'foliage type {name} cull: {ex}')
    try:
        bi = ft.get_editor_property('body_instance'); bi.set_editor_property('collision_enabled', unreal.CollisionEnabled.QUERY_AND_PHYSICS if collision else unreal.CollisionEnabled.NO_COLLISION)
        ft.set_editor_property('body_instance', bi)
    except Exception as ex: unreal.log_warning(f'foliage type {name} collision: {ex}')
    if sink:
        try: ft.set_editor_property('z_offset', unreal.FloatInterval(-sink, -sink * .5))
        except Exception as ex: unreal.log_warning(f'foliage type {name} sink: {ex}')
    EAL.save_loaded_asset(ft); unreal.log(f'FOLIAGE_TYPE {name}'); return ft

def load(path): return unreal.load_asset(path) if EAL.does_asset_exist(path) else None
def M(name): return load(f'{MATS}/{name}')
def SM(name): return load(f'{MESHES}/{name}')
def pick(*paths):
    """first existing file (R02 restyle before R01 original)."""
    return next((p for p in paths if os.path.exists(p)), paths[-1])

# ================================================================== materials
def m_terrain():
    m = new_material('M_Vista_Terrain')
    code = HELP + r'''
float flat = saturate((N.z - 0.66) / 0.14);                     // 1 = grassy flat, 0 = steep rock
float n1 = w.fbm(WP.xy / 1100.0), n2 = w.fbm(WP.xy / 170.0 + 7.0);
float3 grass = lerp(GrassDark, lerp(GrassC, GrassLight, smoothstep(0.35, 0.75, n1)), smoothstep(0.15, 0.6, n2 * 0.6 + n1 * 0.4));
float3 dirt = DirtC * (0.82 + 0.35 * (n2 - 0.5)) * (0.9 + 0.2 * w.n(WP.xy / 25.0));
float3 rock = RockC * (0.78 + 0.45 * dot(Rock, float3(0.33, 0.33, 0.33)) / 0.5);
float3 bed = BedC * (0.75 + 0.5 * w.n(WP.xy / 18.0)) * (0.85 + 0.3 * n2);   // river bed / pond shore: wet earth + pebbles
float3 c = lerp(rock, grass, flat);
c = lerp(c, dirt, smoothstep(0.25, 0.75, Path + (n2 - 0.5) * 0.35) * flat);
c = lerp(c, bed, smoothstep(0.2, 0.7, Wet + (n2 - 0.5) * 0.3));
return c;
'''
    c = custom(m, code, ['WP', 'N', 'Path', 'Wet', 'Rock', 'GrassC', 'GrassLight', 'GrassDark', 'DirtC', 'RockC', 'BedC'], F3, (), -300, 0)
    wp = node(m, unreal.MaterialExpressionWorldPosition, -1300, 0)
    mk = node(m, unreal.MaterialExpressionComponentMask, -1150, 0, r=True, g=True, b=False, a=False); link(wp, '', mk, '')
    dv = node(m, unreal.MaterialExpressionDivide, -1050, 0, const_b=400.0); link(mk, '', dv, 'A')
    rt = node(m, unreal.MaterialExpressionTextureSample, -900, 0, texture=tex('T_Stone_Painted_BaseColor')); link(dv, '', rt, 'UVs')
    vc = node(m, unreal.MaterialExpressionVertexColor, -900, 200)
    feed(m, c, {'WP': wp, 'N': ('node', unreal.MaterialExpressionVertexNormalWS), 'GrassC': ('vector', lin((110, 175, 40))),
                'GrassLight': ('vector', lin((175, 205, 55))), 'GrassDark': ('vector', lin((60, 115, 35))),
                'DirtC': ('vector', lin((176, 128, 78))), 'RockC': ('vector', lin((104, 106, 114))), 'BedC': ('vector', lin((118, 96, 64)))}, x=-1300)
    link(vc, 'R', c, 'Path'); link(vc, 'G', c, 'Wet'); link(rt, 'RGB', c, 'Rock')
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(node(m, unreal.MaterialExpressionConstant, -300, 200, r=0.92), '', unreal.MaterialProperty.MP_ROUGHNESS)
    return finish(m)

def soft_normal(m, up):
    """world-space shading normal = the mesh's (bent) vertex normal pulled toward +Z by `up`, the SAME on both faces of the
    two-sided cards (tangent-space normals flip on back faces -> dark patches). Gives soft painterly foliage."""
    m.set_editor_property('tangent_space_normal', False)
    c = custom(m, 'return normalize(lerp(N, float3(0, 0, 1), Up));', ['N', 'Up'], F3, (), -300, 420)
    feed(m, c, {'N': ('node', unreal.MaterialExpressionVertexNormalWS), 'Up': ('scalar', up)}, x=-700)
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_NORMAL)

def m_grass(name, root, tip, strength, sss, ear=None):
    """R02 grass / tall grass / wheat: root->tip ramp (video: deep green roots, saturated yellow-green tips, no blue),
    big soft colour patches (yellower drifts, deeper green dips), backlit glow, up-facing soft normals, wind.
    ear: wheat seed-head colour (vertex G = 1)."""
    m = new_material(name)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE); m.set_editor_property('two_sided', True)
    code = HELP + r'''
float patch = w.fbm(WP.xy / 1100.0), small = w.fbm(WP.xy / 260.0 + 3.0);
float3 tipc = lerp(TipC * float3(0.82, 0.92, 0.9), TipC * float3(1.1, 1.0, 0.6) + float3(0.04, 0.02, 0.0), smoothstep(0.35, 0.75, patch));
float3 c = lerp(RootC, tipc, smoothstep(0.0, 1.0, pow(saturate(H), 0.75))) * lerp(0.86, 1.1, small);
c = lerp(c, EarC * lerp(0.85, 1.1, small), saturate(Ear));
Sss = c * SssGain * (0.3 + 0.7 * H);
return c;
'''
    c = custom(m, code, ['WP', 'H', 'Ear', 'RootC', 'TipC', 'EarC', 'SssGain'], F3, (('Sss', F3),), -300, 0)
    vc = node(m, unreal.MaterialExpressionVertexColor, -900, 300)
    feed(m, c, {'WP': ('node', unreal.MaterialExpressionWorldPosition), 'RootC': ('vector', lin(root)), 'TipC': ('vector', lin(tip)),
                'EarC': ('vector', lin(ear or tip)), 'SssGain': ('scalar', sss)})
    link(vc, 'R', c, 'H')
    if ear: link(vc, 'G', c, 'Ear')
    else: link(node(m, unreal.MaterialExpressionConstant, -900, 400, r=0.0), '', c, 'Ear')
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(c, 'Sss', unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    MEL.connect_material_property(node(m, unreal.MaterialExpressionConstant, -300, 250, r=0.8), '', unreal.MaterialProperty.MP_ROUGHNESS)
    soft_normal(m, 0.65)
    wind_wpo(m, strength)
    return finish(m, dither=True)

def m_flowers():
    """R02 poppies (+ MI_Vista_Daisies): vertex G = kind (0 stem/leaf, .5 centre, 1 petal), B = petal shade (0 base .. 1 rim)."""
    m = new_material('M_Vista_Poppies')
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE); m.set_editor_property('two_sided', True)
    code = r'''
float3 stem = lerp(StemC * 0.6, StemC, saturate(H));
float3 petal = lerp(Petal * PetalBase, Petal, smoothstep(0.0, 0.8, Shade));
float3 c = Kind > 0.75 ? petal : (Kind > 0.25 ? Centre : stem);
Sss = c * (Kind > 0.75 ? 0.9 : 0.5);
return c;
'''
    c = custom(m, code, ['Kind', 'Shade', 'H', 'Petal', 'PetalBase', 'Centre', 'StemC'], F3, (('Sss', F3),), -300, 0)
    vc = node(m, unreal.MaterialExpressionVertexColor, -900, 0); link(vc, 'G', c, 'Kind'); link(vc, 'B', c, 'Shade'); link(vc, 'R', c, 'H')
    feed(m, c, {'Petal': ('vector', lin((232, 52, 24))), 'PetalBase': ('vector', (0.55, 0.35, 0.35)), 'Centre': ('vector', (0.02, 0.015, 0.012)),
                'StemC': ('vector', lin((70, 125, 20)))})
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(c, 'Sss', unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    MEL.connect_material_property(node(m, unreal.MaterialExpressionConstant, -300, 250, r=0.7), '', unreal.MaterialProperty.MP_ROUGHNESS)
    soft_normal(m, 0.4)
    wind_wpo(m, 9.0)
    return finish(m, dither=True)

def m_canopy():
    """Fill blobs of the round trees (+ far trees): vertex G = light (inner dark .. outer sunlit), B = tint."""
    m = new_material('M_Vista_Canopy')
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE); m.set_editor_property('two_sided', True)
    code = HELP + r'''
float3 c = lerp(Dark, lerp(Mid, Light, smoothstep(0.45, 0.95, G)), smoothstep(0.05, 0.6, G));
c *= lerp(float3(0.92, 1.04, 0.9), float3(1.08, 0.98, 0.82), B);             // per-clump hue shift
c *= 0.9 + 0.2 * w.n(WP.xy / 90.0 + WP.z / 70.0);
Sss = c * float3(0.95, 1.0, 0.5) * SssGain * G;
return c;
'''
    c = custom(m, code, ['WP', 'G', 'B', 'Dark', 'Mid', 'Light', 'SssGain'], F3, (('Sss', F3),), -300, 0)
    vc = node(m, unreal.MaterialExpressionVertexColor, -900, 200); link(vc, 'G', c, 'G'); link(vc, 'B', c, 'B')
    feed(m, c, {'WP': ('node', unreal.MaterialExpressionWorldPosition), 'Dark': ('vector', lin((26, 58, 36))),     # video palette (R02)
                'Mid': ('vector', lin((70, 120, 30))), 'Light': ('vector', lin((160, 196, 28))), 'SssGain': ('scalar', 0.9)})
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(c, 'Sss', unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    MEL.connect_material_property(node(m, unreal.MaterialExpressionConstant, -300, 250, r=0.7), '', unreal.MaterialProperty.MP_ROUGHNESS)
    soft_normal(m, 0.1)
    sway_wpo(m, 2.5)
    return finish(m, dither=True)

def m_leaf():
    m = new_material('M_Vista_Leaf')
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE); m.set_editor_property('two_sided', True)
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    # R02: leaf-bunch atlas (Trees_R02: ~30 small leaves per card) x vertex colour (video ramp shade -> lit), gentle
    # per-tree hue drift, backlit glow = warm yellow-green transmission (two-sided foliage subsurface)
    code = HELP + r'''
float3 c = Tex * VC * Gain;
float l = dot(c, float3(0.3, 0.59, 0.11));
c = max(lerp(float3(l, l, l), c, Sat), 0.0);
c *= lerp(float3(0.96, 1.02, 0.94), float3(1.05, 1.0, 0.86), w.n(OP.xy / 900.0));     // tree-to-tree hue drift
Sss = lerp(c, GlowC, 0.45) * SssGain * saturate(l * 3.0 + 0.2);
return c;
'''
    c = custom(m, code, ['Tex', 'VC', 'OP', 'Gain', 'Sat', 'SssGain', 'GlowC'], F3, (('Sss', F3),), -300, 0)
    ts = node(m, unreal.MaterialExpressionTextureSample, -900, 0, texture=tex('T_Leaf_Cluster_Atlas_BaseColor') or tex('T_Leaf_Maple_Atlas_BaseColor'))
    vc = node(m, unreal.MaterialExpressionVertexColor, -900, 200)
    link(ts, 'RGB', c, 'Tex'); link(vc, '', c, 'VC')
    feed(m, c, {'OP': ('node', unreal.MaterialExpressionObjectPositionWS), 'Gain': ('scalar', 1.35), 'Sat': ('scalar', 1.25),
                'SssGain': ('scalar', 0.9), 'GlowC': ('vector', lin((190, 210, 40)))}, x=-900)
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(c, 'Sss', unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    MEL.connect_material_property(ts, 'A', unreal.MaterialProperty.MP_OPACITY_MASK)
    MEL.connect_material_property(node(m, unreal.MaterialExpressionConstant, -300, 250, r=0.8), '', unreal.MaterialProperty.MP_ROUGHNESS)
    soft_normal(m, 0.15)
    sway_wpo(m, 3.0)
    return finish(m, dither=True)

def m_bark():
    """R02: smooth red-brown painted bark (Trees_R02), moss from vertex colour R (Blender trees) + a mossy, darker
    collar where roots meet the ground (all trees), long vertical tiling."""
    m = new_material('M_Vista_Bark')
    code = HELP + r'''
float hb = WP.z - OP.z;                                            // height above the tree's origin (cm)
float mn = w.fbm(float2(WP.x + WP.y, WP.z) / 90.0);
float collar = saturate(1.0 - hb / CollarH) * smoothstep(0.3, 0.6, mn + 0.15);
float moss = saturate(max(VC.r, collar * 0.75));
float3 c = lerp(Bark * Tint, Moss * MossTint, moss);
c *= lerp(0.72, 1.0, saturate((hb + 30.0) / 60.0));             // roots darken into the soil
return c;
'''
    c = custom(m, code, ['WP', 'OP', 'VC', 'Bark', 'Moss', 'Tint', 'MossTint', 'CollarH'], F3, (), -300, 0)
    ts = node(m, unreal.MaterialExpressionTextureSample, -900, 0, texture=tex('T_Bark_Painted_R02_BaseColor') or tex('T_Bark_Painted_BaseColor'))
    tm = node(m, unreal.MaterialExpressionTextureSample, -900, 200, texture=tex('T_Moss_Painted_BaseColor'))
    link(ts, 'RGB', c, 'Bark'); link(tm, 'RGB', c, 'Moss')
    link(node(m, unreal.MaterialExpressionVertexColor, -900, 400), '', c, 'VC')
    feed(m, c, {'WP': ('node', unreal.MaterialExpressionWorldPosition), 'OP': ('node', unreal.MaterialExpressionObjectPositionWS),
                'Tint': ('vector', (1.0, 0.92, 0.86)), 'MossTint': ('vector', (1.0, 1.0, 0.85)), 'CollarH': ('scalar', 90.0)}, x=-1200)
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(node(m, unreal.MaterialExpressionConstant, -300, 250, r=0.85), '', unreal.MaterialProperty.MP_ROUGHNESS)
    sway_wpo(m, 0.0)
    return finish(m, dither=True)

def m_rock():
    m = new_material('M_Vista_Rock')
    code = HELP + r'''
float3 an = abs(N);
float3 t = (TexXY * an.z + TexXZ * an.y + TexYZ * an.x) / max(an.x + an.y + an.z, 0.001);
float3 c = RockC * (0.72 + 0.55 * dot(t, float3(0.33, 0.33, 0.33)) / 0.5);
c *= 0.88 + 0.24 * w.n(float2(WP.z / 60.0, (WP.x + WP.y) / 400.0));             // faint horizontal strata
float moss = smoothstep(0.45, 0.75, N.z + (w.fbm(WP.xy / 140.0) - 0.5) * 0.5);
return lerp(c, MossC * (0.85 + 0.3 * w.n(WP.xy / 40.0)), moss);
'''
    c = custom(m, code, ['WP', 'N', 'TexXY', 'TexXZ', 'TexYZ', 'RockC', 'MossC'], F3, (), -300, 0)
    wp = node(m, unreal.MaterialExpressionWorldPosition, -1500, 0)
    for i, (r_, g_, b_, nm) in enumerate(((True, True, False, 'TexXY'), (True, False, True, 'TexXZ'), (False, True, True, 'TexYZ'))):
        mk = node(m, unreal.MaterialExpressionComponentMask, -1300, i * 160, r=r_, g=g_, b=b_, a=False); link(wp, '', mk, '')
        dv = node(m, unreal.MaterialExpressionDivide, -1150, i * 160, const_b=350.0); link(mk, '', dv, 'A')
        ts = node(m, unreal.MaterialExpressionTextureSample, -1000, i * 160, texture=tex('T_Stone_Painted_BaseColor')); link(dv, '', ts, 'UVs'); link(ts, 'RGB', c, nm)
    feed(m, c, {'WP': wp, 'N': ('node', unreal.MaterialExpressionPixelNormalWS), 'RockC': ('vector', lin((124, 114, 100))),
                'MossC': ('vector', lin((105, 160, 42)))}, x=-1300)
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(node(m, unreal.MaterialExpressionConstant, -300, 250, r=0.88), '', unreal.MaterialProperty.MP_ROUGHNESS)
    return finish(m)

def m_motes():
    """Pollen in daylight, fireflies at night: tiny unlit warm cards, brighter as night falls."""
    m = new_material('M_Vista_Motes')
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT); m.set_editor_property('two_sided', True)
    c = custom(m, 'return Col * (DayGlow + Night * NightGlow) * (0.6 + 0.8 * Seed.b);', ['Col', 'Night', 'DayGlow', 'NightGlow', 'Seed'], F3, (), -300, 0)
    vc = node(m, unreal.MaterialExpressionVertexColor, -900, 300); link(vc, '', c, 'Seed')
    feed(m, c, {'Col': ('vector', lin((255, 214, 120))), 'Night': ('night',), 'DayGlow': ('scalar', 0.45), 'NightGlow': ('scalar', 6.0)})
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    drift_wpo(m, 22.0, 35.0)
    return finish(m)

def m_drift_leaf():
    m = new_material('M_Vista_DriftLeaf')
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE); m.set_editor_property('two_sided', True)
    c = custom(m, 'float3 c = Seed.r > 0.7 ? A : (Seed.r > 0.3 ? B : C); Sss = c * 0.8; return c;', ['Seed', 'A', 'B', 'C'], F3, (('Sss', F3),), -300, 0)
    vc = node(m, unreal.MaterialExpressionVertexColor, -900, 300); link(vc, '', c, 'Seed')
    feed(m, c, {'A': ('vector', lin((230, 150, 40))), 'B': ('vector', lin((170, 205, 60))), 'C': ('vector', lin((95, 160, 45)))})
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(c, 'Sss', unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    drift_wpo(m, 110.0, 90.0, (1500.0, 1500.0, 700.0))
    return finish(m)

def m_mist():
    """Low translucent mist sheet: drifting noise, soft where it meets the shore (depth fade), round falloff (UV)."""
    m = new_material('M_Vista_Mist')
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT); m.set_editor_property('two_sided', True)
    code = HELP + r'''
float r = length(UV - 0.5) * 2.0;
float n = w.fbm(WP.xy / 900.0 + float2(T * 0.012, T * 0.007)) * w.fbm(WP.xy / 350.0 - float2(T * 0.02, 0.0) + 4.0);
Op = saturate(smoothstep(0.08, 0.35, n) * (1.0 - smoothstep(0.55, 1.0, r)) * Fade * Strength * (0.7 + 0.6 * Night));
return lerp(DayC, NightC, Night);
'''
    c = custom(m, code, ['UV', 'WP', 'T', 'Fade', 'Night', 'Strength', 'DayC', 'NightC'], F3, (('Op', F1),), -300, 0)
    df = node(m, unreal.MaterialExpressionDepthFade, -700, 300); df.set_editor_property('fade_distance_default', 120.0)
    feed(m, c, {'UV': ('node', unreal.MaterialExpressionTextureCoordinate), 'WP': ('node', unreal.MaterialExpressionWorldPosition),
                'T': ('node', unreal.MaterialExpressionTime), 'Night': ('night',), 'Strength': ('scalar', 0.2),
                'DayC': ('vector', lin((255, 236, 214))), 'NightC': ('vector', (0.05, 0.07, 0.12))})
    link(df, '', c, 'Fade')
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(c, 'Op', unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(m); EAL.save_loaded_asset(m); return m

# ================================================================== builders (one group of assets each)
def b_materials():
    """all world materials (in place)."""
    m_terrain()
    # R02 palette (World_R01/reference/PALETTE.md): lit meadow tips ~167,173,3, shade 108,138,4 - albedo slightly above
    m_grass('M_Vista_Grass', (38, 84, 10), (184, 198, 22), 12.0, 0.8)
    m_grass('M_Vista_GrassTall', (40, 86, 12), (172, 192, 26), 18.0, 0.8)
    m_grass('M_Vista_Wheat', (150, 112, 30), (232, 180, 36), 16.0, 0.9, ear=(246, 196, 58))
    instance('MI_Vista_Daisies', m_flowers(), {'Petal': lin((250, 246, 232)), 'PetalBase': (0.8, 0.8, 0.75), 'Centre': lin((240, 176, 24))})
    m_canopy(); m_leaf(); m_bark(); m_rock(); m_motes(); m_drift_leaf(); m_mist()

def b_ground():
    """terrain + far land meshes: layout pieces, normally NOT re-imported (their shape is the world layout).
    Only material + Nanite + collision settings are refreshed."""
    for n in ('SM_Vista_Terrain', 'SM_Vista_Far'):
        mesh = SM(n) or mesh_asset(n, f'{EXP1}/{n}.fbx', keep_size=False)
        mesh.set_material(0, M('M_Vista_Terrain'))
        mesh.get_editor_property('body_setup').set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        nanite(mesh)

WORLD2 = manifest(EXP2)                     # World_R02/exports/manifest.json: Blender dims (m) + triangles per LOD
def world_asset(name, slots, screens, normals=False, keep_size=False, min_hulls=0):
    """restyled world mesh (World_R02/exports, Tools/World/export_vista_assets.py) + its LODs; size checked against the
    Blender manifest (new assets) or the previous asset (restyles that must keep their size: keep_size)."""
    info = WORLD2.get(name)
    fbx = f'{EXP2}/{name}.fbx'
    if not os.path.exists(fbx): fbx = f'{EXP1}/{name}.fbx'; info = None
    mesh = mesh_asset(name, fbx, normals=normals, min_hulls=min_hulls, keep_size=keep_size,
                      size_cm=None if keep_size or not info else max(info['dims']) * 100)
    if not mesh: return None
    lods = [f'{EXP2}/{name}_LOD{i}.fbx' for i in range(1, 4) if os.path.exists(f'{EXP2}/{name}_LOD{i}.fbx')]
    if lods and info: set_lods(mesh, lods, list(screens))
    if isinstance(slots, dict): assign(mesh, slots)
    else: mesh.set_material(0, slots); EAL.save_loaded_asset(mesh)
    return mesh

# LOD screen sizes: small foliage drops detail early (most of the 60k grass clumps draw LOD1/LOD2)
GRASS_LODS, FLOWER_LODS, TREE_LODS, BUSH_LODS = (1.0, 0.22, 0.07), (1.0, 0.12), (1.0, 0.4, 0.13), (1.0, 0.3, 0.1)

def b_grass():
    for n, mat, dens, sc, cull in (('SM_Vista_Grass', 'M_Vista_Grass', 400, (0.8, 1.3), 9000),
                                   ('SM_Vista_GrassTall', 'M_Vista_GrassTall', 120, (0.8, 1.25), 9000),
                                   ('SM_Vista_Tuft', 'M_Vista_GrassTall', 60, (0.8, 1.2), 6000)):
        mesh = world_asset(n, M(mat), GRASS_LODS)
        if mesh: foliage_type(mesh, dens, sc, shadow=False, cull=cull)
    # wheat: its own mesh (stalks + seed ears) since R02; before that a copy of the tall grass
    wheat = world_asset('SM_Vista_Wheat', M('M_Vista_Wheat'), (1.0, 0.18)) if 'SM_Vista_Wheat' in WORLD2 else \
        variant(SM('SM_Vista_GrassTall'), 'SM_Vista_Wheat', M('M_Vista_Wheat'))
    foliage_type(wheat, 300, (0.9, 1.3), shadow=False, cull=9000)

def b_flowers():
    mesh = world_asset('SM_Vista_Poppies', M('M_Vista_Poppies'), FLOWER_LODS)
    foliage_type(mesh, 60, (1.1, 1.5), shadow=False, cull=7000)
    daisies = world_asset('SM_Vista_Daisies', M('MI_Vista_Daisies'), FLOWER_LODS) if 'SM_Vista_Daisies' in WORLD2 else \
        variant(mesh, 'SM_Vista_Daisies', M('MI_Vista_Daisies'))
    foliage_type(daisies, 40, (0.9, 1.2), shadow=False, cull=6000)

def b_trees_round():
    """round world trees: same trunk / branches / domes as R01 (shape and size kept), R02 canopy + LODs."""
    for n, dens in (('SM_Vista_Tree_Round', 0.6), ('SM_Vista_Tree_Tall', 0.6), ('SM_Vista_Tree_Wide', 0.5)):
        mesh = world_asset(n, {'Bark': M('M_Vista_Bark'), 'Leaves': M('M_Vista_Leaf'), 'Fill': M('M_Vista_Canopy')}, TREE_LODS,
                           normals=True, keep_size=True, min_hulls=1)
        foliage_type(mesh, dens, (0.85, 1.3), collision=True, up_deg=30)
    far = SM('SM_Vista_Tree_Far') or mesh_asset('SM_Vista_Tree_Far', f'{EXP1}/SM_Vista_Tree_Far.fbx')   # shape unchanged; colours = M_Vista_Canopy
    assign(far, {'Bark': M('M_Vista_Bark'), 'Leaves': M('M_Vista_Canopy')})
    foliage_type(far, 1, (1.0, 1.8), shadow=False)

def b_details():
    """NEW detail pieces that tie things together (all with Foliage Types): bushes, path-edge tufts (SM_Vista_Tuft, in
    "grass"), water-edge reeds, path stones, fallen leaves. Small pieces: no shadows except bushes, short cull distances."""
    leafy = {'Leaves': M('M_Vista_Leaf'), 'Fill': M('M_Vista_Canopy')}
    for n, dens, sc, cull, shadow in (('SM_Vista_Bush_A', 3, (0.8, 1.3), 12000, True), ('SM_Vista_Bush_B', 4, (0.8, 1.3), 10000, True)):
        mesh = world_asset(n, leafy, BUSH_LODS)
        if mesh: foliage_type(mesh, dens, sc, shadow=shadow, cull=cull, collision=False)
    reeds = world_asset('SM_Vista_Reeds', M('M_Vista_GrassTall'), ())
    if reeds: foliage_type(reeds, 30, (0.8, 1.3), shadow=False, cull=6000)
    stones = world_asset('SM_Vista_PathStones', M('M_Vista_Rock'), ())
    if stones: foliage_type(stones, 10, (0.8, 1.4), shadow=False, cull=4000, align=True, sink=2)
    leaves = world_asset('SM_Vista_FallenLeaves', M('M_Vista_Leaf'), ())
    if leaves: foliage_type(leaves, 8, (0.8, 1.3), shadow=False, cull=3500, align=True)

def b_textures():
    """painted textures from the Blender libraries (in place: re-import into the same texture asset)."""
    for name, png, srgb in (('T_Leaf_Cluster_Atlas_BaseColor', f'{SRC}/Trees_R02/textures/Leaf_Cluster_Atlas_BaseColor.png', True),
                            ('T_Bark_Painted_R02_BaseColor', f'{SRC}/Trees_R02/textures/Bark_Painted_R02_BaseColor.png', True)):
        if not os.path.exists(png): problem(f'{name}: missing {png}'); continue
        t = unreal.AssetImportTask(); t.filename = png; t.destination_path = TEXTURES; t.destination_name = name
        t.automated = True; t.replace_existing = True; t.save = True
        TOOLS.import_asset_tasks([t])
        tx = tex(name)
        if not tx: problem(f'{name}: import failed'); continue
        tx.set_editor_property('srgb', srgb)
        if 'Leaf' in name:
            tx.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_SHARPEN4)   # leaf edges stay crisp at distance
        EAL.save_loaded_asset(tx); unreal.log(f'TEXTURE {name}')

def tree_asset(name, fbx_dir, density, scale, lod_screens=(1.0, 0.4, 0.14), shadow=True):
    """Blender tree (Trees_R02) with its hand-made LODs + collision hulls; materials by slot (Bark/Leaves/Fill)."""
    info = manifest(os.path.dirname(fbx_dir)).get(name[3:], {})                 # Trees_R02/manifest.json: Blender dims (m)
    mesh = mesh_asset(name, f'{fbx_dir}/{name}.fbx', min_hulls=info.get('hulls', 1), size_cm=max(info['dims']) * 100 if info else None)
    if not mesh: return None
    lods = [f'{fbx_dir}/{name}_LOD{i}.fbx' for i in (1, 2)]
    if all(os.path.exists(p) for p in lods): set_lods(mesh, lods, list(lod_screens))
    assign(mesh, {'Bark': M('M_Vista_Bark'), 'Leaves': M('M_Vista_Leaf'), 'Fill': M('M_Vista_Canopy')})
    foliage_type(mesh, density, scale, collision=True, up_deg=30, shadow=shadow)
    return mesh

def b_tree_ancient():
    """hero tree (vista places it at 0.75)."""
    if os.path.exists(f'{TREES}/SM_Tree_Ancient.fbx'):
        tree_asset('SM_Tree_Ancient', TREES, 0.05, (0.7, 0.85), (1.0, 0.3, 0.1))
    else:
        mesh = mesh_asset('SM_Tree_Ancient', f'{SRC}/Trees_R01/exports/SM_Tree_Ancient.fbx', min_hulls=1)
        assign(mesh, {'Bark': M('M_Vista_Bark'), 'Leaves': M('M_Vista_Leaf')})

def b_trees_forest():
    """the four Blender forest trees (Trees_R02) as reusable assets + foliage types (not placed in the vista yet)."""
    for k in range(1, 5):
        tree_asset(f'SM_Tree_Forest_0{k}', TREES, 0.5, (0.85, 1.2))

def b_cliffs():
    for n in ('SM_Vista_Cliff_A', 'SM_Vista_Cliff_B'):
        mesh = mesh_asset(n, pick(f'{EXP2}/{n}.fbx', f'{EXP1}/{n}.fbx'), normals=True)
        assign(mesh, {'Rock': M('M_Vista_Rock'), 'Leaves': M('M_Vista_Leaf')})
        mesh.get_editor_property('body_setup').set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        EAL.save_loaded_asset(mesh)

def b_effects():
    for n, mat in (('SM_Vista_Motes', 'M_Vista_Motes'), ('SM_Vista_DriftLeaves', 'M_Vista_DriftLeaf')):
        mesh = mesh_asset(n, pick(f'{EXP2}/{n}.fbx', f'{EXP1}/{n}.fbx'), keep_size=False)
        mesh.set_material(0, M(mat))
        # drifting cards move far from their rest position (WPO): widen bounds so they are not culled
        mesh.set_editor_property('positive_bounds_extension', unreal.Vector(1500, 1500, 300))
        mesh.set_editor_property('negative_bounds_extension', unreal.Vector(1500, 1500, 300))
        EAL.save_loaded_asset(mesh)
    path = f'{MESHES}/SM_Vista_MistSheet'
    plane = load(path) or EAL.duplicate_asset('/Engine/BasicShapes/Plane', path)
    plane.set_material(0, M('M_Vista_Mist')); unreal.EditorStaticMeshLibrary.remove_collisions(plane)
    plane.get_editor_property('body_setup').set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
    EAL.save_loaded_asset(plane)

def b_frozen():
    """sky dome material + clouds: frozen (user decision) - only created if missing (exec'd from the R03 builder code)."""
    if not M('M_Vista_Sky') or not M('MI_Vista_Clouds'):
        problem('M_Vista_Sky / MI_Vista_Clouds missing - restore them from git (cfff82f), they are not rebuilt here')

def b_review():
    """review-stage pieces (L_AssetReview, Tools/World/build_asset_review.py): neutral grassy ground + the ground swatch
    (terrain material on a patch with path / shore / rock-slope vertex colours)."""
    m = new_material('M_Review_Ground')
    c = custom(m, HELP + 'return C * (0.9 + 0.2 * w.fbm(WP.xy / 400.0));', ['WP', 'C'], F3, (), -300, 0)
    feed(m, c, {'WP': ('node', unreal.MaterialExpressionWorldPosition), 'C': ('vector', lin((112, 140, 32)))})
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(node(m, unreal.MaterialExpressionConstant, -300, 200, r=0.95), '', unreal.MaterialProperty.MP_ROUGHNESS)
    finish(m)
    sw = mesh_asset('SM_Review_GroundSwatch', f'{EXP2}/SM_Review_GroundSwatch.fbx', keep_size=False)
    if sw:
        sw.set_material(0, M('M_Vista_Terrain')); nanite(sw)

BUILDERS = {'textures': b_textures, 'materials': b_materials, 'review': b_review, 'ground': b_ground, 'grass': b_grass, 'flowers': b_flowers, 'trees_round': b_trees_round,
            'tree_ancient': b_tree_ancient, 'trees_forest': b_trees_forest, 'details': b_details, 'cliffs': b_cliffs, 'effects': b_effects, 'frozen': b_frozen}
ORDER = list(BUILDERS)                       # materials first: the mesh builders assign them

def requested():
    raw = ' '.join(sys.argv[1:]) or os.environ.get('RPG_VISTA_ASSETS', '') or 'all'
    names = [n for n in raw.replace(',', ' ').split() if n]
    return ORDER if 'all' in names else [n for n in ORDER if n in names] + [n for n in names if n not in ORDER]

for name in requested():
    if name not in BUILDERS: problem(f'unknown builder {name}'); continue
    unreal.log(f'ASSETS_BUILD {name}')
    try: BUILDERS[name]()
    except Exception as ex:
        import traceback; problem(f'builder {name} failed: {ex}\n{traceback.format_exc()}')
for p in PROBLEMS: unreal.log_warning('ASSETS_PROBLEM ' + p.splitlines()[0])
unreal.log(f'ASSETS_DONE problems={len(PROBLEMS)}')
if os.environ.get('RPG_VISTA_ASSETS_KEEP_OPEN') != '1':
    unreal.SystemLibrary.quit_editor()
