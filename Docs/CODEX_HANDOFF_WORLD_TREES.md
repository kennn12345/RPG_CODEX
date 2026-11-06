# Handoff for Codex: world trees, view distances, grass flicker

> 2026-11-05 Codex follow-up: grass-material lighting stabilization and TSR adjustment implemented locally; user confirmed the flicker fixed. See [GRASS_FLICKER_CODEX.md](GRASS_FLICKER_CODEX.md) for changes, measured limits, and rollback. The historical investigation below is retained.
>
> Tree leaf follow-up: shared `MI_Hero_MayuLeaves` now uses darker green colours and zero emissive intensity, inherited by the placed Lux/Jabami trees and ancient tree. Verified in PIE at noon, sunset, and night; user aesthetic review pending. See the leaf palette entry in [ASSET_RESTYLE_PROGRESS.md](ASSET_RESTYLE_PROGRESS.md). `build_vista_assets.py leaf_palette` reapplies just this material change without rebuilding the world.

Written 2026-11-05 by Claude. Covers everything since `f705cb4` on the local branch `asset-restyle` (HEAD `a8c11c2` before
this doc). Never pushed. Background: `Docs/ASSET_RESTYLE_PROGRESS.md`, `Docs/WORLD_VISTA_HANDOFF.md`, `Docs/CREDITS.md`.

## Where things stand
- Works, confirmed by the user in the editor: Fab trees in the map, ancient tree restyle, globes removed, directional-light
  warning gone, VSM overflow warning gone, leaf LOD jump fixed, longer view distances ("looks better").
- **Open: grass flicker.** Reduced, not gone. See "Open problem" below. This is the next job.
- Not measured: FPS. Nothing has been timed in a play session since the view distances doubled and the Lumen probes got denser.

## Rules from the user
- Stay on local `asset-restyle`; do not push. Commit only files you changed.
- Never stage the user's: `Config/DefaultEditor.ini`, `Blender/`, `Docs/CLAUDE_CURRENT_HANDOFF.md`,
  `Docs/ENVIRONMENT_ASSET_HANDOFF.md`, `Docs/GETUP_HANDOFF.md`, `Docs/NERVOUS_SYSTEM_STAGE1.md`, `Tools/Blender/*.py`,
  `Content/RPG/Animations/FightStance/RPG_FightStance.uasset`.
- Ask before closing the user's Unreal Editor. Scripts cannot save while it is open (check `tasklist`; failed saves are easy
  to miss, because the log moves to `Saved/Logs/RPG_CODEX_2.log`).
- Water and sky are frozen unless the user says otherwise. Show before/after Unreal renders and give honest short notes.
- StyleHex "FREE Stylized Forest Sample" forbids AI use: never script or modify it. Keep the Mayu pack.
- User decisions: keep the fuller clumps (tighter "iter 2" clumps were rejected); leave tree-foot details where they are for
  now; no impostors / HLOD until the world is finished (the player flies fast, so far detail matters).

## Commits
| Commit | What |
|---|---|
| `ff1013c` | Fab tree foliage rebuilt as rounded clumps (`grow_fab_canopies.py`), check station in `L_AssetReview` |
| `61a3c0f` | 72 Fab trees replace the round-tree scatters in `L_WorldVista` (`place_fab_trees.py`) |
| `08432e1` | **Other session:** rock mauve cast fix, restored skipped material builds |
| `8090263` | Trees on terrain, never on assets (some traces hit sky pieces / ruins) |
| `1dd564a` | Ancient tree gets the same clumps + leaves (`SM_Tree_Ancient_Clumps`) |
| `2e2ed7c` | 26 ground pieces stuck in the ancient crown removed (`drop_floating_cover.py`) |
| `97c9e45` | Sun forward shading priority 1, Moon 0 (`fix_light_priority.py`, both map builders) |
| `4bd96ee` | Longer cull distances + shrink fade in foliage materials (`set_view_distances.py`) |
| `3fc4d42` | Distances = 2x original, table-driven so reruns never compound |
| `9617494` | `IncludeInCoarsePages=0` for the VSM job queue overflow |
| `e40ad71` | LOD jump: 70% / 30% of cards at 1.15x / 1.5x, dithered LOD on leaf master, in-place re-import |
| `2a1d118` | Non-shadow scatters out of Lumen + distance fields (`ground_cover_lumen_off.py`) |
| `a8c11c2` | Three Lumen settings; `Visual.FlickerShots` + `flicker_report.ps1` |

