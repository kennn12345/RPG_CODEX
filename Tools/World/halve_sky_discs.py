"""Halve only the sun/moon radii in the currently loaded sky material; keep editor open."""
import unreal,math,json,hashlib,shutil
from pathlib import Path

def halve_sky_discs():
    root=Path(unreal.Paths.project_dir())
    report_dir=Path(unreal.Paths.project_saved_dir())/'SkyDiscResize'
    report_dir.mkdir(exist_ok=True,parents=True)
    material_file=root/'Content/RPG/Environment/World/Materials/M_Vista_Sky.uasset'
    backup=report_dir/'M_Vista_Sky_before_halving.uasset'
    assert not (report_dir/'halved.json').exists(), 'Resize already applied; refusing a second halving'
    shutil.copy2(material_file,backup)
    mat=unreal.load_asset('/Game/RPG/Environment/World/Materials/M_Vista_Sky')
    mel=unreal.MaterialEditingLibrary
    expressions=mel.get_material_expressions(mat)
    code_before=[str(e.get_editor_property('code')) for e in expressions if e.get_class().get_name()=='MaterialExpressionCustom']
    parameters={str(e.get_editor_property('parameter_name')):e for e in expressions if e.get_class().get_name()=='MaterialExpressionScalarParameter'}
    assert 'SunSize' in parameters and 'MoonSize' in parameters
    report={}
    for name in ('SunSize','MoonSize'):
        e=parameters[name];before=float(e.get_editor_property('default_value'));after=before*0.5
        e.set_editor_property('default_value',after)
        report[name]=dict(before_radians=before,after_radians=after,before_degrees=math.degrees(before),after_degrees=math.degrees(after))
    assert code_before==[str(e.get_editor_property('code')) for e in expressions if e.get_class().get_name()=='MaterialExpressionCustom']
    mel.recompile_material(mat)
    assert unreal.EditorAssetLibrary.save_loaded_asset(mat,only_if_is_dirty=False)
    (report_dir/'halved.json').write_text(json.dumps(report,indent=2))
    unreal.log('SKY_DISCS_HALVED '+json.dumps(report))

halve_sky_discs()
