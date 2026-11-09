"""Apply accepted sky size/count changes and shorter rain; keep the live editor open."""
import unreal,runpy,shutil,json
from pathlib import Path
root=Path(unreal.Paths.project_dir());report=root/'Saved/CloudFogRevision';report.mkdir(parents=True,exist_ok=True)
for name,path in [('Sky','World/Materials/M_Vista_Sky'),('Rain','Weather/M_Weather_Rain')]:
    backup=report/(name+'_before_nov8.uasset')
    if not backup.exists():shutil.copy2(root/('Content/RPG/Environment/'+path+'.uasset'),backup)
runpy.run_path(str(root/'Tools/World/apply_rain_sky_revision.py'),run_name='__main__')
(report/'applied.json').write_text(json.dumps(dict(sun_radius_degrees=2.4,moon_radius_degrees=3,star_count_fraction_of_previous=.5,ground_fog_height_cm=609.6,cloud_render_mode=1,rain_shorter=True),indent=2))
unreal.log('CLOUD_GROUNDFOG_REVISION_SAVED')