## What changed, by system

### Trees
- `Tools/World/grow_fab_canopies.py` (Blender, `[-- A I Jabami Ancient]`, default all): keeps pack trunk + branches, drops the
  flat leaf quads (and the ancient tree's `Fill` blobs), k-means their centres into clumps, grows folded cards over each
  clump ellipsoid (front, back, sides, underside) with normals from the ellipsoid. Seeds are stable (crc32).
  Output: `outputs/FabPacks/_canopy_fbx` (outside git). Triangles: 11-18k / 7-12k / 3-5k; ancient 61k / 46k / 38k.
- `Tools/World/restyle_fab_trees.py`: imports them **in place** into `/Game/FabPacks/Restyled`, sets LOD screen sizes
  1 / 0.15 / 0.05, turns on dithered LOD on the Mayu master, makes `SM_Lux_Tree_I_Before`, points `Tree_Hero` at
  `SM_Tree_Ancient_Clumps`. `RPG_FAB_TREES=I` or `Ancient` limits it. Re-running it recreates the `_Before` reference, so
  rebuild `L_AssetReview` afterwards.
- `Tools/World/place_fab_trees.py`: `FabTree_NN` actors (folder `FabTrees`): balanced random deck, yaw, scale .85-1.1; height
  from the terrain copy only; moves 2-8 m off rocks, cliffs, props, path, water, camera feet. 72 placed, 31 moved, 6 skipped.
  Spots saved in `outputs/World_R01/exports/vista_tree_spots.json` (the old scatters are deleted from the map).
- `L_AssetReview` section 15: `AS_TreeCheck_<Before|After>_<34|Side|Up>`.

### View distances
- `Tools/World/set_view_distances.py`: `ORIGINAL` table x `VIEW_MUL` (2.0), fade from 65%. Lower `VIEW_MUL` if FPS suffers.
  Set directly on the instance component, **no Rebuild**.
- `build_vista_assets.distance_fade()`: foliage materials shrink instances to their pivot over the fade band
  (PerInstanceFadeAmount), added to the wind WPO. Applied through `finish(m, dither=True)`.

### Project settings (`Config/DefaultEngine.ini`)
- `r.AOGlobalDistanceField.NumClipmaps=5`: longer range for rock / shore water foam. Foam result not verified by the user.
- `r.Shadow.Virtual.NonNanite.IncludeInCoarsePages=0`
- `r.Lumen.ScreenProbeGather.Temporal.DistanceThresholdForFoliage=100`
- `r.Lumen.ScreenProbeGather.Temporal.MaxFramesAccumulated=32`
- `r.Lumen.ScreenProbeGather.DownsampleFactor=8` (GPU cost not measured; removing this line keeps most of the gain)

## Open problem: grass flicker
User's description: flickering shadows on grass in some areas, by day. At night shadows exist but do not flicker. Reduced by
`a8c11c2`, still visible. User's spot: `BugItGo 580.74 7483.83 -153.97 -14.25 -76.96`.

**Ruled out by the user's eyes** (in the editor): `r.Shadow.Virtual.Enable 0`, `IncludeInCoarsePages 1`,
`r.Shadow.Virtual.Cache 0`, `ShortRangeAO 0` (back to 1 looked worse), `ScreenTraces 0`, `NumClipmaps 4`, sky cycle Animate
off, `slomo 0.01` (so not sway or the moving sun). Grass casts no shadow at all.
**Only thing that stopped it:** `r.Lumen.DiffuseIndirect.Allow 0`. Caution: that blackens shade, which may also just hide it.

**Measured at the spot** (frozen time, 1280x720, 8 px blocks, mean change between frames out of 255):
base 0.34 | Lumen off 0.12 | ForceLOD 0 0.35 | foliage.DitheredLOD 0 0.35 | no AA 0.56 | current settings 0.21 |
on top of those, no clear change: SkipFoliageHits, SpatialFilterNumPasses 3, TemporalFilterProbes, Reflections off,
TwoSidedFoliageBackfaceDiffuse 0, TSR History 200, TSR Flickering.Period 6; TAA 0.18. Worst-block numbers are noisy (2.9-5.3).

**The measurement does not see what the user sees.** Frozen numbers are near the floor while the flicker is still visible.
Phone video (banding) and a 946-frame screen capture (brightness steady, compressed) did not show it either. Likely gaps:
frozen vs live, every 6th frame, block averaging, offscreen vs the user's window. Ideas not tried: consecutive-frame capture
with no gaps at full resolution in a live run; `Allow 0` with a brightness boost, to test whether it only hides the flicker;
per-pixel rather than block metrics; asking the user for a timestamp and screen area in their capture (requested, unanswered).
Also unanswered: dark patches pulsing or fine sparkle, near or far, standing still or only moving.

Test: `RPG_FLICKER_SPOT="x y z pitch yaw" RPG_FLICKER_RUN=name RPG_FLICKER_FREEZE=1 RPG_FLICKER_FRAMES=8 UnrealEditor.exe
RPG_CODEX.uproject -ExecCmds="r.X 0,Automation RunTests Visual.FlickerShots" -TestExit="Automation Test Queue Empty"
-unattended -RenderOffscreen -nosplash` -> `Saved/Screenshots/Flicker/<run>`; then `flicker_report.ps1 -Root ... -Runs a,b -Block 8 -Heat`.

## Traps
- **Do not run `build_vista.py`:** it rebuilds the layout and would bring back the round trees and drop this work.
- **`place_vista_details.py` is a rerun with consequences:** it would re-create the Detail scatters at the original cull
  distances, and they can land on tree crowns. Run afterwards: `drop_floating_cover.py`, `set_view_distances.py`,
  `ground_cover_lumen_off.py`. The same applies to any `ARPGScatter.Rebuild` (it also resets the fade start to 80%).
- `build_vista.py` / `place_vista_details.py` still contain the original cull values, by design: the table in
  `set_view_distances.py` is the source of the doubled values.
- Tree-foot details still sit at the old tree spots (31 trees moved). User said leave it for now.
- Fab content is git-ignored (`Content/FabPacks`, Lux, Mayu) and `L_WorldVista` depends on it. On another machine: re-add from
  Fab, run `grow_fab_canopies.py` + `restyle_fab_trees.py`. Lux and Jabami need visible credit (`Docs/CREDITS.md`).
- `Tools/World/ground_cover_lumen_off.py` means ground cover no longer feeds Lumen: new scatters need it re-run.
- Comment in `DefaultEngine.ini` says 2026-11-06; the real date was 11-05.
- Unreal often segfaults on quit after saving a map (`build_asset_review`, `fix`-style scripts). The save has already happened:
  check `git status` and the log's `*_DONE` line.
- No `python` on PATH. Compile with `Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe RPG_CODEXEditor Win64 Development
  -Project=... -WaitMutex`; `Build.bat` breaks on the space in `Program Files` from Git Bash.
- `make_sheet.ps1` and `flicker_report.ps1`: run as a ScriptBlock from PowerShell (plain `-File` is blocked).
- Stills cannot show flicker, fades or LOD pops. Ask the user to look, and say so when a thing is unverified.

## Review images (`outputs/World_R01/review`, under `C:/Users/cedar/Documents/Codex/2026-11-02/ok/`)
`Unreal_TreeI_Clumps_BeforeAfter.png` (approved look), `Unreal_FabTrees_RoundClumps_BeforeAfter.png`,
`Unreal_WorldVista_FabTrees_Grounded.png`, `Unreal_AncientTree_NoGlobes_BeforeAfter.png`,
`Unreal_WorldVista_ViewDistance_2x.png`, `Unreal_WorldVista_GrassLumenOff.png`.

## Also open, lower priority
Move tree-foot details; FPS check while flying; confirm the foam range; trunk material has no LOD crossfade; ancient far LOD
is heavy (38k); Nanite trees if shadows or FPS need it; impostors / HLOD when the world is finished; cliff ivy thickness.
