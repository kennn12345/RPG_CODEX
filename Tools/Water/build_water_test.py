"""Builds the stylized water (materials + river/lake) and the test map /Game/RPG/Maps/L_WaterTest.
Look target: POLYART-style stylized water (bold cellular foam, clear shallows, river streaks, sun glints) with colours
between the approved concept (green shallows) and the video (turquoise -> deep blue). The startup map is not touched.
Run in the full editor (it saves and closes the editor):
  UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="C:/UNREAL PROJECTS/RPG_CODEX/Tools/Water/build_water_test.py" -unattended
Then photograph it:  -ExecCmds="Automation RunTests Visual.WaterShots" (see RPGWaterShots.cpp)."""
import unreal, math, os

SRC = 'C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs'
ROOT = '/Game/RPG/Environment'
WATER, TERRAIN, PROPS, TEX = ROOT + '/Water', ROOT + '/TestTerrain', ROOT + '/Props', ROOT + '/Textures'
MAP = '/Game/RPG/Maps/L_WaterTest'
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
LAKE_X = 9000.0

def lin(c):
    return [((v / 255) / 12.92) if v / 255 <= .04045 else ((v / 255 + .055) / 1.055) ** 2.4 for v in c]

# ------------------------------------------------------------------ import
unreal.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')   # legacy FBX options below

def import_file(path, dest, name=None, scale=1.0):
    old = f"{dest}/{name or os.path.splitext(os.path.basename(path))[0]}"
    if EAL.does_asset_exist(old): EAL.delete_asset(old)          # fresh import: re-import keeps stale settings
    t = unreal.AssetImportTask()
    t.filename = path; t.destination_path = dest; t.automated = True; t.replace_existing = True; t.save = True
    if name: t.destination_name = name
    if path.lower().endswith('.fbx'):
        o = unreal.FbxImportUI()
        o.import_mesh = True; o.import_materials = False; o.import_textures = False; o.import_as_skeletal = False
        sm = o.static_mesh_import_data
        sm.combine_meshes = True; sm.auto_generate_collision = False
        sm.vertex_color_import_option = unreal.VertexColorImportOption.REPLACE
        sm.generate_lightmap_u_vs = False
        sm.import_uniform_scale = scale
        t.options = o
    TOOLS.import_asset_tasks([t])
    paths = list(t.imported_object_paths)
    if not paths: raise RuntimeError('import failed: ' + path)
    return unreal.load_asset(paths[0])

def tex(path, name, srgb=True):
    a = import_file(path, TEX, name); a.set_editor_property('srgb', srgb); EAL.save_loaded_asset(a); return a

T_EARTH = tex(SRC + '/Earth_R01/textures/Earth_Rich_Brown_BaseColor.png', 'T_Earth_Rich_Brown_BaseColor')
T_STONE = tex(SRC + '/Rocks_R01/textures/Stone_Painted_BaseColor.png', 'T_Stone_Painted_BaseColor')
T_BLADE = tex(SRC + '/Grass_R01/textures/Grass_Blade_BaseColor.png', 'T_Grass_Blade_BaseColor')
T_BARK = tex(SRC + '/Trees_R01/textures/Bark_Painted_BaseColor.png', 'T_Bark_Painted_BaseColor')
T_MOSS = tex(SRC + '/Trees_R01/textures/Moss_Painted_BaseColor.png', 'T_Moss_Painted_BaseColor')
T_LEAF = tex(SRC + '/Trees_R01/textures/Leaf_Maple_Atlas_BaseColor.png', 'T_Leaf_Maple_Atlas_BaseColor')

# the strip pre-turned in Blender (export_river_strip_x.py) so it lies along +X, the river actor's forward axis
if EAL.does_asset_exist(WATER + '/SM_Water_River_Straight10'): EAL.delete_asset(WATER + '/SM_Water_River_Straight10')   # superseded import
SM_RIVER = import_file(SRC + '/Water_R01/exports/unreal/SM_Water_River_Strip10.fbx', WATER)
SM_LAKE = import_file(SRC + '/Water_R01/exports/SM_Water_Lake.fbx', WATER)
SM_TR = import_file(SRC + '/Water_R01/exports/test_scene/SM_TestTerrain_River.fbx', TERRAIN)
SM_TL = import_file(SRC + '/Water_R01/exports/test_scene/SM_TestTerrain_Lake.fbx', TERRAIN)
SM_OCEAN = import_file(SRC + '/Water_R01/exports/unreal/SM_Water_OceanTile.fbx', WATER)
SM_BEACH = import_file(SRC + '/Water_R01/exports/unreal/SM_TestTerrain_Beach.fbx', TERRAIN)
_b = SM_TR.get_bounding_box()
YS = -1.0 if (_b.min.y + _b.max.y) < 0 else 1.0   # the FBX importer mirrors Blender's Y
# Rocks_R01 FBXs come in at 1/100 scale (unit setting in that export - to fix at the source); scaled here for the test map
ROCKS = {n: import_file(f'{SRC}/Rocks_R01/exports/SM_{n}.fbx', PROPS, scale=100.0) for n in
         ('Rock_MediumLarge_B', 'Rock_Medium_A', 'Rock_MediumSmall_B', 'Rock_MediumSmall_C', 'Rock_SmallMedium_A', 'Pebbles_A')}
GRASS = {n: import_file(f'{SRC}/Grass_R01/exports/SM_{n}.fbx', PROPS) for n in ('Grass_Tall', 'Grass_Medium', 'Grass_Short')}
TREES = {n: import_file(f'{SRC}/Trees_R01/exports/SM_{n}.fbx', PROPS) for n in ('Tree_Forest_01', 'Tree_Forest_02', 'Tree_Forest_03')}
for m in (SM_TR, SM_TL, SM_BEACH):   # walkable test terrain: per-triangle collision
    m.get_editor_property('body_setup').set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)

# ------------------------------------------------------------------ material helpers
def new_material(name, folder):
    path = f'{folder}/{name}'
    if EAL.does_asset_exist(path): EAL.delete_asset(path)
    return TOOLS.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())

