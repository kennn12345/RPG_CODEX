"""Builds /Game/RPG/Maps/L_WorldVista: the "hero vista" slice of the stylized world (style target: golden-hour fantasy
forest of the Dreamscape: Tower video - original content; no floating chained rocks, no tower, no birds yet - per the user).
R02: day/night cycle (ARPGSkyCycle + sky dome: big sun/moon, stars, Milky Way - approved sky concept 10), the stylized
water (Docs/WATER_UNREAL_HANDOFF.md) on the lake, 4 ponds and 2 rivers, far hills + mountains, stacked cliffs with ivy,
round fluffy trees, lake mist, pollen motes / fireflies and drifting leaves.
Run in the full editor (saves and quits):
  UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/build_vista.py" -unattended -RenderOffscreen
Photograph: -ExecCmds="Automation RunTests Visual.WorldShots" (WorldShot cameras) / Visual.SkyShots (SkyShot cameras x 5 hours)."""
import unreal, math, json, os

SRC = 'C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs'
EXP = SRC + '/World_R01/exports'
ROOT = '/Game/RPG/Environment/World'
MESHES, MATS, FOLIAGE = ROOT + '/Meshes', ROOT + '/Materials', ROOT + '/Foliage'   # reusable assets, by kind
WATER = '/Game/RPG/Environment/Water'
MAP = '/Game/RPG/Maps/L_WorldVista'
CLOUD_COVER = -0.05                      # added to the engine default cloud coverage (-0.2): lower = fewer, more separate clouds
EAL, MEL = unreal.EditorAssetLibrary, unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
LAYOUT = json.load(open(EXP + '/vista_layout.json'))
YS = -1.0                               # the FBX importer mirrors Blender Y (see Docs/WATER_UNREAL_HANDOFF.md)
def U(x, y): return (x * 100.0, YS * y * 100.0)   # Blender metres -> Unreal cm (XY)

def lin(c): return [((v / 255) / 12.92) if v / 255 <= .04045 else ((v / 255 + .055) / 1.055) ** 2.4 for v in c]

# ------------------------------------------------------------------ import
unreal.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')
def import_fbx(path, dest, scale=1.0, normals=False):
    name = os.path.splitext(os.path.basename(path))[0]
    if EAL.does_asset_exist(f'{dest}/{name}'): EAL.delete_asset(f'{dest}/{name}')   # fresh import (re-import keeps stale settings)
    t = unreal.AssetImportTask(); t.filename = path; t.destination_path = dest; t.automated = True; t.replace_existing = True; t.save = True
    o = unreal.FbxImportUI(); o.import_mesh = True; o.import_materials = False; o.import_textures = False; o.import_as_skeletal = False
    sm = o.static_mesh_import_data; sm.combine_meshes = True; sm.auto_generate_collision = False
    sm.vertex_color_import_option = unreal.VertexColorImportOption.REPLACE; sm.generate_lightmap_u_vs = False; sm.import_uniform_scale = scale
    if normals: sm.normal_import_method = unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS   # bent leaf normals (soft round crowns)
    t.options = o; TOOLS.import_asset_tasks([t])
    return unreal.load_asset(list(t.imported_object_paths)[0])

PLAIN = ('SM_Vista_Terrain', 'SM_Vista_Far', 'SM_Vista_Grass', 'SM_Vista_GrassTall', 'SM_Vista_Poppies',
         'SM_Vista_Tree_Far', 'SM_Vista_Motes', 'SM_Vista_DriftLeaves')
BENT = ('SM_Vista_Cliff_A', 'SM_Vista_Cliff_B', 'SM_Vista_Tree_Round', 'SM_Vista_Tree_Tall', 'SM_Vista_Tree_Wide')
# R02 kept everything flat in ROOT: clear those (the map is rebuilt below) so each asset lives once, in its folder
for path in EAL.list_assets(ROOT, recursive=False, include_folder=False):
    EAL.delete_asset(path.split('.')[0])
M_ = {n: import_fbx(f'{EXP}/{n}.fbx', MESHES) for n in PLAIN}
M_.update({n: import_fbx(f'{EXP}/{n}.fbx', MESHES, normals=True) for n in BENT})
for old in ('SM_Vista_Mountains',):                                   # superseded by SM_Vista_Far
    if EAL.does_asset_exist(f'{ROOT}/{old}'): EAL.delete_asset(f'{ROOT}/{old}')
TREE_ANCIENT = import_fbx(SRC + '/Trees_R01/exports/SM_Tree_Ancient.fbx', MESHES)
for n in ('SM_Vista_Terrain', 'SM_Vista_Far', 'SM_Vista_Cliff_A', 'SM_Vista_Cliff_B'):
    M_[n].get_editor_property('body_setup').set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
# drifting cards move far from their rest position (WPO): widen bounds so they are not culled
for n in ('SM_Vista_Motes', 'SM_Vista_DriftLeaves'):
    M_[n].set_editor_property('positive_bounds_extension', unreal.Vector(1500, 1500, 300))
    M_[n].set_editor_property('negative_bounds_extension', unreal.Vector(1500, 1500, 300))
T_STONE = unreal.load_asset('/Game/RPG/Environment/Textures/T_Stone_Painted_BaseColor')
T_BARK = unreal.load_asset('/Game/RPG/Environment/Textures/T_Bark_Painted_BaseColor')
T_LEAF = unreal.load_asset('/Game/RPG/Environment/Textures/T_Leaf_Maple_Atlas_BaseColor')

# ------------------------------------------------------------------ material helpers
def new_material(name):
    if EAL.does_asset_exist(f'{MATS}/{name}'): EAL.delete_asset(f'{MATS}/{name}')
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
    """inputs: name -> expression | ('scalar', v) | ('vector', rgb) | ('node', cls) | ('night',) = 0 day .. 1 night from the sun direction"""
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
def finish(m):
    # scattered meshes are instanced (ARPGScatter); without this usage flag they render with the default grey material
    m.set_editor_property('used_with_instanced_static_meshes', True)
    m.set_editor_property('used_with_nanite', True)                    # terrain / far hills / boulders are Nanite
    MEL.recompile_material(m); EAL.save_loaded_asset(m); return m

def cpd(m, name, index, x, y, default=0.0):
    """scalar read from the component's custom primitive data (written by ARPGSkyCycle for its SkyReceivers)."""
    e = scalar(m, name, default, x, y)
    e.set_editor_property('use_custom_primitive_data', True); e.set_editor_property('primitive_data_index', index)
    return e

