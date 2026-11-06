"""Night sky in M_Vista_Sky (frozen material, edited in place; run Tools/World/set_sky_disc_sizes.py first). Clear full-moon night:
dense coloured stars, nebulas, a blue Milky Way and two spiral galaxies, with the original soft moon. Everything is procedural in the sky
material and fades in after dusk. Replaces the blocks between the marker comments, so it's safe to rerun.
  UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/set_night_sky.py" -unattended -RenderOffscreen"""
import unreal

NIGHT = r'''// stars, nebulas and galaxies: a clear full-moon night
float deep = saturate((-h - 0.2) / 0.3);                                     // full show once the sky is properly dark, so dusk keeps its colours
float starFade = saturate((-h - 0.05) / 0.25);
float3 nightSky = 0;
if (Night > 0.01 && V.z > -0.05)
{
    c *= lerp(1.0, 0.25, deep);                                              // near-black backdrop
    float3 q = V * 2.3 + 2.0 * float3(w.fbm3(V * 1.7 + 11.0), w.fbm3(V * 1.7 + 23.0), w.fbm3(V * 1.7 + 37.0));   // warped, so shapes billow
    float n1 = w.fbm3(q), n2 = w.fbm3(q * 2.3 + 5.0), lace = w.fbm3(q * 6.0 + 9.0);
    // nebula patches: each glows around its own direction with its own colours
    float a1 = pow(saturate(dot(V, normalize(float3(0.62, -0.72, 0.42))) * 0.5 + 0.5), 40.0);   // over the valley: electric blue with fiery edges
    float a2 = pow(saturate(dot(V, normalize(float3(-0.72, -0.28, 0.5))) * 0.5 + 0.5), 60.0);   // magenta and violet
    float a3 = pow(saturate(dot(V, normalize(float3(0.1, 0.72, 0.5))) * 0.5 + 0.5), 50.0);      // teal and rose
    float a4 = pow(saturate(dot(V, normalize(float3(0.3, 0.15, 0.95))) * 0.5 + 0.5), 70.0);     // overhead indigo
    float body = smoothstep(0.44, 0.68, n1), edge = smoothstep(0.4, 0.5, n1) * (1.0 - smoothstep(0.5, 0.6, n1));
    float3 neb = a1 * (float3(0.05, 0.22, 1.0) * body * 1.9 + float3(1.0, 0.38, 0.05) * edge * 1.3 + float3(0.5, 0.75, 1.0) * pow(saturate(lace), 3.0) * body * 1.6);
    neb += a2 * (float3(0.75, 0.08, 0.7) * body * 1.5 + float3(0.25, 0.1, 0.95) * edge * 1.0 + float3(1.0, 0.45, 0.8) * pow(saturate(n2), 3.0) * body);
    neb += a3 * (float3(0.03, 0.6, 0.65) * body * 1.3 + float3(0.95, 0.2, 0.3) * edge * 0.9);
    neb += a4 * (float3(0.12, 0.1, 0.8) * body * 1.1 + float3(0.55, 0.12, 0.65) * edge * 0.6);
    // dark dust lanes in front of the glow
    neb *= 1.0 - 0.75 * smoothstep(0.52, 0.72, n2) * smoothstep(0.42, 0.62, w.fbm3(q * 3.1 + 2.0));
    // Milky Way: a broad band of blue haze
    float3 mwN = normalize(float3(0.35, 0.8, 0.5));
    float dz = dot(V, mwN);
    float band = exp(-dz * dz / 0.035), core = exp(-dz * dz / 0.006);
    float3 mw = band * (float3(0.05, 0.16, 0.5) * smoothstep(0.34, 0.7, n2) + float3(0.35, 0.5, 0.9) * pow(saturate(lace), 3.0)) + core * (float3(1.0, 0.68, 0.34) * smoothstep(0.46, 0.68, n1) * 0.8 + float3(0.55, 0.26, 0.14) * smoothstep(0.42, 0.6, n2) * 0.45);   // blue haze, gold and rust core
    mw *= 1.0 - 0.92 * smoothstep(0.47, 0.6, w.fbm3(V * 9.0 + 4.0)) * band;                  // dark dust lanes
    // faint teal glow low on the horizon
    float3 glowLow = float3(0.04, 0.24, 0.26) * pow(saturate(1.0 - V.z), 7.0);
    // two spiral galaxies, tilted discs with log-spiral arms
    float3 gal = 0;
    for (int g = 0; g < 2; g++)
    {
        float3 Gd = normalize(g == 0 ? float3(0.86, -0.3, 0.42) : float3(-0.42, 0.74, 0.52));
        float gs = g == 0 ? 0.3 : 0.16;
        float3 Gx = normalize(cross(Gd, float3(0.2, 0.3, 1.0))), Gy = cross(Gd, Gx);
        float2 p = float2(dot(V, Gx), dot(V, Gy)) / gs;
        if (dot(V, Gd) > 0.0)
        {
            float tilt = g == 0 ? 0.62 : 0.47, turn = g == 0 ? 0.7 : -0.4;
            p = float2(p.x * cos(turn) - p.y * sin(turn), p.x * sin(turn) + p.y * cos(turn));
            p.y /= 1.0 - tilt;
            float r = length(p), th = atan2(p.y, p.x);
            float arm = pow(saturate(cos(2.0 * th - 5.0 * log(r + 0.06)) * 0.5 + 0.5), 2.0);
            float disc2 = exp(-r * 3.4), mott = 0.45 + 0.9 * w.fbm3(float3(p * 7.0, float(g) * 4.0));
            float dustG = 1.0 - 0.7 * pow(saturate(cos(2.0 * th - 5.0 * log(r + 0.06) + 1.3) * 0.5 + 0.5), 4.0) * smoothstep(0.1, 0.3, r);
            gal += (float3(0.28, 0.45, 1.0) * disc2 * arm * mott * 3.2 + float3(0.9, 0.48, 0.3) * exp(-r * 6.0) * mott * 2.2 * dustG + float3(1.0, 0.9, 0.72) * exp(-r * r * 90.0) * 7.0) * (g == 0 ? 1.0 : 0.7);
        }
    }
    // stars in four layers: colour by chance, a few big and glowing, thicker along the Milky Way
    float3 stars = 0;
    for (int l = 0; l < 4; l++)
    {
        float grid = l == 0 ? 70.0 : l == 1 ? 150.0 : l == 2 ? 300.0 : 620.0;
        float3 sv = V * grid + float(l) * 17.3, cell = floor(sv), f = frac(sv);
        float seed = w.h3(cell + float(l) * 3.1);
        float density = (l == 0 ? 0.1 : l == 1 ? 0.24 : l == 2 ? 0.42 : 0.5) * (1.0 + 1.2 * band) * (1.0 + 0.5 * smoothstep(0.35, 0.65, n2));
        if (seed < density)
        {
            float3 at = 0.5 + 0.28 * (float3(w.h3(cell + 1.3), w.h3(cell + 2.7), w.h3(cell + 5.1)) - 0.5) * 2.0 * (l == 0 ? 0.3 : 1.0);
            float d = length(f - at), kind = w.h3(cell + 7.9), power = w.h3(cell + 9.1);
            float3 tint = kind < 0.5 ? float3(0.72, 0.84, 1.0) : kind < 0.75 ? float3(1.0, 0.98, 0.94) : kind < 0.9 ? float3(1.0, 0.82, 0.5) : float3(1.0, 0.55, 0.3);
            float size = l == 0 ? 0.1 : l == 1 ? 0.14 : l == 2 ? 0.2 : 0.3;
            float point2 = smoothstep(size, 0.0, d);
            float halo = l == 0 ? exp(-d * d * 90.0) * 0.1 : 0.0;
            float gain = (l == 0 ? 3.2 : l == 1 ? 1.3 : l == 2 ? 0.6 : 0.24) * (0.3 + 0.7 * power * power);
            stars += tint * (point2 * point2 + halo) * gain * (0.8 + 0.2 * sin(T * (1.5 + 3.0 * kind) + seed * 400.0));
        }
    }
    nightSky = (neb * 0.06 + mw * 0.045 + gal * 0.05 + glowLow * 0.12) * deep + stars * StarBright * starFade;   // night exposure amplifies these a lot
    nightSky *= Night * above;
}
'''

