# Asset restyle progress

Last update: 2026-11-03T17:50:00-06:00
Branch: asset-restyle (from world-vista cfff82f). Local commits only, never pushed.
Brief: Docs/ASSET_RESTYLE_BRIEF.md. Handoff: Docs/WORLD_VISTA_HANDOFF.md ("Asset pipeline" section).

## Blockers
- 2026-11-03 15:27 -> still at 17:50: an interactive UnrealEditor.exe (PID 9524, started 14:59 by the user on
  RPG_CODEX.uproject) is running. Per the brief it is NOT killed -> no Unreal import / render / C++ build yet.
  All non-Unreal work for every asset group is done (below). NEXT RUN, once the editor is closed, in order:
  1. C++ build (Visual.AssetShots test in Source/RPG_CODEX/RPGWaterShots.cpp - written, NOT compiled yet):
     Build.bat RPG_CODEXEditor Win64 Development -Project=... -WaitMutex -NoHotReload
  2. UnrealEditor -ExecutePythonScript="C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/build_vista_assets.py all"
     -> check the log for ASSETS_PROBLEM, LogPython: Error, Failed to compile, Error saving. First real run of the
     script: expect API fixes (set_lods: StaticMeshEditorSubsystem.import_lod / set_lod_screen_sizes names unverified;
     soft_normal sets tangent_space_normal False; texture import options). Run builders one by one if it fails
     (e.g. "textures materials", then "trees_round", ...).
  3. UnrealEditor -ExecutePythonScript=".../Tools/World/build_asset_review.py" (makes L_AssetReview)
  4. -ExecCmds="Automation RunTests Visual.AssetShots" -TestExit="Automation Test Queue Empty" -> Saved/Screenshots/Assets
  5. Sheets with Tools/World/make_sheet.ps1 (reference | ours) -> outputs/World_R01/review; iterate each asset
     (max ~4 iterations), commit per asset group; also Visual.WorldShots for the vista + check the log for
     performance warnings.

## Queue (Blender = source done, Unreal = import + review in L_AssetReview still pending for ALL)
| # | Step | Status |
|---|---|---|
| 0 | Branch, reference frames + palette (World_R01/reference, PALETTE.md) | done |
| 1 | build_vista_assets.py (assets only, in place) + build_vista.py layout-only | done (script); first Unreal run pending |
| 2 | L_AssetReview map script + Visual.AssetShots test | written; C++ build + run pending |
| 3 | Trees: Trees_R02 (ancient + 4 forest, LODs) | Blender done (outputs/Trees_R02, HANDOFF.md); Unreal pending |
| 4 | Trees: round world trees R02 + LODs; far tree colours (M_Vista_Canopy palette) | Blender done (World_R02); Unreal pending |
| 5 | Grass + flowers + wheat | Blender done (grass/tall/tuft/wheat/poppies/daisies + LODs), materials written; Unreal pending |
| 6 | Ground + path (M_Vista_Terrain) | material rewritten (palette, ragged path edge, damp rim, sand/wet shore, warm rock); swatch exported; Unreal pending |
| 7 | Transitions + detail pieces | bushes A/B, tuft, reeds, path stones, fallen leaves + FTs (builder details); edges in M_Vista_Terrain; Unreal pending |
| 8 | Rocks colour/surface | Blender Rocks_R02 done (bevel + vertex colour, dims within 1%, same hulls); builder rocks + M_Rock_Stone R02; Unreal pending |
| 9 | Cliffs + ivy | Blender done (R01 slabs replayed, edge light, thick ivy); M_Vista_Rock = stone graph; Unreal pending |
| 10 | Small effects (mist, pollen, drifting leaves) | materials restyled (round additive motes, masked leaf shape, mist 0.3); Unreal pending |

## Commits
- 84f52c4 Split world asset build from layout; add R02 restyle sources and review stage
- 33d54cb Restyle rocks, ground, cliffs and small effects toward the reference palette

## Notes
- Reference frames = browser screenshots of the 480p video (787x448 crops), saved as PNG; colours sampled in PALETTE.md.
- Blender sources outside the repo: outputs/Trees_R02/source, outputs/Rocks_R02/source (rocks_restyle.py, rocks_preview.py);
  repo: Tools/World/export_vista_assets.py (-> outputs/World_R02).
- Blender previews (golden-hour Cycles) are only a shape/density check; colours are tuned in Unreal (two-sided foliage
  back faces look dark in Cycles - the Unreal materials use world-space soft normals instead).
- Review sheets so far (Blender stage): outputs/World_R01/review/Trees_R02_Blender_iter1.png, Rocks_R02_Blender_lineup.png,
  Cliffs_R02_Blender.png.
- make_sheet.ps1: run it through the PowerShell tool, or powershell -ExecutionPolicy Bypass -File (scripts are disabled
  by policy for plain -File).
- The vista's scatter counts/positions are layout (not changed): density changes come from the meshes themselves
  (blades per clump, LODs, cull distances in the foliage types).
