"""Sun, moon and low-sun sky colours in M_Vista_Sky (the sky material is frozen: not rebuilt by build_vista_assets.py, so it's edited
in place). SunSize and MoonSize are radii in radians; originals: sun 4 degrees, moon 5 degrees. Evening: fire-orange sun with a hot
centre and glowing halo, red-orange horizon, purple overhead. Dawn: softer gold-orange sun, brightening sky. Each line is matched by
its start, so any earlier version gets replaced and it's safe to rerun. The final blend line belongs to Tools/World/set_night_sky.py.
On screen, overexposed red shifts to pink or yellow, and too much green reads as yellow: tune low-sun colours by brightness first.
  UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/set_sky_disc_sizes.py" -unattended -RenderOffscreen"""
import math, re
import unreal

SIZES_DEGREES = {'SunSize': 2.4, 'MoonSize': 3.0}
EVENING = 'float evening = smoothstep(-0.2, 0.2, Ls.x);   // the sun rises and sets on opposite sides\n'
GOLD = 'float3 zG = lerp(float3(0.30, 0.34, 0.70), float3(0.22, 0.05, 0.20), evening), hG = lerp(float3(1.0, 0.62, 0.34), float3(1.0, 0.20, 0.03), evening);   // low sun: dawn brightens, evening is red-orange and purple'
TWILIGHT = 'float3 zT = lerp(float3(0.10, 0.10, 0.30), float3(0.12, 0.02, 0.20), evening), hT = lerp(float3(0.62, 0.38, 0.38), float3(0.60, 0.07, 0.08), evening);   // twilight: dusk red and purple'
DIM = 'c *= SkyBright * lerp(lerp(0.55, 0.28, evening), 1.0, a1);                 // much dimmer low-sun sky: overexposed, its red turned pink on screen'
COLOUR = 'float3 sunCol = lerp(lerp(float3(1.0, 0.5, 0.16), float3(1.0, 0.07, 0.004), evening), float3(1.0, 0.93, 0.72), smoothstep(0.1, 0.45, h));   // low sun: fire orange in the evening, softer at dawn'
DISC = ('float duskLow = evening * (1.0 - smoothstep(0.12, 0.45, h));   // the evening sun has to read as a light source: hot centre and a glowing halo\n'
        'float sunCore = smoothstep(cos(SunSize * 0.85), cos(SunSize * 0.2), cs);\n'
        'float3 sunC = sunCol * smoothstep(-0.05, 0.02, h) * (disc * SunGain * lerp(lerp(0.09, 0.07, evening), 1.0, smoothstep(0.12, 0.45, h)) + glow * GlowGain)\n'
        '    + duskLow * smoothstep(-0.05, 0.02, h) * (float3(1.0, 0.36, 0.05) * disc * sunCore * 1.2 + float3(1.0, 0.4, 0.1) * pow(saturate(cs), 28.0) * 0.55);')
PATTERNS = ((r'(float evening = [^\n]*\n)?float3 zG = [^\n]*', EVENING + GOLD), (r'float3 zT = [^\n]*', TWILIGHT), (r'c \*= SkyBright \* [^\n]*', DIM),
            (r'float3 sunCol = [^\n]*', COLOUR), (r'(float (dawnLow|duskLow) = [^\n]*\n)?(float sunCore = [^\n]*\n)?float3 sunC = .*?;[^\n]*', DISC))
MEL = unreal.MaterialEditingLibrary
sky = unreal.load_asset('/Game/RPG/Environment/World/Materials/M_Vista_Sky')
found = 0
missing = []
for e in MEL.get_material_expressions(sky):
    cn = e.get_class().get_name()
    if cn == 'MaterialExpressionScalarParameter' and str(e.get_editor_property('parameter_name')) in SIZES_DEGREES:
        e.set_editor_property('default_value', math.radians(SIZES_DEGREES[str(e.get_editor_property('parameter_name'))]))
        found += 1
    elif cn == 'MaterialExpressionCustom':
        code = str(e.get_editor_property('code'))
        for pattern, new in PATTERNS:
            code, n = re.subn(pattern, lambda m: new, code, count=1, flags=re.S)
            if not n:
                missing.append(pattern[:30])
        e.set_editor_property('code', code)
MEL.recompile_material(sky)
saved = unreal.EditorAssetLibrary.save_loaded_asset(sky, only_if_is_dirty=False)
unreal.log(f'SKY_SIZES set={found} missing={missing} saved={saved}')
unreal.SystemLibrary.quit_editor()