MOON = r'''// moon: the original soft look, kept at the larger size
float cm = dot(V, Lm);
float md = smoothstep(cos(MoonSize), cos(MoonSize * 0.97), cm);
float3 tx = normalize(cross(Lm, float3(0, 0, 1))), ty = cross(tx, Lm);
float2 uv = float2(dot(V, tx), dot(V, ty)) / sin(MoonSize);
float limb = sqrt(saturate(1.0 - dot(uv, uv)));
float maria = smoothstep(0.42, 0.62, w.fbm3(float3(uv * 2.2, 3.7)));
float craters = w.fbm3(float3(uv * 9.0, 1.3));
float mtex = (0.95 - 0.35 * maria) * (0.85 + 0.3 * craters) * (0.55 + 0.45 * limb);
float3 moonC = (md * mtex * float3(0.98, 0.95, 0.88) * MoonBright + pow(saturate(cm), 250.0) * float3(0.55, 0.65, 0.95) * MoonGlow) * Night;
'''

RETURN = 'return c * (1.0 - disc * above * smoothstep(-0.05, 0.02, h) * (1.0 - smoothstep(0.12, 0.45, h))) * (1.0 - md * Night) + nightSky * (1.0 - md) + (sunC + moonC) * above;   // the low sun and the moon replace the sky behind them'

MEL = unreal.MaterialEditingLibrary
sky = unreal.load_asset('/Game/RPG/Environment/World/Materials/M_Vista_Sky')
state = 'custom node not found'
for e in MEL.get_material_expressions(sky):
    if e.get_class().get_name() != 'MaterialExpressionCustom':
        continue
    code = str(e.get_editor_property('code'))
    s0 = code.find('// stars')
    s1 = code.find('// big soft sun')
    m0 = code.find('float cm = dot(V, Lm);')
    if m0 >= 0 and code.rfind('// moon:', 0, m0) >= 0:
        m0 = code.rfind('// moon:', 0, m0)
    r0 = code.find('return c ')
    if min(s0, s1, m0, r0) < 0:
        state = f'markers missing {s0} {s1} {m0} {r0}'
        continue
    code = code[:s0] + NIGHT + code[s1:m0] + MOON + RETURN + '\n'
    e.set_editor_property('code', code)
    state = 'changed'
MEL.recompile_material(sky)
saved = unreal.EditorAssetLibrary.save_loaded_asset(sky, only_if_is_dirty=False)
unreal.log(f'NIGHT_SKY {state} saved={saved}')
unreal.SystemLibrary.quit_editor()
