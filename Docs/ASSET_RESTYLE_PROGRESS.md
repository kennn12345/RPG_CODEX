# Asset restyle progress

Last update: 2026-11-03T17:05:00-06:00
Branch: asset-restyle (from world-vista cfff82f). Local commits only, never pushed.
Brief: Docs/ASSET_RESTYLE_BRIEF.md. Handoff: Docs/WORLD_VISTA_HANDOFF.md ("Asset pipeline" section).

## Blockers
- 2026-11-03 15:27 -> still at 17:05: an interactive UnrealEditor.exe (PID 9524, started 14:59 by the user on
  RPG_CODEX.uproject) is running. Per the brief it is NOT killed -> no Unreal import / render / C++ build yet.
  Everything that does not need Unreal is done (below). NEXT RUN, once the editor is closed, do in order:
  1. C++ build (Visual.AssetShots test in Source/RPG_CODEX/RPGWaterShots.cpp - written, NOT compiled yet):
     Build.bat RPG_CODEXEditor Win64 Development -Project=... -WaitMutex -NoHotReload
  2. UnrealEditor -ExecutePythonScript="C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/build_vista_assets.py all"
     -> check the log for ASSETS_PROBLEM, LogPython: Error, Failed to compile, Error saving. First real run of the
     script: expect API fixes (set_lods: StaticMeshEditorSubsystem.import_lod / set_lod_screen_sizes names unverified;
     soft_normal sets material tangent_space_normal False).
  3. UnrealEditor -ExecutePythonScript=".../Tools/World/build_asset_review.py" (makes L_AssetReview)
  4. -ExecCmds="Automation RunTests Visual.AssetShots" -TestExit="Automation Test Queue Empty" -> Saved/Screenshots/Assets
  5. Sheets with Tools/World/make_sheet.ps1 (reference | ours) -> outputs/World_R01/review, iterate per asset.

## Queue
| # | Step | Status |
|---|---|---|
| 0 | Branch, reference frames + palette (World_R01/reference, PALETTE.md) | done |
| 1 | build_vista_assets.py (assets only, in place) + build_vista.py layout-only | done (script), Unreal run pending |
| 2 | L_AssetReview map script + Visual.AssetShots test | written, build + run pending |
| 3 | Trees: Trees_R02 (ancient + 4 forest, LODs) | Blender done (Trees_R02, previews), Unreal import + review pending |
| 4 | Trees: round world trees R02 + LODs; far tree colours (M_Vista_Canopy palette) | Blender done (World_R02), Unreal pending |
| 5 | Grass + flowers + wheat | Blender done (grass/tall/tuft/wheat/poppies/daisies + LODs), materials written; Unreal pending |
| 6 | Ground + path (M_Vista_Terrain) | next (swatch mesh SM_Review_GroundSwatch exported) |
| 7 | Transitions + detail pieces (tufts, bushes, leaves, path stones, reeds) | detail meshes exported; transitions next |
| 8 | Rocks colour/surface (Rocks_R02 + M_Rock_Stone) | next |
| 9 | Cliffs + ivy | next |
| 10 | Small effects (mist, pollen, drifting leaves) | next |

## Commits
- (this commit) Split world asset build from layout; R02 restyle sources (trees, grass, flowers, details), review stage.

## Notes
- Reference frames = browser screenshots of the 480p video (787x448 crops), saved as PNG; colours sampled in PALETTE.md.
- Blender previews (golden-hour Cycles) are only a shape/density check; colours are tuned in Unreal (two-sided foliage
  back faces look dark in Cycles - Unreal materials use world-space soft normals instead).
- Review sheets so far: outputs/World_R01/review/Trees_R02_Blender_iter1.png.
