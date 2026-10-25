import unreal,json
from pathlib import Path
root='/Game/Characters/UEFN_Mannequin/Animations/Run/'
report={}
for name in ['M_Relaxed_Run_Loop_B','M_Neutral_Run_Loop_B','M_Relaxed_Run_Start_B_Lfoot','M_Relaxed_Run_Stop_B_Lfoot']:
    path=root+name
    clip=unreal.load_asset(path)
    report[name]={'loaded':bool(clip),'references':list(unreal.EditorAssetLibrary.find_package_referencers_for_asset(path,False)) if clip else []}
out=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()))/'DirectionalReview'
out.mkdir(exist_ok=True);(out/'asset-audit.json').write_text(json.dumps(report,indent=2));unreal.log('DIRECTIONAL_ASSET_AUDIT '+json.dumps(report))
