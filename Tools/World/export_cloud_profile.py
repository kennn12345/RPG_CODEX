"""Read-only: export the stock cloud height-profile texture to Saved/CloudLook/T_Profile_08.png for inspection."""
import unreal
from pathlib import Path
tex = unreal.load_asset('/Engine/EngineSky/VolumetricClouds/T_Profile_08')
base = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), 'CloudLook')
for ext in ('tga','hdr','exr','bmp'):
  out = base / ('T_Profile_08.' + ext)
  task = unreal.AssetExportTask()
  task.object = tex
  task.filename = str(out)
  task.automated = True
  task.replace_identical = True
  task.prompt = False
  ok = unreal.Exporter.run_asset_export_task(task)
  unreal.log(f'CLOUD_PROFILE_EXPORT ok={ok} size={tex.blueprint_get_size_x()}x{tex.blueprint_get_size_y()} srgb={tex.srgb} path={out}')
