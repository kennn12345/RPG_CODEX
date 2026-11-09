import unreal,runpy
from pathlib import Path
root=Path(unreal.Paths.project_dir())
runpy.run_path(str(root/'Tools/World/apply_dynamic_cloud_fronts.py'),run_name='__main__')
mat=unreal.load_asset('/Game/RPG/Environment/Weather/M_Weather_Clouds')
assert unreal.EditorAssetLibrary.save_loaded_asset(mat,only_if_is_dirty=False)
unreal.log('DYNAMIC_FRONT_ASSETS_FINAL_SAVED')