def node(mat, cls, x, y, **props):
    e = MEL.create_material_expression(mat, cls, x, y)
    for k, v in props.items(): e.set_editor_property(k, v)
    return e

def scalar(mat, name, value, x, y):
    return node(mat, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)

def vector(mat, name, rgb, x, y):
    return node(mat, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(*rgb, 1.0))

def custom(mat, code, inputs, out_type, extra, x, y):
    c = node(mat, unreal.MaterialExpressionCustom, x, y, code=code, output_type=out_type)
    ins = []
    for n_ in inputs:
        ci = unreal.CustomInput(); ci.set_editor_property('input_name', n_); ins.append(ci)
    c.set_editor_property('inputs', ins)
    outs = []
    for n_, t in extra:
        co = unreal.CustomOutput(); co.set_editor_property('output_name', n_); co.set_editor_property('output_type', t); outs.append(co)
    c.set_editor_property('additional_outputs', outs)
    return c

def link(a, a_out, b, b_in): MEL.connect_material_expressions(a, a_out, b, b_in)

# shared HLSL helpers (value noise, fbm, cellular F1 / F2-F1)
HELP = r'''
struct WF {
  float h(float2 p){ return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
  float n(float2 p){ float2 i = floor(p), f = frac(p); float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(h(i), h(i + float2(1, 0)), u.x), lerp(h(i + float2(0, 1)), h(i + float2(1, 1)), u.x), u.y); }
  float fbm(float2 p){ return 0.55 * n(p) + 0.30 * n(p * 2.03 + 7.1) + 0.15 * n(p * 4.1 + 3.3); }
  float2 cell(float2 p, float t){ float2 i = floor(p), f = frac(p); float m1 = 8.0, m2 = 8.0;
    for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) {
      float2 g = float2(x, y); float2 o = float2(h(i + g), h(i + g + 19.7)); o = 0.5 + 0.45 * sin(t + 6.2831 * o);
      float d = length(g + o - f); if (d < m1) { m2 = m1; m1 = d; } else if (d < m2) { m2 = d; } }
    return float2(m1, m2 - m1); }
};
WF w;
'''

# ------------------------------------------------------------------ the stylized water material
WATER_CODE = HELP + r'''
float depth = max(SceneD - PixD, 0.0) * max(abs(CamZ), 0.25);          // approx. vertical water depth, cm
float d = saturate(depth / DeepDepth);
float3 c = lerp(ShallowC, MidC, smoothstep(0.0, 0.42, d));
c = lerp(c, DeepC, smoothstep(0.42, 1.0, d));
// metres: river = flow UV (U across, V downstream, 5 m per unit) panned downstream; lake = world XY drifting slowly
float2 p = IsRiver > 0.5 ? float2(UV.x * 5.0, (VOff + VDir * UV.y) * 5.0 - T * FlowSpeed) : WP.xy / 100.0 * 0.5 + float2(T * 0.03, T * 0.02);
// river: thin white streaks lying along the current;  lake: rare small glints
float streak;
if (IsRiver > 0.5) {
  float s = w.n(float2(p.x * 6.0, p.y * 0.9)) * w.n(float2(p.x * 2.6 + 3.0, p.y * 0.45 + T * 0.15));   // short thin dashes
  streak = smoothstep(0.47, 0.50, s);
} else {
  streak = 0.0;                                                     // calm lake: no streaks (sun glints come from specular)
}
// bold cellular foam wherever anything touches the water: shallow water (depth) OR close to any surface (mesh distance
// fields - gives a thick ring around steep rocks/posts that depth alone would miss); opaque white with dark holes
float fd = max(saturate(1.0 - depth / FoamWidth), saturate(1.0 - DistSurf / RockFoamWidth));
float2 cl = w.cell(WP.xy / FoamCell, T * 1.3);
float fn = w.fbm(WP.xy / 150.0 + T * 0.10);
float foam = saturate((fd * 1.25 + fn * 0.85 - 0.95) / 0.035) * smoothstep(0.10, 0.19, cl.x);
// shore waves: foam lines on depth contours that keep moving toward the shore, broken up, fading out in deep water
float sw = 0.0;
if (ShoreWaves > 0.0) {
  float ph = depth / ShoreSpacing + T * ShoreSpeed;
  float band = 1.0 - smoothstep(0.0, 0.16, frac(ph));
  float range = saturate(1.0 - depth / ShoreRange) * saturate(depth / 6.0);
  sw = band * range * smoothstep(0.35, 0.55, w.fbm(WP.xy / 90.0 + T * 0.05)) * smoothstep(0.08, 0.16, cl.x) * ShoreWaves;
}
// crest foam on the tallest waves (cellular, broken)
float cf = CrestFoam * smoothstep(0.56, 0.66, Crest + (fn - 0.5) * 0.3) * smoothstep(0.10, 0.19, cl.x);
float white = max(max(foam, sw), max(cf, streak * 0.95));
Opacity = lerp(lerp(ShallowOpacity, DeepOpacity, smoothstep(0.0, 0.9, d)), 1.0, white);
Emiss = white * float3(0.06, 0.07, 0.08);
float e = 0.12; float2 q = p * 1.4;
float n0 = w.fbm(q), nx = w.fbm(q + float2(e, 0)), ny = w.fbm(q + float2(0, e));
float3 det = float3((n0 - nx) * NormalStrength, (n0 - ny) * NormalStrength, 1.0);
Nrm = normalize(float3(det.xy + WN.xy, det.z * WN.z));               // detail ripples on top of the big waves
Rough = lerp(0.04, 0.6, white);
return lerp(c, float3(0.95, 0.97, 1.0), white);
'''

