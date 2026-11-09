"""Night sky in M_Vista_Sky (frozen material, edited in place; run Tools/World/set_sky_disc_sizes.py first). Clear full-moon night:
dense coloured stars, nebulas, a blue Milky Way and two spiral galaxies, with the original soft moon. Everything is procedural in the sky
material and fades in after dusk. Replaces the blocks between the marker comments, so it's safe to rerun.
  UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/set_night_sky.py" -unattended -RenderOffscreen"""
import unreal

from pathlib import Path
NIGHT = Path(__file__).with_name('night_sky_detail.hlsl').read_text()

MOON = r'''// moon: the original soft look, kept at the current approved size
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
times = [e for e in MEL.get_material_expressions(sky) if e.get_class().get_name() == 'MaterialExpressionTime']
if not times:
    times = [MEL.create_material_expression(sky, unreal.MaterialExpressionTime, -1500, 1800)]
times[0].set_editor_property('ignore_pause', True)
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
    has_weather = 'float phaseAngle' in code
    moon = Path(__file__).with_name('moon_phases.hlsl').read_text() if has_weather else MOON
    code = code[:s0] + NIGHT + code[s1:m0] + moon + ('' if has_weather else RETURN) + '\n'
    e.set_editor_property('code', code)
    MEL.connect_material_expressions(times[0], '', e, 'T')
    state = 'changed'
MEL.recompile_material(sky)
saved = unreal.EditorAssetLibrary.save_loaded_asset(sky, only_if_is_dirty=False)
unreal.log(f'NIGHT_SKY {state} saved={saved}')
unreal.SystemLibrary.quit_editor()
