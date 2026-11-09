"""Soften night haze while preserving star scatter, animation, daylight and discs."""
import unreal, json, shutil
from pathlib import Path
root=Path(unreal.Paths.project_dir())
report=root/'Saved/NightSkySoftened'
report.mkdir(parents=True,exist_ok=True)
assert not (report/'applied.json').exists(), 'Already applied; inspect before repeating'
mat=unreal.load_asset('/Game/RPG/Environment/World/Materials/M_Vista_Sky')
mel=unreal.MaterialEditingLibrary
expressions=mel.get_material_expressions(mat)
node=next(e for e in expressions if e.get_class().get_name()=='MaterialExpressionCustom' and '// stars' in str(e.get_editor_property('code')))
before=str(node.get_editor_property('code'))
start,end=before.index('// stars'),before.index('// big soft sun')
night=(root/'Tools/World/night_sky_detail.hlsl').read_text()
after=before[:start]+night+'\n'+before[end:]
assert after[:start]==before[:start]
assert after[after.index('// big soft sun'):]==before[end:]
scatter='    // Jittered 2D face samples'
assert before[before.index(scatter):before.index('    nightSky =')].strip()==after[after.index(scatter):after.index('    // Keep most of the dome clear:')].strip()
shutil.copy2(root/'Content/RPG/Environment/World/Materials/M_Vista_Sky.uasset',report/'M_Vista_Sky_before.uasset')
(report/'custom_before.hlsl').write_text(before)
(report/'custom_after.hlsl').write_text(after)
node.set_editor_property('code',after)
mel.recompile_material(mat)
assert unreal.EditorAssetLibrary.save_loaded_asset(mat,only_if_is_dirty=False)
(report/'applied.json').write_text(json.dumps(dict(saved=True,star_scatter_and_twinkle_unchanged=True,day_sun_moon_unchanged=True),indent=2))
unreal.log('NIGHT_GALAXY_SOFTENED_SAVED')
