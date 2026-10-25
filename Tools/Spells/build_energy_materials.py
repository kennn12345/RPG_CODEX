"""Author the original energy materials. Run with UnrealEditor-Cmd -run=pythonscript -script=... ."""
import unreal

ROOT = '/Game/RPG/Spells/Energy'
unreal.EditorAssetLibrary.make_directory(ROOT)
mel = unreal.MaterialEditingLibrary

def node(mat, cls, **properties):
    value = mel.create_material_expression(mat, cls)
    for name, prop in properties.items():
        value.set_editor_property(name, prop)
    return value

def connect(source, target, pin):
    assert mel.connect_material_expressions(source, '', target, pin), pin

def custom(mat, code, inputs, output=unreal.CustomMaterialOutputType.CMOT_FLOAT3):
    pins = []
    for name in inputs:
        pin = unreal.CustomInput()
        pin.set_editor_property('input_name', name)
        pins.append(pin)
    value = node(mat, unreal.MaterialExpressionCustom, code=code, output_type=output,
                 inputs=pins)
    for name, source in inputs.items():
        connect(source, value, name)
    return value

for kind in ['Core', 'Shell', 'Ring']:
    path = f'{ROOT}/M_Energy{kind}'
    mat = unreal.load_asset(path)
    if not mat:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            f'M_Energy{kind}', ROOT, unreal.Material, unreal.MaterialFactoryNew())
    mel.delete_all_material_expressions(mat)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided', True)
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE if kind == 'Core' else unreal.BlendMode.BLEND_ADDITIVE)
    uv = node(mat, unreal.MaterialExpressionTextureCoordinate)
    time = node(mat, unreal.MaterialExpressionTime)
    color = node(mat, unreal.MaterialExpressionVectorParameter, parameter_name='Color', default_value=unreal.LinearColor(0.08,0.5,1,1))
    fade = node(mat, unreal.MaterialExpressionScalarParameter, parameter_name='Fade', default_value=1.0)
    fresnel = node(mat, unreal.MaterialExpressionFresnel, exponent=2.0, base_reflect_fraction=0.02)
    inputs = {'UV':uv, 'T':time, 'C':color, 'F':fresnel, 'Fade':fade}
    veins = 'float v=pow(saturate(0.5+0.5*sin((UV.x*9+sin(UV.y*23-T*3)*0.15+T*0.8)*6.283185)),18);'
    if kind == 'Core':
        code = veins + 'return (C*(1.7+F*4+v*4)+float3(0.65,0.88,1)*pow(1-F,4)*1.4)*Fade;'
    elif kind == 'Shell':
        code = veins + 'return C*(1.2+v*2.0)*Fade;'
    else:
        code = 'return lerp(C,float3(0.6,0.9,1),0.25)*4*Fade;'
    emissive = custom(mat, code, inputs)
    mel.connect_material_property(emissive, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if kind != 'Core':
        if kind == 'Shell':
            opacity_code = veins + 'return saturate(pow(F,2.5)*0.6+v*0.13)*Fade;'
        else:
            opacity_code = '''float2 p=UV-0.5;
float a=atan2(p.y,p.x);
float radius=0.425+sin(a*7-T*5)*0.006;
float ring=1-smoothstep(0.006,0.019,abs(length(p)-radius));
return ring*(0.6+0.4*pow(0.5+0.5*sin(a*5+T*9),2))*Fade;'''
        opacity = custom(mat, opacity_code, inputs, unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        mel.connect_material_property(opacity, '', unreal.MaterialProperty.MP_OPACITY)
    mel.layout_material_expressions(mat)
    mel.recompile_material(mat)
    assert unreal.EditorAssetLibrary.save_loaded_asset(mat)
    unreal.log(f'ENERGY_MATERIAL_SAVED {path}')