# Gerstner waves - MUST match RPGWaterWaves.h (same 4 direction / length / height ratios)
WAVE_COMMON = r'''
float dirs[4] = { 0.0, 35.0, -28.0, 70.0 };
float lens[4] = { 1.0, 0.62, 0.41, 0.27 };
float amps[4] = { 1.0, 0.55, 0.35, 0.20 };
'''
WAVE_WPO = WAVE_COMMON + r'''
float3 o = float3(0, 0, 0);
if (WaveHeight <= 0.0) return o;
for (int i = 0; i < 4; i++) {
  float a = radians(WaveDir + dirs[i]); float2 dd = float2(cos(a), sin(a));
  float k = 6.28318 / max(WaveLength * lens[i], 1.0); float om = sqrt(980.0 * k) * WaveSpeed; float A = WaveHeight * amps[i];
  float ph = k * dot(dd, WP.xy) - om * T; float Q = Steep / (k * A * 4.0 + 0.0001);
  o.xy += dd * (Q * A * cos(ph)); o.z += A * sin(ph);
}
return o;
'''
WAVE_NORMAL = WAVE_COMMON + r'''
float2 g = float2(0, 0); float z = 0.0;
if (WaveHeight > 0.0) {
  for (int i = 0; i < 4; i++) {
    float a = radians(WaveDir + dirs[i]); float2 dd = float2(cos(a), sin(a));
    float k = 6.28318 / max(WaveLength * lens[i], 1.0); float om = sqrt(980.0 * k) * WaveSpeed; float A = WaveHeight * amps[i];
    float ph = k * dot(dd, WP.xy) - om * T;
    g += dd * (A * k * cos(ph)); z += A * sin(ph);
  }
}
Crest = WaveHeight > 0.0 ? z / (WaveHeight * 2.1) : 0.0;     // -1 .. 1
return normalize(float3(-g.x, -g.y, 1.0));                     // world space
'''

