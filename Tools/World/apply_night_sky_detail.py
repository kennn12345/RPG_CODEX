"""Replace only the night block in the live editor. No quit or map save."""
import unreal,json,hashlib,shutil,math
from pathlib import Path

def apply_night_detail():
    root=Path(unreal.Paths.project_dir())
    report=root/'Saved/NightSkyDetail';report.mkdir(exist_ok=True,parents=True)
    material_file=root/'Content/RPG/Environment/World/Materials/M_Vista_Sky.uasset'
    backup=report/'M_Vista_Sky_before_night_detail.uasset'
    assert not (report/'applied.json').exists(),'Night detail already applied; inspect before repeating'
    shutil.copy2(material_file,backup)
    mat=unreal.load_asset('/Game/RPG/Environment/World/Materials/M_Vista_Sky')
    mel=unreal.MaterialEditingLibrary
    expressions=mel.get_material_expressions(mat)
    scalars={str(e.get_editor_property('parameter_name')):float(e.get_editor_property('default_value')) for e in expressions if e.get_class().get_name()=='MaterialExpressionScalarParameter'}
    found=0
    for node in expressions:
        if node.get_class().get_name()!='MaterialExpressionCustom':continue
        code=str(node.get_editor_property('code'))
        start=code.find('// stars');end=code.find('// big soft sun')
        if start<0 or end<=start:continue
        (report/'custom_before.hlsl').write_text(code)
        night=(root/'Tools/World/night_sky_detail.hlsl').read_text()
        updated=code[:start]+night+'\n'+code[end:]
        assert updated[:start]==code[:start]
        assert updated[updated.index('// big soft sun'):]==code[end:]
        node.set_editor_property('code',updated)
        # Verify the scintillation input is driven by real material time.
        times=[e for e in expressions if e.get_class().get_name()=='MaterialExpressionTime']
        if not times:
            times=[mel.create_material_expression(mat,unreal.MaterialExpressionTime,-1500,1800)]
        times[0].set_editor_property('ignore_pause',True)
        assert mel.connect_material_expressions(times[0],'',node,'T')
        (report/'custom_after.hlsl').write_text(updated)
        found+=1
    assert found==1,found
    after={str(e.get_editor_property('parameter_name')):float(e.get_editor_property('default_value')) for e in expressions if e.get_class().get_name()=='MaterialExpressionScalarParameter'}
    assert scalars==after
    mel.recompile_material(mat)
    assert unreal.EditorAssetLibrary.save_loaded_asset(mat,only_if_is_dirty=False)
    data=dict(night_block_replaced=True,day_sun_moon_code_unchanged=True,all_scalar_parameters_unchanged=True,
        sun_radius_degrees=math.degrees(scalars['SunSize']),moon_radius_degrees=math.degrees(scalars['MoonSize']),
        time_input_connected=True,saved=True)
    (report/'applied.json').write_text(json.dumps(data,indent=2))
    unreal.log('NIGHT_SKY_DETAIL_APPLIED '+json.dumps(data))

apply_night_detail()
