"""Apply the November 7 rain/sky revision in the open editor; no level rebuild."""
import unreal,math,json,shutil
from pathlib import Path
root=Path(unreal.Paths.project_dir());report=root/'Saved/RainRevision';report.mkdir(parents=True,exist_ok=True)
for name,path in [('Sky','World/Materials/M_Vista_Sky'),('Rain','Weather/M_Weather_Rain'),('Lighting','Weather/MPC_WeatherLighting')]:
    backup=report/(name+'_before.uasset')
    if not backup.exists():shutil.copy2(root/('Content/RPG/Environment/'+path+'.uasset'),backup)
mel=unreal.MaterialEditingLibrary
mat=unreal.load_asset('/Game/RPG/Environment/World/Materials/M_Vista_Sky')
expressions=mel.get_material_expressions(mat)
source=next(e for e in expressions if e.get_class().get_name()=='MaterialExpressionCustom' and '// stars' in str(e.get_editor_property('code')))
code=str(source.get_editor_property('code'));start,end=code.index('// stars'),code.index('// big soft sun')
code=code[:start]+(root/'Tools/World/night_sky_detail.hlsl').read_text().rstrip()+'\n'+code[end:]
source.set_editor_property('code',code)
for e in expressions:
    if e.get_class().get_name()=='MaterialExpressionScalarParameter':
        name=str(e.get_editor_property('parameter_name'))
        if name in ['SunSize','MoonSize']:e.set_editor_property('default_value',math.radians(2.4 if name=='SunSize' else 3.0))
mel.recompile_material(mat);assert unreal.EditorAssetLibrary.save_loaded_asset(mat,only_if_is_dirty=False)
script=root/'Tools/World/refine_weather_materials.py'
exec(compile(script.read_text(),str(script),'exec'),{'__file__':str(script),'__name__':'__main__'})
(report/'applied.json').write_text(json.dumps(dict(sun_radius_degrees=2.4,moon_radius_degrees=3,star_fraction=.4,moon_occludes_stars=True,rain_world_space=True),indent=2))
unreal.log('RAIN_SKY_REVISION_SAVED')
