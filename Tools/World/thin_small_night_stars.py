"""Keep half of the two smallest star layers; leave every other sky setting intact."""
import unreal, json, shutil
from pathlib import Path
root=Path(unreal.Paths.project_dir())
report=root/'Saved/NightSkyFewerSmallStars'
report.mkdir(parents=True,exist_ok=True)
assert not (report/'applied.json').exists(), 'Already applied; inspect before repeating'
mat=unreal.load_asset('/Game/RPG/Environment/World/Materials/M_Vista_Sky')
mel=unreal.MaterialEditingLibrary
node=next(e for e in mel.get_material_expressions(mat) if e.get_class().get_name()=='MaterialExpressionCustom' and '// stars' in str(e.get_editor_property('code')))
before=str(node.get_editor_property('code'))
line='            if (l >= 2 && w.h3(id + 21.47) >= 0.5) continue; // Thin only the two smallest layers by half.\n'
anchor='            if (seed >= density) continue;\n'
assert line not in before and before.count(anchor)==1
after=before.replace(anchor,anchor+line)
start,end=after.index('// stars'),after.index('// big soft sun')
assert after[start:end].strip()==(root/'Tools/World/night_sky_detail.hlsl').read_text().strip()
shutil.copy2(root/'Content/RPG/Environment/World/Materials/M_Vista_Sky.uasset',report/'M_Vista_Sky_before.uasset')
(report/'custom_before.hlsl').write_text(before)
(report/'custom_after.hlsl').write_text(after)
node.set_editor_property('code',after)
mel.recompile_material(mat)
assert unreal.EditorAssetLibrary.save_loaded_asset(mat,only_if_is_dirty=False)
(report/'applied.json').write_text(json.dumps(dict(saved=True,small_star_fraction=0.5,other_shader_code_unchanged=True),indent=2))
unreal.log('SMALL_NIGHT_STARS_THINNED_SAVED')