def night_node(m, x, y):
    """0 = day .. 1 = night (ARPGSkyCycle custom primitive data 6)."""
    return cpd(m, 'NightAmount', 6, x, y)

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
# tree sway (whole tree, by height above its base) + leaf flutter
SWAY = HELP + r'''
float hh = saturate((WP.z - OP.z) / 1500.0);
float ph = T * 0.7 + dot(OP.xy, float2(0.001, 0.0013));
float3 o = float3(normalize(float2(1.0, 0.35)) * sin(ph) * SwayAmp * hh * hh, 0);
o += Flutter * float3(sin(T * 2.7 + dot(WP, float3(0.031, 0.027, 0.02))), cos(T * 2.3 + dot(WP, float3(0.022, 0.035, 0.018))), sin(T * 3.1 + WP.x * 0.03) * 0.5) * hh;
return o;
'''
# drifting cards (pollen / fireflies / leaves): every card moves downwind and wraps inside its actor's box; R/G/B = seeds,
# Ctr (UV1) = the card's centre, so a card wraps as one piece instead of stretching across the box
DRIFT = HELP + r'''
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

def sway_wpo(m, flutter):
    c = custom(m, SWAY, ['WP', 'OP', 'T', 'SwayAmp', 'Flutter'], F3, (), -400, 600)
    feed(m, c, {'WP': ('node', unreal.MaterialExpressionWorldPosition), 'OP': ('node', unreal.MaterialExpressionObjectPositionWS),
                'T': ('node', unreal.MaterialExpressionTime), 'SwayAmp': ('scalar', 18.0), 'Flutter': ('scalar', flutter)})
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

def drift_wpo(m, speed, bob, box=(1500.0, 1500.0, 500.0)):
    c = custom(m, DRIFT, ['WP', 'OP', 'T', 'Seed', 'Ctr', 'Box', 'Speed', 'Bob'], F3, (), -400, 600)
    link(node(m, unreal.MaterialExpressionTextureCoordinate, -900, 800, coordinate_index=1), '', c, 'Ctr')
    vc = node(m, unreal.MaterialExpressionVertexColor, -900, 700)
    feed(m, c, {'WP': ('node', unreal.MaterialExpressionWorldPosition), 'OP': ('node', unreal.MaterialExpressionObjectPositionWS),
                'T': ('node', unreal.MaterialExpressionTime), 'Box': ('vector', box), 'Speed': ('scalar', speed), 'Bob': ('scalar', bob)})
    link(vc, '', c, 'Seed')
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

# ------------------------------------------------------------------ materials
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
    rt = node(m, unreal.MaterialExpressionTextureSample, -900, 0, texture=T_STONE); link(dv, '', rt, 'UVs')
    vc = node(m, unreal.MaterialExpressionVertexColor, -900, 200)
    feed(m, c, {'WP': wp, 'N': ('node', unreal.MaterialExpressionVertexNormalWS), 'GrassC': ('vector', lin((110, 175, 40))),
                'GrassLight': ('vector', lin((175, 205, 55))), 'GrassDark': ('vector', lin((60, 115, 35))),
                'DirtC': ('vector', lin((176, 128, 78))), 'RockC': ('vector', lin((104, 106, 114))), 'BedC': ('vector', lin((118, 96, 64)))}, x=-1300)
    link(vc, 'R', c, 'Path'); link(vc, 'G', c, 'Wet'); link(rt, 'RGB', c, 'Rock')
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(node(m, unreal.MaterialExpressionConstant, -300, 200, r=0.92), '', unreal.MaterialProperty.MP_ROUGHNESS)
    return finish(m)

def m_grass(name, root, tip, strength, sss):
    m = new_material(name)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE); m.set_editor_property('two_sided', True)
    code = HELP + r'''
float patch = w.fbm(WP.xy / 900.0);
float3 tipc = lerp(TipC, TipC * float3(1.08, 1.0, 0.7) + float3(0.05, 0.03, 0.0), smoothstep(0.55, 0.8, patch));
float3 c = lerp(RootC, tipc, pow(saturate(H), 0.85)) * lerp(0.85, 1.1, w.fbm(WP.xy / 300.0 + 3.0));
Sss = c * SssGain;
return c;
'''
    c = custom(m, code, ['WP', 'H', 'RootC', 'TipC', 'SssGain'], F3, (('Sss', F3),), -300, 0)
    vc = node(m, unreal.MaterialExpressionVertexColor, -900, 300)
    feed(m, c, {'WP': ('node', unreal.MaterialExpressionWorldPosition), 'RootC': ('vector', lin(root)), 'TipC': ('vector', lin(tip)),
                'SssGain': ('scalar', sss)})
    link(vc, 'R', c, 'H')
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(c, 'Sss', unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    MEL.connect_material_property(node(m, unreal.MaterialExpressionConstant, -300, 250, r=0.75), '', unreal.MaterialProperty.MP_ROUGHNESS)
    wind_wpo(m, strength)
    return finish(m)

def m_flowers():
    m = new_material('M_Vista_Poppies')
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE); m.set_editor_property('two_sided', True)
    code = r'''