def build_water_material():
    m = new_material('M_StylizedWater', WATER)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_SINGLE_LAYER_WATER)
    m.set_editor_property('used_with_spline_meshes', True)
    m.set_editor_property('two_sided', True)                      # visible from underwater too
    ins = ['SceneD', 'PixD', 'CamZ', 'UV', 'VOff', 'VDir', 'WP', 'T', 'IsRiver', 'FlowSpeed', 'FoamWidth', 'FoamCell',
           'ShallowC', 'MidC', 'DeepC', 'DeepDepth', 'ShallowOpacity', 'DeepOpacity', 'NormalStrength',
           'DistSurf', 'RockFoamWidth', 'ShoreWaves', 'ShoreSpacing', 'ShoreSpeed', 'ShoreRange', 'Crest', 'CrestFoam', 'WN']
    F1, F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT1, unreal.CustomMaterialOutputType.CMOT_FLOAT3
    c = custom(m, WATER_CODE, ins, F3, [('Opacity', F1), ('Emiss', F3), ('Nrm', F3), ('Rough', F1)], 0, 0)
    y = -900
    def feed(expr, out, name):
        link(expr, out, c, name)
    # ---- waves: one parameter set feeds the vertex offset and the pixel normal / crest
    wp_abs = node(m, unreal.MaterialExpressionWorldPosition, -1500, -300)
    t_node = node(m, unreal.MaterialExpressionTime, -1500, -220)
    wparams = {n_: scalar(m, n_, v, -1500, -140 + 80 * i) for i, (n_, v) in enumerate(
        (('WaveHeight', 0.0), ('WaveLength', 1200.0), ('WaveSpeed', 1.0), ('WaveDir', 0.0), ('Steep', 0.6)))}
    wave_ins = ['WP', 'T', 'WaveHeight', 'WaveLength', 'WaveSpeed', 'WaveDir', 'Steep']
    wpo = custom(m, WAVE_WPO, wave_ins, F3, [], -1100, -400)
    wn = custom(m, WAVE_NORMAL, wave_ins[:-1], F3, [('Crest', F1)], -1100, -250)
    for cc in (wpo, wn):
        link(wp_abs, '', cc, 'WP'); link(t_node, '', cc, 'T')
        for n_ in wave_ins[2:]:
            if n_ in [i.get_editor_property('input_name') for i in cc.get_editor_property('inputs')]: link(wparams[n_], '', cc, n_)
    MEL.connect_material_property(wpo, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    tr = node(m, unreal.MaterialExpressionTransform, -800, -250)
    tr.set_editor_property('transform_source_type', unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    tr.set_editor_property('transform_type', unreal.MaterialVectorCoordTransform.TRANSFORM_TANGENT)
    link(wn, '', tr, ''); feed(tr, '', 'WN'); feed(wn, 'Crest', 'Crest')
    feed(node(m, unreal.MaterialExpressionDistanceToNearestSurface, -900, -1000), '', 'DistSurf')
    feed(node(m, unreal.MaterialExpressionSceneDepthWithoutWater, -900, y), '', 'SceneD'); y += 80
    feed(node(m, unreal.MaterialExpressionPixelDepth, -900, y), '', 'PixD'); y += 80
    cv = node(m, unreal.MaterialExpressionCameraVectorWS, -1100, y)
    cz = node(m, unreal.MaterialExpressionComponentMask, -900, y, r=False, g=False, b=True, a=False); link(cv, '', cz, ''); feed(cz, '', 'CamZ'); y += 80
    feed(node(m, unreal.MaterialExpressionTextureCoordinate, -900, y), '', 'UV'); y += 80
    cpd = scalar(m, 'FlowUVOffset', 0.0, -900, y); cpd.set_editor_property('use_custom_primitive_data', True)
    cpd.set_editor_property('primitive_data_index', 0); feed(cpd, '', 'VOff'); y += 80   # set per river piece by ARPGStylizedRiver
    cpd1 = scalar(m, 'FlowUVDirection', 1.0, -900, y); cpd1.set_editor_property('use_custom_primitive_data', True)
    cpd1.set_editor_property('primitive_data_index', 1); feed(cpd1, '', 'VDir'); y += 80
    feed(node(m, unreal.MaterialExpressionWorldPosition, -900, y), '', 'WP'); y += 80
    feed(node(m, unreal.MaterialExpressionTime, -900, y), '', 'T'); y += 80
    for name, val in (('IsRiver', 1.0), ('FlowSpeed', 0.9), ('FoamWidth', 28.0), ('FoamCell', 22.0), ('DeepDepth', 170.0),
                      ('ShallowOpacity', 0.10), ('DeepOpacity', 0.86), ('NormalStrength', 2.2), ('RockFoamWidth', 45.0),
                      ('ShoreWaves', 0.0), ('ShoreSpacing', 70.0), ('ShoreSpeed', 0.35), ('ShoreRange', 160.0), ('CrestFoam', 0.0)):
        feed(scalar(m, name, val, -900, y), '', name); y += 80
    # palette: concept green shallows -> turquoise -> the video's deep blue
    for name, rgb in (('ShallowC', (120, 190, 110)), ('MidC', (25, 175, 175)), ('DeepC', (12, 82, 150))):
        feed(vector(m, name, lin(rgb), -900, y), '', name); y += 120
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(c, 'Opacity', unreal.MaterialProperty.MP_OPACITY)
    MEL.connect_material_property(c, 'Emiss', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(c, 'Nrm', unreal.MaterialProperty.MP_NORMAL)
    MEL.connect_material_property(c, 'Rough', unreal.MaterialProperty.MP_ROUGHNESS)
    spec = node(m, unreal.MaterialExpressionConstant, -300, 400, r=0.6); MEL.connect_material_property(spec, '', unreal.MaterialProperty.MP_SPECULAR)
    out = node(m, unreal.MaterialExpressionSingleLayerWaterMaterialOutput, 400, 200)
    sc = vector(m, 'Scattering', (0.02, 0.07, 0.07), 0, 300); ab = vector(m, 'Absorption', (0.35, 0.06, 0.05), 0, 420)
    link(sc, '', out, 'Scattering Coefficients'); link(ab, '', out, 'Absorption Coefficients')
    MEL.recompile_material(m); EAL.save_loaded_asset(m)
    insts = {}
    for name, params in INSTANCES.items():
        path = f'{WATER}/{name}'
        if EAL.does_asset_exist(path): EAL.delete_asset(path)
        mi = TOOLS.create_asset(name, WATER, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mi, m)
        for k, v in params.items(): MEL.set_material_instance_scalar_parameter_value(mi, k, v)
        EAL.save_loaded_asset(mi); insts[name] = mi
    return insts

# per water type; wave values MUST equal the matching zone's FRPGWaveSettings (set in the level section below)
WAVES = {'River': dict(Height=2.0, Length=400.0, Speed=0.8, Direction=0.0),
         'Lake': dict(Height=3.0, Length=550.0, Speed=0.8, Direction=30.0),
         'Ocean': dict(Height=30.0, Length=1500.0, Speed=1.0, Direction=90.0 * -YS)}   # rolling toward the beach (land at Unreal Y sign -YS)
def wave_params(kind):
    w_ = WAVES[kind]; return dict(WaveHeight=w_['Height'], WaveLength=w_['Length'], WaveSpeed=w_['Speed'], WaveDir=w_['Direction'])
INSTANCES = {
    'MI_StylizedWater_River': dict(IsRiver=1.0, **wave_params('River')),
    'MI_StylizedWater_Lake': dict(IsRiver=0.0, NormalStrength=1.2, ShoreWaves=0.45, ShoreSpacing=45.0, ShoreRange=90.0, **wave_params('Lake')),
    'MI_StylizedWater_Ocean': dict(IsRiver=0.0, NormalStrength=1.6, ShoreWaves=1.0, CrestFoam=1.0, DeepDepth=420.0, FoamWidth=34.0,
                                   **wave_params('Ocean')),
}

# ------------------------------------------------------------------ underwater post-process + caustic decal
UNDERWATER_CODE = HELP + r'''
float f = 1.0 - exp(-Dep / FogDist);
float3 c = lerp(Col * Tint, FogColor, saturate(f));
float x = UV.x * 7.0 + UV.y * 1.8;                                            // slanted light rays from the surface
float r = w.n(float2(x, T * 0.15)) * w.n(float2(x * 0.5 + 3.0, T * 0.1));
r = smoothstep(0.22, 0.6, r) * pow(saturate(1.0 - UV.y), 1.5) * RayStrength;
return c + FogColor * 2.2 * r * (1.0 - 0.5 * f);
'''
CAUSTIC_CODE = HELP + r'''
float below = saturate((SurfaceZ - WP.z) / 10.0);
float dep = max(SurfaceZ - WP.z, 0.0);
float2 a = w.cell(WP.xy / 38.0 + float2(T * 0.05, 0), T * 0.9), b = w.cell(WP.xy / 29.0 - float2(0, T * 0.04) + 11.3, T * 0.7);
float lines = (1.0 - smoothstep(0.0, 0.035, a.y)) * 0.6 + (1.0 - smoothstep(0.0, 0.03, b.y)) * 0.4;
return saturate(lines * below * saturate(1.0 - dep / 300.0) * Strength);
'''

def build_underwater_materials():
    F3, F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT3, unreal.CustomMaterialOutputType.CMOT_FLOAT1
    pp = new_material('M_UnderwaterPP', WATER)
    pp.set_editor_property('material_domain', unreal.MaterialDomain.MD_POST_PROCESS)
    c = custom(pp, UNDERWATER_CODE, ['Col', 'Dep', 'UV', 'T', 'FogColor', 'FogDist', 'Tint', 'RayStrength'], F3, [], 0, 0)
    st = node(pp, unreal.MaterialExpressionSceneTexture, -600, -200, scene_texture_id=unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    sd = node(pp, unreal.MaterialExpressionSceneTexture, -600, -60, scene_texture_id=unreal.SceneTextureId.PPI_SCENE_DEPTH)
    mk = node(pp, unreal.MaterialExpressionComponentMask, -400, -60, r=True, g=False, b=False, a=False); link(sd, 'Color', mk, '')
    link(st, 'Color', c, 'Col'); link(mk, '', c, 'Dep')
    link(node(pp, unreal.MaterialExpressionScreenPosition, -600, 80), 'ViewportUV', c, 'UV')
    link(node(pp, unreal.MaterialExpressionTime, -600, 180), '', c, 'T')
    link(vector(pp, 'FogColor', lin((40, 150, 160)), -600, 260), '', c, 'FogColor')
    link(scalar(pp, 'FogDist', 900.0, -600, 380), '', c, 'FogDist')
    link(vector(pp, 'Tint', (0.75, 1.0, 1.0), -600, 460), '', c, 'Tint')
    link(scalar(pp, 'RayStrength', 0.9, -600, 580), '', c, 'RayStrength')
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(pp); EAL.save_loaded_asset(pp)

    dm = new_material('M_WaterCaustics', WATER)
    dm.set_editor_property('material_domain', unreal.MaterialDomain.MD_DEFERRED_DECAL)
    dm.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    c2 = custom(dm, CAUSTIC_CODE, ['WP', 'T', 'SurfaceZ', 'Strength'], F1, [], 0, 0)
    link(node(dm, unreal.MaterialExpressionWorldPosition, -600, 0), '', c2, 'WP')
    link(node(dm, unreal.MaterialExpressionTime, -600, 100), '', c2, 'T')
    link(scalar(dm, 'SurfaceZ', 0.0, -600, 200), '', c2, 'SurfaceZ')
    link(scalar(dm, 'Strength', 0.5, -600, 300), '', c2, 'Strength')
    col = node(dm, unreal.MaterialExpressionConstant3Vector, -300, -150, constant=unreal.LinearColor(1.0, 0.86, 0.45, 1))
    MEL.connect_material_property(col, '', unreal.MaterialProperty.MP_BASE_COLOR)
    em = node(dm, unreal.MaterialExpressionMultiply, -150, 150); link(col, '', em, 'A'); link(c2, '', em, 'B')
    MEL.connect_material_property(em, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(c2, '', unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(dm); EAL.save_loaded_asset(dm)
    return pp, dm

# ------------------------------------------------------------------ terrain / props materials (test map)
TERRAIN_CODE = HELP + r'''
float wz = WaterZ0 + WaterSlope * WP.y;
float below = saturate((wz - WP.z) / 8.0);
float dep = max(wz - WP.z, 0.0);
float3 c = Tex * Tint;
c = lerp(c, c * float3(0.80, 1.05, 0.70) + float3(0.03, 0.05, 0.01), below);          // olive silt under water
float2 a = w.cell(WP.xy / 38.0 + float2(T * 0.05, 0), T * 0.9), b = w.cell(WP.xy / 29.0 - float2(0, T * 0.04) + 11.3, T * 0.7);
float lines = (1.0 - smoothstep(0.0, 0.035, a.y)) * 0.45 + (1.0 - smoothstep(0.0, 0.03, b.y)) * 0.3;
float fade = below * saturate(1.0 - dep / 120.0) * 0.7;
return c + float3(0.95, 0.80, 0.25) * lines * fade * CausticStrength;
'''

def build_terrain_material():
    m = new_material('M_TestTerrain_Earth', TERRAIN)
    wp = node(m, unreal.MaterialExpressionWorldPosition, -1100, 0)
    mask = node(m, unreal.MaterialExpressionComponentMask, -950, 0, r=True, g=True, b=False, a=False); link(wp, '', mask, '')
    div = node(m, unreal.MaterialExpressionDivide, -800, 0, const_b=200.0); link(mask, '', div, 'A')
    ts = node(m, unreal.MaterialExpressionTextureSample, -650, 0, texture=T_EARTH); link(div, '', ts, 'UVs')
    F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
    c = custom(m, TERRAIN_CODE, ['Tex', 'WP', 'T', 'WaterZ0', 'WaterSlope', 'CausticStrength', 'Tint'], F3, [], -300, 0)
    link(vector(m, 'Tint', (1.0, 1.0, 1.0), -650, 560), '', c, 'Tint')
    link(ts, 'RGB', c, 'Tex'); link(wp, '', c, 'WP'); link(node(m, unreal.MaterialExpressionTime, -650, 200), '', c, 'T')
    for i, (n_, v) in enumerate((('WaterZ0', 0.0), ('WaterSlope', 0.0), ('CausticStrength', 0.0))):   # caustics: zone decals
        link(scalar(m, n_, v, -650, 300 + 80 * i), '', c, n_)
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
    r = node(m, unreal.MaterialExpressionConstant, -300, 200, r=0.9); MEL.connect_material_property(r, '', unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(m); EAL.save_loaded_asset(m)
    return m

def simple_material(name, texture, tint=(1, 1, 1), vertex_color=False, gain=1.0, masked=False, two_sided=False, uv_world=False):
    m = new_material(name, PROPS)
    if masked: m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    m.set_editor_property('two_sided', two_sided)
    ts = node(m, unreal.MaterialExpressionTextureSample, -700, 0, texture=texture)
    if uv_world:
        wp = node(m, unreal.MaterialExpressionWorldPosition, -1100, 0)
        mk = node(m, unreal.MaterialExpressionComponentMask, -950, 0, r=True, g=True, b=False, a=False); link(wp, '', mk, '')
        dv = node(m, unreal.MaterialExpressionDivide, -850, 0, const_b=160.0); link(mk, '', dv, 'A'); link(dv, '', ts, 'UVs')
    col = node(m, unreal.MaterialExpressionMultiply, -450, 0, const_b=gain); link(ts, 'RGB', col, 'A')
    tn = node(m, unreal.MaterialExpressionMultiply, -300, 0); link(col, '', tn, 'A')
    link(node(m, unreal.MaterialExpressionConstant3Vector, -450, 150, constant=unreal.LinearColor(*tint, 1)), '', tn, 'B')
    last = tn
    if vertex_color:
        vc = node(m, unreal.MaterialExpressionVertexColor, -450, 260)
        mv = node(m, unreal.MaterialExpressionMultiply, -150, 0); link(tn, '', mv, 'A'); link(vc, '', mv, 'B'); last = mv
    MEL.connect_material_property(last, '', unreal.MaterialProperty.MP_BASE_COLOR)
    if masked: MEL.connect_material_property(ts, 'A', unreal.MaterialProperty.MP_OPACITY_MASK)
    r = node(m, unreal.MaterialExpressionConstant, -300, 300, r=0.85); MEL.connect_material_property(r, '', unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(m); EAL.save_loaded_asset(m)
    return m

W = build_water_material()
M_UNDERWATER, M_CAUSTICS = build_underwater_materials()
M_TERRAIN = build_terrain_material()
M_ROCK = simple_material('M_TestRock', T_STONE, tint=(.55, .62, .78), uv_world=True)
M_GRASS = simple_material('M_TestGrass', T_BLADE, vertex_color=True, gain=1.45, two_sided=True)
M_BARK = simple_material('M_TestBark', T_BARK)
M_LEAF = simple_material('M_TestLeaf', T_LEAF, vertex_color=True, gain=1.25, masked=True, two_sided=True)
for mesh in ROCKS.values(): mesh.set_material(0, M_ROCK)
for mesh in GRASS.values(): mesh.set_material(0, M_GRASS)
for mesh in TREES.values():
    mesh.set_material(0, M_BARK)
    if len(mesh.static_materials) > 1: mesh.set_material(1, M_LEAF)
SM_RIVER.set_material(0, W['MI_StylizedWater_River']); SM_LAKE.set_material(0, W['MI_StylizedWater_Lake'])
SM_OCEAN.set_material(0, W['MI_StylizedWater_Ocean'])
for a in list(ROCKS.values()) + list(GRASS.values()) + list(TREES.values()) + [SM_RIVER, SM_LAKE, SM_OCEAN, SM_TR, SM_TL, SM_BEACH]:
    EAL.save_loaded_asset(a)

# ------------------------------------------------------------------ level
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if EAL.does_asset_exist(MAP):
    les.load_level(MAP)
    for a in eas.get_all_level_actors():
        if not isinstance(a, (unreal.WorldSettings, unreal.Brush)): eas.destroy_actor(a)
else:
    les.new_level(MAP)
world = unreal.EditorLevelLibrary.get_editor_world()

def spawn(cls_or_asset, loc, rot=(0, 0, 0), label=None, scale=None):
    r = unreal.Rotator(roll=rot[2], pitch=rot[1], yaw=rot[0])
    a = eas.spawn_actor_from_object(cls_or_asset, unreal.Vector(*loc), r) if not isinstance(cls_or_asset, type) \
        else eas.spawn_actor_from_class(cls_or_asset, unreal.Vector(*loc), r)
    if label: a.set_actor_label(label)
    if scale: a.set_actor_scale3d(unreal.Vector(*scale))
    return a

# lighting: sun + sky atmosphere + realtime sky light + light fog, gentle fixed-ish exposure
sun = spawn(unreal.DirectionalLight, (0, 0, 2000), (130, -42, 0), 'Sun')
sc = sun.get_component_by_class(unreal.DirectionalLightComponent)
sc.set_editor_property('intensity', 7.0); sc.set_editor_property('atmosphere_sun_light', True)
sc.set_editor_property('light_color', unreal.Color(255, 244, 226, 255))
spawn(unreal.SkyAtmosphere, (0, 0, 0), label='SkyAtmosphere')
sky = spawn(unreal.SkyLight, (0, 0, 500), label='SkyLight')
skc = sky.get_component_by_class(unreal.SkyLightComponent); skc.set_editor_property('real_time_capture', True); skc.set_editor_property('intensity', 1.2)
spawn(unreal.ExponentialHeightFog, (0, 0, -200), label='Fog')
pp = spawn(unreal.PostProcessVolume, (0, 0, 0), label='PostProcess'); pp.set_editor_property('unbound', True)
s = pp.get_editor_property('settings')
s.set_editor_property('override_auto_exposure_bias', True); s.set_editor_property('auto_exposure_bias', 0.6)
s.set_editor_property('override_bloom_intensity', True); s.set_editor_property('bloom_intensity', 0.9)
pp.set_editor_property('settings', s)

# terrain: river channel (FBX keeps Blender coords; check whether Y came in mirrored) and lake basin
river_t = spawn(SM_TR, (0, 0, 0), label='Terrain_River')
lake_t = spawn(SM_TL, (LAKE_X, 0, 0), label='Terrain_Lake')
for actor, z0, slope in ((river_t, 60.0, -60.0 / 7000.0 * YS), (lake_t, 0.0, 0.0)):
    mi = TOOLS.create_asset(f'MI_{actor.get_actor_label()}', TERRAIN, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew()) \
        if not EAL.does_asset_exist(f'{TERRAIN}/MI_{actor.get_actor_label()}') else unreal.load_asset(f'{TERRAIN}/MI_{actor.get_actor_label()}')
    MEL.set_material_instance_parent(mi, M_TERRAIN)
    MEL.set_material_instance_scalar_parameter_value(mi, 'WaterZ0', z0)
    MEL.set_material_instance_scalar_parameter_value(mi, 'WaterSlope', slope)
    EAL.save_loaded_asset(mi)
    actor.static_mesh_component.set_material(0, mi)

# river: spline through the Blender demo centreline (m -> cm), width per point via spline point scale Y
DEMO = ((0, 0, .6), (6, 12, .55), (2, 26, .45), (10, 38, .35), (22, 44, .2), (30, 56, .1), (28, 70, 0))
DEMO_W = (7.0, 8.0, 9.5, 9.0, 8.0, 8.5, 7.5)
river = spawn(unreal.RPGStylizedRiver, (0, 0, 0), label='River')
river.set_editor_property('water_mesh', SM_RIVER); river.set_editor_property('water_material', W['MI_StylizedWater_River'])
river.set_editor_property('width', 800.0)

def waves(kind):
    w_ = WAVES[kind]; s_ = unreal.RPGWaveSettings()
    s_.height, s_.length, s_.speed, s_.direction = w_['Height'], w_['Length'], w_['Speed'], w_['Direction']
    return s_

river.set_editor_property('waves', waves('River'))
river.set_editor_property('flow_speed', 90.0)                     # = material FlowSpeed 0.9 m/s
river.set_editor_property('underwater_material', M_UNDERWATER); river.set_editor_property('caustic_material', M_CAUSTICS)
spl = river.get_editor_property('spline'); spl.clear_spline_points(True)
for i, (x, y, z) in enumerate(DEMO):
    spl.add_spline_point(unreal.Vector(x * 100, YS * y * 100, z * 100), unreal.SplineCoordinateSpace.LOCAL, True)
    spl.set_scale_at_spline_point(i, unreal.Vector(1, DEMO_W[i] / 8.0, 1), True)
river.rebuild()

def water_body(label, loc, extent, mesh, material, kind, surface_scale=(1, 1, 1)):
    """Lake / ocean: zone box (top = surface) + surface mesh; loc is the box CENTRE."""
    b_ = spawn(unreal.RPGWaterBody, loc, label=label)
    zone = b_.get_editor_property('zone')
    zone.set_box_extent(unreal.Vector(*extent), True)
    zone.set_editor_property('waves', waves(kind))
    zone.set_editor_property('underwater_material', M_UNDERWATER); zone.set_editor_property('caustic_material', M_CAUSTICS)
    b_.set_editor_property('surface_scale', unreal.Vector(*surface_scale))
    b_.set_editor_property('water_mesh', mesh); b_.set_editor_property('water_material', material)
    return b_

lake = water_body('Lake', (LAKE_X, 0, -150), (900, 900, 150), SM_LAKE, W['MI_StylizedWater_Lake'], 'Lake')

def ground_z(x, y, default=0.0):
    hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, 3000), unreal.Vector(x, y, -3000),
                                                 unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [], unreal.DrawDebugTrace.NONE, True)
    t = hit.to_tuple() if hit else None
    if not t or not t[0]: return default
    for v in t:
        if isinstance(v, unreal.Vector) and abs(v.z) < 2999: return v.z
    return default

def along(d, a):
    """point at distance d (cm) along the river, a = -1..1 across (fraction of half width)."""
    p = spl.get_location_at_distance_along_spline(d, unreal.SplineCoordinateSpace.WORLD)
    r = spl.get_right_vector_at_distance_along_spline(d, unreal.SplineCoordinateSpace.WORLD)
    half = 400.0 * spl.get_scale_at_distance_along_spline(d).y
    return unreal.Vector(p.x + r.x * a * half, p.y + r.y * a * half, p.z), p

rock_pt, rock_c = along(3400, .62)
rock = spawn(ROCKS['Rock_MediumLarge_B'], (rock_pt.x, rock_pt.y, ground_z(rock_pt.x, rock_pt.y) - 10), (35, 0, 0), 'Rock_Emergent', (1.7, 1.7, 1.7))
for k, (d, a, n, s) in enumerate(((2900, .1, 'Rock_Medium_A', .9), (3100, -.55, 'Rock_MediumSmall_B', 1.3), (3800, -.45, 'Rock_MediumSmall_C', 1.4),
                                   (3600, .1, 'Pebbles_A', 2.5), (3000, .5, 'Rock_SmallMedium_A', 3.0), (4000, .3, 'Rock_SmallMedium_A', 3.0))):
    p, _ = along(d, a); spawn(ROCKS[n], (p.x, p.y, ground_z(p.x, p.y) - 12), (k * 70, 0, 0), f'Rock_{k}', (s, s, s))
for k, (d, a, n, s) in enumerate(((2700, 1.45, 'Grass_Tall', 1.3), (2900, 1.6, 'Grass_Medium', 1.2), (3100, 1.5, 'Grass_Medium', 1.1),
                                   (3800, -1.5, 'Grass_Tall', 1.3), (4000, -1.6, 'Grass_Medium', 1.1), (4200, -1.45, 'Grass_Short', 1.2))):
    p, _ = along(d, a); spawn(GRASS[n], (p.x, p.y, ground_z(p.x, p.y) - 3), (k * 50, 0, 0), f'Grass_{k}', (s, s, s))
for k, (x, y, n, r) in enumerate(((-600, 1000, 'Tree_Forest_01', 0), (50, 1100, 'Tree_Forest_03', 80), (650, 950, 'Tree_Forest_02', 170),
                                   (-1150, 900, 'Tree_Forest_02', 120), (1050, 1150, 'Tree_Forest_01', 40))):
    spawn(TREES[n], (LAKE_X + x, YS * y, ground_z(LAKE_X + x, YS * y, 40)), (r, 0, 0), f'Tree_{k}')
for k, (x, y, n, s) in enumerate(((-560, -460, 'Rock_MediumSmall_B', 1.5), (-400, -640, 'Rock_SmallMedium_A', 2.5), (-180, -540, 'Pebbles_A', 2.5))):
    spawn(ROCKS[n], (LAKE_X + x, YS * y, ground_z(LAKE_X + x, YS * y) - 10), (k * 60, 0, 0), f'LakeRock_{k}', (s, s, s))

# ocean + beach (land toward Unreal Y sign LAND; the shoreline is ~18 m from the beach centre)
OCEAN_X, LAND = -25000.0, -YS
beach = spawn(SM_BEACH, (OCEAN_X, 0, 0), label='Terrain_Beach')
mi_b = unreal.load_asset(f'{TERRAIN}/MI_Terrain_Beach') if EAL.does_asset_exist(f'{TERRAIN}/MI_Terrain_Beach') else \
    TOOLS.create_asset('MI_Terrain_Beach', TERRAIN, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
MEL.set_material_instance_parent(mi_b, M_TERRAIN)
MEL.set_material_instance_vector_parameter_value(mi_b, 'Tint', unreal.LinearColor(11.0, 24.0, 42.0, 1))   # dark brown earth texture -> pale warm sand
EAL.save_loaded_asset(mi_b); beach.static_mesh_component.set_material(0, mi_b)
ocean = water_body('Ocean', (OCEAN_X, -LAND * 1500, -450), (8000, 5500, 450), SM_OCEAN, W['MI_StylizedWater_Ocean'], 'Ocean')
for k, (x, y) in enumerate(((-300, -1200), (250, -2200), (900, -800))):
    spawn(ROCKS['Rock_MediumLarge_B'] if k == 0 else ROCKS['Rock_Medium_A'],
          (OCEAN_X + x, LAND * y, ground_z(OCEAN_X + x, LAND * y) - 20), (k * 80, 0, 0), f'SeaRock_{k}', (2.2, 2.2, 2.2) if k == 0 else (1.6, 1.6, 1.6))

# floating props (physics + buoyancy): barrels/crate on the lake and ocean, a log drifting down the river
CYL, CUBE = unreal.load_asset('/Engine/BasicShapes/Cylinder'), unreal.load_asset('/Engine/BasicShapes/Cube')
def floater(label, mesh, loc, scale, rot=(0, 0, 0), tags=()):
    f_ = spawn(unreal.RPGFloatingProp, loc, rot, label, scale)
    m_ = f_.get_editor_property('mesh'); m_.set_static_mesh(mesh); m_.set_material(0, M_BARK); m_.set_simulate_physics(True)
    f_.tags = [unreal.Name('BuoyancyProbe')] + [unreal.Name(t) for t in tags]
    return f_
floater('Float_LakeBarrel', CYL, (LAKE_X + 150, 0, 60), (0.7, 0.7, 0.9))
floater('Float_LakeCrate', CUBE, (LAKE_X - 250, YS * 200, 60), (0.8, 0.8, 0.8), (25, 0, 0))
floater('Float_OceanBarrel', CYL, (OCEAN_X + 150, -LAND * 1800, 80), (0.7, 0.7, 0.9))
floater('Float_OceanCrate', CUBE, (OCEAN_X - 500, -LAND * 2600, 80), (0.9, 0.9, 0.9), (40, 0, 0))
lp, _ = along(1500, 0.0)
lyaw = math.degrees(math.atan2(*reversed(tuple(spl.get_direction_at_distance_along_spline(1500, unreal.SplineCoordinateSpace.WORLD).to_tuple()[:2]))))
floater('Float_RiverLog', CYL, (lp.x, lp.y, lp.z + 30), (0.4, 0.4, 1.8), (lyaw, 90, 0), ('RiverDrift',))

# wading test points (RPG.Water.Wading): dry bank, wading depth in the lake, head-under in the ocean
def point(tag, x, y):
    tp = spawn(unreal.TargetPoint, (x, y, ground_z(x, y)), label=tag); tp.tags = [unreal.Name(tag)]
point('WaterTest_Dry', -1100, YS * -500)   # river terrain's dry bank before the river starts (walks +X, stays dry)
point('WaterTest_Wade', LAKE_X - 200, YS * -520)   # ~1 m of water
point('WaterTest_Deep', OCEAN_X - 500, -LAND * 3500)

# light shafts from the sun (seen under water / through foliage)
sc.set_editor_property('enable_light_shaft_bloom', True); sc.set_editor_property('enable_light_shaft_occlusion', True)

# cameras photographed by Visual.WaterShots (sorted by label)
def camera(label, eye, target, fov=60.0):
    d = unreal.Vector(target[0] - eye[0], target[1] - eye[1], target[2] - eye[2])
    yaw = math.degrees(math.atan2(d.y, d.x)); pitch = math.degrees(math.atan2(d.z, math.hypot(d.x, d.y)))
    c = spawn(unreal.CameraActor, eye, (yaw, pitch, 0), label)
    c.tags = [unreal.Name('WaterShot')]
    cc = c.get_component_by_class(unreal.CameraComponent); cc.set_editor_property('field_of_view', fov); cc.set_editor_property('constrain_aspect_ratio', False)
    return c

fwd = spl.get_direction_at_distance_along_spline(3400, unreal.SplineCoordinateSpace.WORLD)
yaw_f = math.atan2(fwd.y, fwd.x) + math.radians(55)
tgt = (rock_c.x, rock_c.y, rock_c.z)
camera('WS_01_RiverRock', (tgt[0] - math.cos(yaw_f) * 560, tgt[1] - math.sin(yaw_f) * 560, tgt[2] + 860), tgt, 55)
low, _ = along(2300, -1.3)
camera('WS_02_RiverLow', (low.x, low.y, low.z + 170), (rock_c.x, rock_c.y, rock_c.z), 65)
camera('WS_03_Lake', (LAKE_X - 320, YS * -900, 700), (LAKE_X - 100, YS * 250, 0), 60)
camera('WS_04_Overview', (-2200, YS * 2600, 2600), (1200, YS * 3600, 0), 70)
camera('WS_05_OceanShore', (OCEAN_X - 900, LAND * 3400, 520), (OCEAN_X + 200, LAND * 300, 0), 65)
camera('WS_06_OceanWaves', (OCEAN_X - 200, -LAND * 800, 260), (OCEAN_X + 1800, -LAND * 3800, 0), 70)
camera('WS_07_Underwater', (OCEAN_X + 300, -LAND * 2400, -260), (OCEAN_X + 2300, -LAND * 1600, -120), 75)
camera('WS_08_LakeFloating', (LAKE_X - 700, YS * -700, 420), (LAKE_X, YS * 60, 0), 55)
spawn(unreal.PlayerStart, (-800, 0, 300), label='PlayerStart')   # on the bank beside the river's start
les.save_current_level()
unreal.log('WATER_TEST_BUILT')
unreal.SystemLibrary.quit_editor()