float3 stem = float3(0.06, 0.16, 0.03);
float3 c = Kind > 0.75 ? Petal : (Kind > 0.25 ? Centre : stem);
Sss = c * 0.6;
return c;
'''
    c = custom(m, code, ['Kind', 'Petal', 'Centre'], F3, (('Sss', F3),), -300, 0)
    vc = node(m, unreal.MaterialExpressionVertexColor, -900, 0); link(vc, 'G', c, 'Kind')
    link(vector(m, 'Petal', lin((225, 38, 28)), -900, 150), '', c, 'Petal')
    link(vector(m, 'Centre', (0.03, 0.02, 0.02), -900, 250), '', c, 'Centre')
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(c, 'Sss', unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    wind_wpo(m, 9.0)
    return finish(m)

def m_canopy():
    """Leaf cards + fill blobs of the round trees and the cliff ivy: vertex G = light (inner dark .. outer sunlit), B = tint."""
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
    feed(m, c, {'WP': ('node', unreal.MaterialExpressionWorldPosition), 'Dark': ('vector', lin((24, 70, 34))),
                'Mid': ('vector', lin((70, 150, 40))), 'Light': ('vector', lin((175, 215, 60))), 'SssGain': ('scalar', 0.9)})
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(c, 'Sss', unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    MEL.connect_material_property(node(m, unreal.MaterialExpressionConstant, -300, 250, r=0.7), '', unreal.MaterialProperty.MP_ROUGHNESS)
    sway_wpo(m, 2.5)
    return finish(m)

def m_leaf():
    m = new_material('M_Vista_Leaf')
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE); m.set_editor_property('two_sided', True)
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    code = r'''
float3 c = Tex * VC * Gain;
float l = dot(c, float3(0.3, 0.59, 0.11));
c = max(lerp(float3(l, l, l), c, Sat), 0.0);              // more saturated, like the reference canopies
Sss = c * float3(0.9, 1.0, 0.45) * SssGain;
return c;
'''
    c = custom(m, code, ['Tex', 'VC', 'Gain', 'Sat', 'SssGain'], F3, (('Sss', F3),), -300, 0)
    ts = node(m, unreal.MaterialExpressionTextureSample, -900, 0, texture=T_LEAF)
    vc = node(m, unreal.MaterialExpressionVertexColor, -900, 200)
    link(ts, 'RGB', c, 'Tex'); link(vc, '', c, 'VC')
    feed(m, c, {'Gain': ('scalar', 1.45), 'Sat': ('scalar', 1.35), 'SssGain': ('scalar', 0.8)}, x=-900)
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(c, 'Sss', unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    MEL.connect_material_property(ts, 'A', unreal.MaterialProperty.MP_OPACITY_MASK)
    sway_wpo(m, 3.5)
    return finish(m)

def m_bark():
    m = new_material('M_Vista_Bark')
    ts = node(m, unreal.MaterialExpressionTextureSample, -700, 0, texture=T_BARK)
    mul = node(m, unreal.MaterialExpressionMultiply, -400, 0); link(ts, 'RGB', mul, 'A')
    link(node(m, unreal.MaterialExpressionConstant3Vector, -700, 200, constant=unreal.LinearColor(0.62, 0.48, 0.40, 1)), '', mul, 'B')   # painted bark is already warm
    MEL.connect_material_property(mul, '', unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(node(m, unreal.MaterialExpressionConstant, -300, 250, r=0.85), '', unreal.MaterialProperty.MP_ROUGHNESS)
    sway_wpo(m, 0.0)
    return finish(m)

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
        ts = node(m, unreal.MaterialExpressionTextureSample, -1000, i * 160, texture=T_STONE); link(dv, '', ts, 'UVs'); link(ts, 'RGB', c, nm)
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

SKY_CODE = HELP + r'''
float3 V = normalize(-Cam);
float3 Ls = normalize(float3(SX, SY, SZ) + float3(0, 0, 1e-4)), Lm = normalize(float3(MX, MY, MZ) + float3(0, 0, 1e-4));   // toward sun / moon
float above = smoothstep(-0.02, 0.03, V.z);
// stylized sky gradient keyed to the sun's height (approved concept: noon blue, golden sunset/morning, purple dusk, navy night)
float h = Ls.z, t = pow(saturate(V.z), 0.45);
float3 zN = float3(0.05, 0.22, 0.75), hN = float3(0.42, 0.62, 0.95);      // noon
float3 zG = float3(0.22, 0.17, 0.48), hG = float3(1.0, 0.52, 0.22);       // golden hour
float3 zT = float3(0.06, 0.05, 0.2), hT = float3(0.42, 0.22, 0.32);       // dusk
float3 zX = float3(0.004, 0.007, 0.03), hX = float3(0.02, 0.025, 0.07);   // night
float a1 = smoothstep(0.03, 0.45, h), a2 = smoothstep(-0.12, 0.03, h), a3 = smoothstep(-0.3, -0.1, h);
float3 zen = lerp(lerp(lerp(zX, zT, a3), zG, a2), zN, a1);
float3 hor = lerp(lerp(lerp(hX, hT, a3), hG, a2), hN, a1);
float3 c = lerp(hor, zen, t);
float side = pow(saturate(dot(normalize(V.xy + 0.0001), normalize(Ls.xy + 0.0001)) * 0.5 + 0.5), 4.0);
c = lerp(c, hG * 1.2, side * (1.0 - a1) * a3 * (1.0 - t) * 0.6);          // warm glow on the sun's side when it is low
c *= SkyBright * lerp(0.6, 1.0, a1);                                       // dimmer low-sun sky so the big sun disc stands out
// stars: one random point per cell of a direction grid, a few cells lit, softly twinkling
float3 sv = V * 170.0, cell = floor(sv), f = frac(sv);
float3 sp = float3(w.h3(cell + 1.3), w.h3(cell + 2.7), w.h3(cell + 5.1)) * 0.7 + 0.15;
float hs = w.h3(cell);
float star = step(0.92, hs) * smoothstep(0.16, 0.0, length(f - sp)) * (0.3 + 0.7 * w.h3(cell + 9.1));
star *= 0.75 + 0.25 * sin(T * (1.5 + 3.0 * hs) + hs * 60.0);
float3 sv2 = V * 420.0, cell2 = floor(sv2);                                   // faint dense star dust
float dust = step(0.985, w.h3(cell2)) * smoothstep(0.3, 0.0, length(frac(sv2) - 0.5)) * 0.35;
// Milky Way: soft band across the sky with cloudy structure and a brighter core
float3 mwN = normalize(float3(0.35, 0.8, 0.5));
float dz = dot(V, mwN);
float band = exp(-dz * dz / 0.03);
float neb = w.fbm3(V * 5.0), neb2 = w.fbm3(V * 17.0 + 3.0);
float3 mw = band * (float3(0.42, 0.33, 0.75) * smoothstep(0.35, 0.75, neb) + float3(0.95, 0.85, 1.0) * pow(saturate(neb2), 3.0) * 1.6 * exp(-dz * dz / 0.008));
float dustBand = dust * (1.0 + 3.0 * band);
float3 nightSky = (star * float3(1.0, 0.96, 0.88) * StarBright + dustBand * float3(0.8, 0.85, 1.0) * StarBright + mw * MilkyBright) * Night * above;
// big soft sun (light 0) and big cratered moon (light 1)
float cs = dot(V, Ls);
float disc = smoothstep(cos(SunSize), cos(SunSize * 0.9), cs);
float glow = pow(saturate(cs), 900.0) * 0.5 + pow(saturate(cs), 60.0) * 0.12;
float3 sunCol = lerp(float3(1.0, 0.55, 0.25), float3(1.0, 0.93, 0.72), smoothstep(0.0, 0.35, h));   // orange near the horizon
float3 sunC = sunCol * smoothstep(-0.05, 0.02, h) * (disc * SunGain + glow * GlowGain);
float cm = dot(V, Lm);
float md = smoothstep(cos(MoonSize), cos(MoonSize * 0.97), cm);
float3 tx = normalize(cross(Lm, float3(0, 0, 1))), ty = cross(tx, Lm);
float2 uv = float2(dot(V, tx), dot(V, ty)) / sin(MoonSize);
float limb = sqrt(saturate(1.0 - dot(uv, uv)));
float maria = smoothstep(0.42, 0.62, w.fbm3(float3(uv * 2.2, 3.7)));
float craters = w.fbm3(float3(uv * 9.0, 1.3));
float mtex = (0.95 - 0.35 * maria) * (0.85 + 0.3 * craters) * (0.55 + 0.45 * limb);
float3 moonC = (md * mtex * float3(0.98, 0.95, 0.88) * MoonBright + pow(saturate(cm), 250.0) * float3(0.55, 0.65, 0.95) * MoonGlow) * Night;
return c + nightSky + (sunC + moonC) * above;
'''

def m_sky():
    """Sky dome (Is Sky): stylized gradient + the concept's large sun/moon, stars and Milky Way, driven by ARPGSkyCycle
    (sun / moon direction + night amount as custom primitive data). The SkyAtmosphere still lights the scene."""
    m = new_material('M_Vista_Sky')
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT); m.set_editor_property('two_sided', True)
    m.set_editor_property('is_sky', True)
    c = custom(m, SKY_CODE, ['Cam', 'SkyBright', 'SX', 'SY', 'SZ', 'MX', 'MY', 'MZ', 'T', 'Night', 'StarBright', 'MilkyBright', 'SunSize', 'SunGain', 'GlowGain',
                             'MoonSize', 'MoonBright', 'MoonGlow'], F3, (), -300, 0)
    feed(m, c, {'Cam': ('node', unreal.MaterialExpressionCameraVectorWS), 'SkyBright': ('scalar', 2.0),
                'T': ('node', unreal.MaterialExpressionTime), 'Night': ('night',),
                'StarBright': ('scalar', 0.6), 'MilkyBright': ('scalar', 0.15), 'SunSize': ('scalar', math.radians(4.0)),
                'SunGain': ('scalar', 30.0), 'GlowGain': ('scalar', 3.0), 'MoonSize': ('scalar', math.radians(5.0)),
                'MoonBright': ('scalar', 0.6), 'MoonGlow': ('scalar', 0.05)}, x=-1100)
    for i, nm in enumerate(('SX', 'SY', 'SZ', 'MX', 'MY', 'MZ')):
        link(cpd(m, nm, i, -700, 400 + i * 90), '', c, nm)
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(m); EAL.save_loaded_asset(m); return m

def instance(name, parent, vectors=None, scalars=None):
    path = f'{MATS}/{name}'
    if EAL.does_asset_exist(path): EAL.delete_asset(path)
    mi = TOOLS.create_asset(name, MATS, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, parent)
    for k, rgb in (vectors or {}).items(): MEL.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(*rgb, 1))
    for k, v in (scalars or {}).items(): MEL.set_material_instance_scalar_parameter_value(mi, k, v)
    EAL.save_loaded_asset(mi); return mi

MT = m_terrain()
MG = m_grass('M_Vista_Grass', (40, 92, 25), (165, 205, 50), 12.0, 0.7)
MGT = m_grass('M_Vista_GrassTall', (45, 95, 28), (150, 200, 55), 18.0, 0.7)
MW = m_grass('M_Vista_Wheat', (150, 110, 35), (245, 200, 70), 16.0, 0.9)
MF = m_flowers(); MC = m_canopy(); ML = m_leaf(); MB = m_bark(); MR = m_rock()
MI_DAISY = instance('MI_Vista_Daisies', MF, {'Petal': lin((245, 240, 225)), 'Centre': lin((240, 180, 30))})
MMOTE = m_motes(); MDL = m_drift_leaf(); MMIST = m_mist(); MSKY = m_sky()
for old in ('M_Vista_Mountains', 'MI_Vista_Lake', 'MPC_Sky'):                     # superseded (far land uses the terrain material; lake = the original water)
    if EAL.does_asset_exist(f'{ROOT}/{old}'): EAL.delete_asset(f'{ROOT}/{old}')

def assign(mesh, by_slot):
    """materials by FBX slot name (Bark/Leaves/Rock), falling back to slot order."""
    for i, sm in enumerate(mesh.static_materials):
        nm = str(sm.material_slot_name)
        mat = next((v for k, v in by_slot.items() if k.lower() in nm.lower()), list(by_slot.values())[min(i, len(by_slot) - 1)])
        mesh.set_material(i, mat)
    EAL.save_loaded_asset(mesh)
for n, mat in (('SM_Vista_Terrain', MT), ('SM_Vista_Far', MT), ('SM_Vista_Grass', MG), ('SM_Vista_GrassTall', MGT),
               ('SM_Vista_Poppies', MF), ('SM_Vista_Motes', MMOTE), ('SM_Vista_DriftLeaves', MDL)):
    M_[n].set_material(0, mat); EAL.save_loaded_asset(M_[n])
for n in ('SM_Vista_Tree_Round', 'SM_Vista_Tree_Tall', 'SM_Vista_Tree_Wide'): assign(M_[n], {'Bark': MB, 'Leaves': ML, 'Fill': MC})
assign(M_['SM_Vista_Tree_Far'], {'Bark': MB, 'Leaves': MC})
for n in ('SM_Vista_Cliff_A', 'SM_Vista_Cliff_B'): assign(M_[n], {'Rock': MR, 'Leaves': ML})

def variant(src, name, mat):
    """a mesh asset of its own (same geometry, its own material) - e.g. wheat, daisies - so it can be placed anywhere."""
    path = f'{MESHES}/{name}'
    if EAL.does_asset_exist(path): EAL.delete_asset(path)
    m = EAL.duplicate_asset(src.get_path_name().split('.')[0], path)
    m.set_material(0, mat); EAL.save_loaded_asset(m); return m
M_['SM_Vista_Wheat'] = variant(M_['SM_Vista_GrassTall'], 'SM_Vista_Wheat', MW)
M_['SM_Vista_Daisies'] = variant(M_['SM_Vista_Poppies'], 'SM_Vista_Daisies', MI_DAISY)

# Nanite on the big opaque pieces (no masks): far fewer Virtual Shadow Map non-Nanite marking jobs
for n in ('SM_Vista_Terrain', 'SM_Vista_Far'):
    ns = M_[n].get_editor_property('nanite_settings'); ns.set_editor_property('enabled', True)
    M_[n].set_editor_property('nanite_settings', ns); EAL.save_loaded_asset(M_[n])

def foliage_type(mesh, density, scale, align=False, shadow=True, cull=0, collision=False, sink=0.0, up_deg=45.0):
    """FT_<mesh>: paintable with the Foliage tool (same mesh + material as the vista scatter)."""
    name = 'FT_' + mesh.get_name()[3:]; path = f'{FOLIAGE}/{name}'
    try:   # created once, then updated in place (re-creating a deleted foliage type in one session returns None)
        ft = unreal.load_asset(path) if EAL.does_asset_exist(path) else \
            TOOLS.create_asset(name, FOLIAGE, unreal.FoliageType_InstancedStaticMesh, unreal.FoliageType_InstancedStaticMeshFactory())
    except Exception as ex:
        unreal.log_warning(f'foliage type {name}: {ex}'); return None
    fr = unreal.FloatInterval(); fr.min, fr.max = scale
    for k, v in (('mesh', mesh), ('density', density), ('scale_x', fr), ('align_to_normal', align), ('cast_shadow', shadow),
                 ('ground_slope_angle', unreal.FloatInterval(0.0, up_deg))):
        try: ft.set_editor_property(k, v)
        except Exception as ex: unreal.log_warning(f'foliage type {name} {k}: {ex}')
    if cull:
        ci = unreal.Int32Interval(); ci.min, ci.max = int(cull * .8), int(cull)
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
for n, dens, sc, kw in (('SM_Vista_Grass', 400, (0.8, 1.3), dict(shadow=False, cull=9000)),
                        ('SM_Vista_GrassTall', 120, (0.8, 1.25), dict(shadow=False, cull=9000)),
                        ('SM_Vista_Wheat', 300, (0.9, 1.3), dict(shadow=False, cull=9000)),
                        ('SM_Vista_Poppies', 60, (1.1, 1.5), dict(shadow=False, cull=7000)),
                        ('SM_Vista_Daisies', 40, (0.9, 1.2), dict(shadow=False, cull=6000)),
                        ('SM_Vista_Tree_Round', 0.6, (0.85, 1.3), dict(collision=True, up_deg=30)),
                        ('SM_Vista_Tree_Tall', 0.6, (0.85, 1.3), dict(collision=True, up_deg=30)),
                        ('SM_Vista_Tree_Wide', 0.5, (0.85, 1.3), dict(collision=True, up_deg=30)),
                        ('SM_Vista_Tree_Far', 1, (1.0, 1.8), dict(shadow=False))):
    foliage_type(M_[n], dens, sc, **kw)

# rocks = the Blender rock library (Tools/World/import_rocks.py -> /Game/RPG/Environment/Rocks); the old generated
# vista boulders are retired
for old in (f'{MESHES}/SM_Vista_Boulder_A', f'{MESHES}/SM_Vista_Boulder_B', f'{FOLIAGE}/FT_Vista_Boulder_A', f'{FOLIAGE}/FT_Vista_Boulder_B'):
    if EAL.does_asset_exist(old): EAL.delete_asset(old)
if not EAL.does_asset_exist('/Game/RPG/Environment/Rocks/Meshes/SM_Rock_Large_A'):
    exec(open('C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/import_rocks.py').read(), {})
def ROCK(n): return unreal.load_asset(f'/Game/RPG/Environment/Rocks/Meshes/SM_{n}')
TREE_ANCIENT.set_material(0, MB)
if len(TREE_ANCIENT.static_materials) > 1: TREE_ANCIENT.set_material(1, ML)
EAL.save_loaded_asset(TREE_ANCIENT)

# ------------------------------------------------------------------ level
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem); eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if EAL.does_asset_exist(MAP):
    les.load_level(MAP)
    for a in eas.get_all_level_actors():
        if not isinstance(a, (unreal.WorldSettings, unreal.Brush)): eas.destroy_actor(a)
else:
    les.new_level(MAP)
world = unreal.EditorLevelLibrary.get_editor_world()

def spawn(what, loc, rot=(0, 0, 0), label=None, scale=None):
    r = unreal.Rotator(roll=rot[2], pitch=rot[1], yaw=rot[0])
    a = eas.spawn_actor_from_class(what, unreal.Vector(*loc), r) if isinstance(what, type) else eas.spawn_actor_from_object(what, unreal.Vector(*loc), r)
    if label: a.set_actor_label(label)
    if scale: a.set_actor_scale3d(unreal.Vector(*scale))
    return a

def setp(obj, props, what):
    for prop, val in props:
        try: obj.set_editor_property(prop, val)
        except Exception as ex: unreal.log_warning(f'{what} {prop}: {ex}')

# ---- sky + lights: ARPGSkyCycle drives sun / moon / sky light / haze / exposure through one continuous day
def dir_light(label, index, shadows=True):
    a = spawn(unreal.DirectionalLight, (0, 0, 3000), (0, -45, 0), label)
    a.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    c = a.get_component_by_class(unreal.DirectionalLightComponent)
    setp(c, (('atmosphere_sun_light', True), ('atmosphere_sun_light_index', index), ('cast_shadows', shadows),
             ('light_source_angle', 1.5), ('cast_cloud_shadows', True)), label)
    return a
sun = dir_light('Sun', 0); moon = dir_light('Moon', 1)
setp(sun.get_component_by_class(unreal.DirectionalLightComponent), (('enable_light_shaft_bloom', True), ('bloom_scale', 0.35)), 'sun')
atm = spawn(unreal.SkyAtmosphere, (0, 0, 0), label='SkyAtmosphere')
# aerial perspective starts beyond the sky dome (30 km): it would otherwise filter the dome's blue to black; height fog does the haze
setp(atm.get_component_by_class(unreal.SkyAtmosphereComponent), (('aerial_perspective_start_depth', 100.0), ('mie_scattering_scale', 0.006)), 'atmosphere')
clouds = spawn(unreal.VolumetricCloud, (0, 0, 0), label='Clouds')
cc_ = clouds.get_component_by_class(unreal.VolumetricCloudComponent)
setp(cc_, (('layer_bottom_altitude', 2.2), ('layer_height', 4.5)), 'clouds')
try:
    # fewer, rounder clouds than the engine default (concept: simple puffy clouds in open sky)
    base_cloud = unreal.load_asset('/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst')
    vals = {k: MEL.get_material_instance_scalar_parameter_value(base_cloud, k) for k in ('Layout_CloudGlobalScale', 'Cloud_GlobalCoverage', 'Cloud_GlobalDensity')}
    unreal.log(f'CLOUD DEFAULTS {vals}')
    mi_cloud = instance('MI_Vista_Clouds', base_cloud, scalars={'Cloud_GlobalCoverage': vals['Cloud_GlobalCoverage'] + CLOUD_COVER,
                                                               'Layout_CloudGlobalScale': vals['Layout_CloudGlobalScale'] * 1.4})
    cc_.set_editor_property('material', mi_cloud)
except Exception as ex: unreal.log_warning(f'cloud material: {ex}')
sky = spawn(unreal.SkyLight, (0, 0, 800), label='SkyLight')
sky.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
setp(sky.get_component_by_class(unreal.SkyLightComponent), (('real_time_capture', True), ('intensity', 1.35)), 'skylight')
fog = spawn(unreal.ExponentialHeightFog, (0, 0, -300), label='HazeFog')
setp(fog.get_component_by_class(unreal.ExponentialHeightFogComponent),
     (('fog_density', 0.007), ('fog_height_falloff', 0.1), ('start_distance', 2500.0), ('fog_max_opacity', 0.8),
      ('fog_cutoff_distance', 2.0e6),                                     # no haze on the sky dome (30 km): keeps the sky blue

      ('directional_inscattering_exponent', 6.0), ('enable_volumetric_fog', True), ('volumetric_fog_scattering_distribution', 0.6)), 'fog')
pp = spawn(unreal.PostProcessVolume, (0, 0, 0), label='Grade'); pp.set_editor_property('unbound', True)
s = pp.get_editor_property('settings')
for prop, val in (('auto_exposure_bias', 0.15), ('auto_exposure_min_brightness', -1.5), ('auto_exposure_max_brightness', 14.0),
                  ('bloom_intensity', 1.1), ('color_saturation', unreal.Vector4(1.15, 1.15, 1.15, 1.0)),
                  ('color_contrast', unreal.Vector4(1.05, 1.05, 1.05, 1.0)), ('vignette_intensity', 0.3)):
    try:
        s.set_editor_property('override_' + prop, True); s.set_editor_property(prop, val)
    except Exception as ex: unreal.log_warning(f'grade {prop}: {ex}')
pp.set_editor_property('settings', s)
dome_mesh = unreal.load_asset('/Engine/EngineSky/SM_SkySphere') or unreal.load_asset('/Engine/BasicShapes/Sphere')
dome = spawn(dome_mesh, (0, 0, 0), label='SkyDome')
dsm = dome.static_mesh_component
dsm.set_material(0, MSKY); setp(dsm, (('cast_shadow', False), ('bounds_scale', 10.0)), 'dome'); dsm.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
db = dome_mesh.get_bounds().box_extent.x
dome.set_actor_scale3d(unreal.Vector(*(3.0e6 / max(db, 1.0),) * 3))                  # ~30 km radius, beyond the far mountains
cyc = spawn(unreal.RPGSkyCycle, (0, 0, 0), label='SkyCycle')
for k, v in (('sun', sun), ('moon', moon), ('sky_light', sky), ('fog', fog), ('grade', pp), ('time_of_day', 17.3),
             ('sunrise_yaw', 140.0), ('day_length_minutes', 24.0), ('moon_intensity', 0.25), ('exposure_bias_night', -2.5), ('sky_light_day', 0.9), ('sun_intensity', 9.0),
             ('sun_color_low', unreal.LinearColor(1.0, 0.55, 0.28, 1)), ('haze_low', unreal.LinearColor(0.48, 0.32, 0.24, 1)), ('sky_light_night', 1.0)):
    cyc.set_editor_property(k, v)
cyc.set_time_of_day(17.3)

# terrain + far hills / mountains
spawn(M_['SM_Vista_Terrain'], (0, 0, 0), label='Terrain')
spawn(M_['SM_Vista_Far'], (0, 0, 0), label='FarLand')

def ground(x, y, default=0.0):
    hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, 20000), unreal.Vector(x, y, -20000),
                                                 unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [], unreal.DrawDebugTrace.NONE, True)
    t = hit.to_tuple() if hit else None
    if not t or not t[0]: return default
    for v in t:
        if isinstance(v, unreal.Vector) and abs(v.z) < 19999: return v.z
    return default

# ---- water: the stylized water from L_WaterTest (same materials, waves, underwater look, caustics)
MI_RIVER, MI_LAKE = unreal.load_asset(f'{WATER}/MI_StylizedWater_River'), unreal.load_asset(f'{WATER}/MI_StylizedWater_Lake')
SM_LAKE, SM_STRIP = unreal.load_asset(f'{WATER}/SM_Water_Lake'), unreal.load_asset(f'{WATER}/SM_Water_River_Strip10')
M_UNDER, M_CAUST = unreal.load_asset(f'{WATER}/M_UnderwaterPP'), unreal.load_asset(f'{WATER}/M_WaterCaustics')
WAVES = {'River': (2.0, 400.0, 0.8, 0.0), 'Lake': (3.0, 550.0, 0.8, 30.0)}             # MUST match the MI wave params (build_water_test.py)
def waves(kind):
    s_ = unreal.RPGWaveSettings(); s_.height, s_.length, s_.speed, s_.direction = WAVES[kind]; return s_
LAKE_EXT = SM_LAKE.get_bounds().box_extent.x

def water_body(label, cx, cy, r, z, half_depth=300.0):
    """zone box (top = the waterline, 2 x half_depth deep - deeper than the bed) + the surface mesh."""
    X, Y = U(cx, cy); Z = z * 100.0
    b_ = spawn(unreal.RPGWaterBody, (X, Y, Z - half_depth), label=label)
    zone = b_.get_editor_property('zone')
    zone.set_box_extent(unreal.Vector(r * 100 * 1.6, r * 100 * 1.6, half_depth), True)
    setp(zone, (('waves', waves('Lake')), ('underwater_material', M_UNDER), ('caustic_material', M_CAUST)), label)
    sc_ = r * 100 * 1.6 / max(LAKE_EXT, 1.0)
    b_.set_editor_property('surface_scale', unreal.Vector(sc_, sc_, 1))
    b_.set_editor_property('water_mesh', SM_LAKE); b_.set_editor_property('water_material', MI_LAKE)
    return b_
lx, ly, lr = LAYOUT['lake']; LX, LY = U(lx, ly); LAKE_Z = LAYOUT['lake_z'] * 100.0
water_body('Lake', lx, ly, lr, LAYOUT['lake_z'])
for i, (px, py, pr, pz) in enumerate(LAYOUT['ponds']):
    if pr >= 10: water_body('Pool_Deep', px, py, pr, pz, 450.0)      # 5 m deep swimming hole (walk on the bottom)
    else: water_body(f'Pond_{i + 1}', px, py, pr, pz)
for rv in LAYOUT['rivers']:
    river = spawn(unreal.RPGStylizedRiver, (0, 0, 0), label=rv['name'])
    for k, v in (('water_mesh', SM_STRIP), ('water_material', MI_RIVER), ('width', 800.0), ('piece_length', 600.0), ('waves', waves('River')),
                 ('flow_speed', 90.0), ('underwater_material', M_UNDER), ('caustic_material', M_CAUST), ('zone_depth', 250.0)):
        river.set_editor_property(k, v)
    spl = river.get_editor_property('spline'); spl.clear_spline_points(True)
    for i, (x, y, z) in enumerate(rv['points']):
        spl.add_spline_point(unreal.Vector(x * 100, YS * y * 100, z * 100), unreal.SplineCoordinateSpace.LOCAL, True)
        spl.set_scale_at_spline_point(i, unreal.Vector(1, (rv['width'] + 0.8) / 8.0, 1), True)   # a little wider than the channel: hidden in the banks
    river.rebuild()

# mist sheets over the lake and ponds
if EAL.does_asset_exist(f'{MESHES}/SM_Vista_MistSheet'): EAL.delete_asset(f'{MESHES}/SM_Vista_MistSheet')
PLANE = EAL.duplicate_asset('/Engine/BasicShapes/Plane', f'{MESHES}/SM_Vista_MistSheet')
PLANE.set_material(0, MMIST); unreal.EditorStaticMeshLibrary.remove_collisions(PLANE)
PLANE.get_editor_property('body_setup').set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
EAL.save_loaded_asset(PLANE)
for i, (cx, cy, r, z) in enumerate([(lx, ly, lr, LAYOUT['lake_z'])] + [tuple(p) for p in LAYOUT['ponds']]):
    X, Y = U(cx, cy)
    for k, (dz, s_) in enumerate(((30, 1.25), (90, 1.05))):
        mst = spawn(PLANE, (X, Y, z * 100 + dz), (k * 37, 0, 0), f'Mist_{i}_{k}', (r * s_, r * s_, 1))
        mst.static_mesh_component.set_material(0, MMIST); setp(mst.static_mesh_component, (('cast_shadow', False),), 'mist')
        mst.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)   # scatter traces must reach the ground

PATH = [unreal.Vector(*U(x, y), 0) for x, y in LAYOUT['path']]

def scatter(label, mesh, count, center=(0, 0), half=(9800, 9800), seed=1, scale=(0.8, 1.2), up=0.75, align=False, sink=0.0,
            path_r=0.0, patch=0.0, cover=0.5, collision=False, cull=0.0, material=None, margin=10.0, circles=(), shadow=True):
    a = spawn(unreal.RPGScatter, (center[0], center[1], 0), label=label)
    for k, v in (('mesh', mesh), ('count', count), ('half_extent', unreal.Vector2D(*half)), ('seed', seed), ('scale_range', unreal.Vector2D(*scale)),
                 ('min_up_normal', up), ('align_to_surface', align), ('sink_depth', sink), ('exclude_path', PATH), ('exclude_radius', path_r),
                 ('patch_size', patch), ('patch_coverage', cover), ('collision', collision), ('cull_distance', cull),
                 ('skip_water', True), ('water_margin', margin), ('exclude_circles', [unreal.Vector(*c_) for c_ in circles])):
        a.set_editor_property(k, v)
    if material: a.set_editor_property('material', material)
    a.get_editor_property('instances').set_editor_property('cast_shadow', shadow)   # small foliage: no shadows (VSM cost)
    a.rebuild(); unreal.log(f'SCATTER {label}: {a.get_editor_property("placed")}')
    return a

# ground cover first: its down-traces must hit terrain, not tree canopies or rocks (water zones already exist -> skipped)
scatter('Grass', M_['SM_Vista_Grass'], 60000, seed=21, scale=(0.8, 1.3), up=0.7, path_r=150, cull=9000, shadow=False)
scatter('GrassTall', M_['SM_Vista_GrassTall'], 14000, seed=22, scale=(0.8, 1.25), up=0.75, path_r=220, patch=2600, cover=0.5, cull=9000, shadow=False)
scatter('GrassShore', M_['SM_Vista_GrassTall'], 2500, (LX, LY), (lr * 170, lr * 170), 26, (0.9, 1.4), 0.7, False, 0, 0, 0, 0.5, False, 9000, None, 25.0, shadow=False)
WX, WY = U(-55, 22)
scatter('Wheat', M_['SM_Vista_Wheat'], 9000, (WX, WY), (2300, 1500), 23, (0.9, 1.3), 0.8, False, 0, 260, 0, 0.5, False, 9000, shadow=False)
scatter('Poppies', M_['SM_Vista_Poppies'], 12000, seed=24, scale=(1.1, 1.5), up=0.8, path_r=170, patch=1500, cover=0.4, cull=7000, shadow=False)
scatter('Daisies', M_['SM_Vista_Daisies'], 4000, seed=25, scale=(0.9, 1.2), up=0.8, path_r=160, patch=1100, cover=0.3, cull=6000, shadow=False)
# big things (collision on: trees and rocks block the player)
CA, CB = U(0, LAYOUT['cliff_y'] - 2)[1], U(0, LAYOUT['cliff_y'] + 8)[1]   # two rows along the ridge foot and slope
scatter('Cliffs_A', M_['SM_Vista_Cliff_A'], 9, (0, CA), (9500, 900), 3, (0.9, 1.4), 0.0, False, 120.0, 400, 0, 0.5, True)
scatter('Cliffs_B', M_['SM_Vista_Cliff_B'], 9, (0, CB), (9500, 900), 6, (0.8, 1.3), 0.0, False, 120.0, 400, 0, 0.5, True)
SPAWN_CLEAR = [(*U(-4, -72), 2400)]                                   # open ground at the player spawn + NPCs (add_vista_npcs.py)
# Blender rocks, every variant: (size class, count, scale, sink cm, path clearance cm, collision + shadow)
for k, (cls, count, sc, sink, pr, solid) in enumerate((('Large', 5, (0.8, 1.3), 30, 500, True), ('MediumLarge', 6, (0.8, 1.2), 20, 400, True),
                                                       ('Medium', 10, (0.8, 1.2), 12, 300, True), ('MediumSmall', 16, (0.8, 1.3), 6, 200, True),
                                                       ('SmallMedium', 30, (0.8, 1.4), 2, 120, False), ('Small', 30, (0.8, 1.5), 1, 100, False))):
    for j, v in enumerate('ABC'):
        scatter(f'Rocks_{cls}_{v}', ROCK(f'Rock_{cls}_{v}'), count, seed=60 + k * 3 + j, scale=sc, up=0.55, align=True, sink=sink, path_r=pr,
                collision=solid, shadow=solid, margin=-30.0, circles=SPAWN_CLEAR if solid else ())
for j, v in enumerate('ABC'):
    scatter(f'Pebbles_{v}', ROCK(f'Pebbles_{v}'), 25, seed=80 + j, scale=(0.9, 1.6), up=0.6, align=True, sink=1, path_r=60, shadow=False, margin=-30.0)
# stones along the lake shore
for j, v in enumerate('ABC'):
    scatter(f'ShoreRocks_{v}', ROCK(f'Rock_MediumSmall_{v}'), 8, (LX, LY), (lr * 150, lr * 150), 90 + j, (0.8, 1.4), 0.5, True, 6, 0, 0, 0.5, True,
            margin=-40.0)
spawn(TREE_ANCIENT, (LX - 2600, LY + YS * -900, ground(LX - 2600, LY + YS * -900) - 30), (40, 0, 0), 'Tree_Hero', (0.75, 0.75, 0.75))
# open ground around the lake and ponds so the water reads from the paths and cameras
OPEN = [(LX, LY, lr * 100 * 1.55)] + [(*U(px, py), pr * 100 * 1.9) for px, py, pr, pz in LAYOUT['ponds']]
for i, n in enumerate(('SM_Vista_Tree_Round', 'SM_Vista_Tree_Tall', 'SM_Vista_Tree_Wide')):
    scatter(f'Trees_{n[13:]}', M_[n], 26, seed=10 + i, scale=(0.85, 1.3), up=0.82, sink=15, path_r=450, patch=3500, cover=0.65, collision=True,
            margin=150.0, circles=OPEN)
# far forests on the surrounding hills (cheap blob trees, outside the vista square)
for i, (cx, cy_, hx, hy) in enumerate(((0, 28000, 45000, 17500), (0, -28000, 45000, 17500), (28000, 0, 17500, 10500), (-28000, 0, 17500, 10500))):
    scatter(f'FarTrees_{i}', M_['SM_Vista_Tree_Far'], 2500, (cx, cy_), (hx, hy), 40 + i, (1.0, 1.8), 0.8, False, 50, 0, 6000, 0.5, shadow=False)

# life: pollen / fireflies and drifting leaves (boxes 30 x 30 m around each actor)
for i, (x, y) in enumerate(((-3, -60), (lx, ly), (-20, 20))):
    X, Y = U(x, y); mo = spawn(M_['SM_Vista_Motes'], (X, Y, ground(X, Y)), (i * 50, 0, 0), f'Motes_{i}')
    mo.static_mesh_component.set_editor_property('cast_shadow', False)
for i, (x, y) in enumerate(((0, -45), (-20, 0), (30, -5))):
    X, Y = U(x, y); dl = spawn(M_['SM_Vista_DriftLeaves'], (X, Y, ground(X, Y)), (i * 70, 0, 0), f'DriftLeaves_{i}')
    dl.static_mesh_component.set_editor_property('cast_shadow', False)

# cameras (Visual.WorldShots / Visual.SkyShots)
def cam(label, eye_xy, eye_h, tgt_xy, tgt_h, fov=62.0, tag='WorldShot'):
    e = (eye_xy[0], eye_xy[1], ground(*eye_xy) + eye_h); t = (tgt_xy[0], tgt_xy[1], ground(*tgt_xy) + tgt_h)
    d = (t[0] - e[0], t[1] - e[1], t[2] - e[2])
    c = spawn(unreal.CameraActor, e, (math.degrees(math.atan2(d[1], d[0])), math.degrees(math.atan2(d[2], math.hypot(d[0], d[1]))), 0), label)
    c.tags = [unreal.Name(tag)]
    cc = c.get_component_by_class(unreal.CameraComponent); cc.set_editor_property('field_of_view', fov); cc.set_editor_property('constrain_aspect_ratio', False)
cam('WS_01_Path', U(-3, -75), 170, U(4, -20), 150)
cam('WS_02_Lake', U(20, -45), 300, U(lx, ly + 4), 0, 66)
cam('WS_03_Meadow', U(-32, -12), 90, U(-10, 28), 120, 58)
cam('WS_04_Overlook', U(-60, -95), 2300, U(10, 35), 0, 70)
cam('WS_05_Wheat', U(-30, 2), 160, U(-62, 25), 150, 60)
cam('WS_06_LakeShore', U(lx - 20, ly - 12), 120, U(lx + 8, ly + 6), 60, 70)
cam('WS_07_RiverA', U(73, 6), 260, U(60, 27), 0, 62)
cam('WS_08_PondWest', U(-70, 30), 180, U(-85, 44), 0, 64)
cam('WS_09_Wide', U(-20, -55), 900, U(40, 30), 300, 75)
DP = next(p for p in LAYOUT['ponds'] if p[2] >= 10)
cam('WS_15_DeepPool', U(DP[0] + 4, DP[1] + 19), 260, U(DP[0], DP[1] - 2), 0, 72)   # from behind the jump ledge
# aerial overviews of the whole vista
cam('WS_10_AerialSouth', U(-10, -150), 6000, U(10, 0), 0, 70)
cam('WS_11_AerialEast', U(150, -20), 5500, U(0, 10), 0, 70)
cam('WS_12_AerialNorthWest', U(-130, 110), 6500, U(10, -20), 0, 70)
cam('WS_13_TopDown', U(0, -40), 16000, U(0, -5), 0, 75)
# sky: one view toward the sunset / morning horizon, one up toward the night moon
SKY_EYE = U(20, -42)                                                   # open meadow south-west of the lake
cam('SKY_A_Sunset', SKY_EYE, 600, (SKY_EYE[0] + math.cos(math.radians(-40)) * 10000, SKY_EYE[1] + math.sin(math.radians(-40)) * 10000), 1500, 80, 'SkyShot')
cam('SKY_B_Overhead', SKY_EYE, 2500, (SKY_EYE[0] + math.cos(math.radians(230)) * 10000, SKY_EYE[1] + math.sin(math.radians(230)) * 10000), 9000, 85, 'SkyShot')
spawn(unreal.PlayerStart, (U(-4, -85)[0], U(-4, -85)[1], ground(*U(-4, -85)) + 120), label='PlayerStart')
# everything whose material follows the day/night cycle
cyc.set_editor_property('sky_receivers', [a for a in eas.get_all_level_actors() if a.get_actor_label().startswith(('SkyDome', 'Mist_', 'Motes_'))])
cyc.set_time_of_day(17.3)
les.save_current_level()
unreal.log('VISTA_BUILT')
# the NPCs from L_DummyField + navigation bounds (copies them in and saves the vista again)
exec(open('C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/add_vista_npcs.py').read())
unreal.SystemLibrary.quit_editor()
