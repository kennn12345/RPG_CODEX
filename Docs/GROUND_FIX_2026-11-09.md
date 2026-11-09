# Test-square ground fixes — November 9, 2026 (overnight)

User request: feet clipping into a "false ground", water showing where it shouldn't, a larger/deeper/better-shaped pond
(remove assets it takes up), and no see-through bits at rock corners. Clouds and weather were left alone.

## ⚠ To make these real (they are verified but NOT saved yet)

The open editor kept the terrain, rock material and level files locked all night, so every fix was applied and checked
in a hidden editor **in memory only**. To save them:

1. Save your work and **close Unreal**.
2. Run:
   ```
   powershell -ExecutionPolicy Bypass -File "C:\UNREAL PROJECTS\RPG_CODEX\Tools\World\apply_ground_fixes.ps1"
   ```
   It refuses to run while any editor is open, applies and saves everything, then re-checks the saved files
   (collision gap and stray water) and prints a summary. Running it twice is safe (the terrain is tagged once done).
3. Reopen Unreal. If it offers to save `L_WorldVista` from an older session, don't.

Nothing is committed.

## 1. False ground (legs sinking into the floor) — cause found and fixed

The terrain (`SM_Vista_Terrain`) is a Nanite mesh with complex-as-simple collision. Nanite builds collision from its
*fallback* mesh, and with `fallback_target = AUTO` that fallback was simplified to **939 triangles** against **125,000**
visible ones. Measured every metre over the 196 × 196 m square:

| | Before | After |
|---|---|---|
| Collision triangles | 939 | all (147,920 with the new pond) |
| Spots > 5 cm off | 78 % | 0 % |
| Spots > 20 cm off | 28 % | 0 % |
| Worst: visible ground above your feet | 2.94 m | 0 |
| Worst: visible ground below your feet | 1.55 m | 0 |

Fix: `fallback_target = PERCENT_TRIANGLES`, 100 %, error 0 (`Tools/World/fix_terrain_collision.py`). The far hills
(`SM_Vista_Far`, 49,152 vs 3,065 triangles) had the same flaw and got the same fix. Cliffs and trees aren't Nanite and
were already exact. Measured by `Tools/World/measure_terrain_collision.py`.

## 2. Water where it shouldn't be — fixed

Every pond's water is a square sheet 1.6 × its radius, so any dip in the ground under the sheet's corners showed water.
`Tools/World/analyze_vista_water.py` samples every 50 cm under each sheet:

| Water | Problem | Fix |
|---|---|---|
| Deep pool | 112 m² hollow beside it, up to 84 cm under the waterline, held water (plus wading/caustics there) | hollow raised to waterline + 12 cm, its wet-earth colour faded back to grass |
| Pond 3 | 2 m² dip by its corner | same |
| River A → lake, River B → pond 2 | the river strip's square end sat ~2 cm above the lake/pond surface, its cut edge showing | last two spline points now end 12 cm under the water they flow into |
| Pond 1 | wet at its sheet edge along the river channel that feeds it | left as is (real water) |

After (final combined dry run, all fixes together): stray water 0 everywhere except a < 1 m² patch outside the new
pond's sheet (not visible); collision gap 0 on all 38,809 samples. Photos: `Saved/GroundFix/Shots/before` vs `final`.
Not changed: the existing ponds' shorelines are still stair-stepped by the terrain's 0.8 m triangles (visible from above).
The same local tessellation used for the new pond could smooth them later.

## 3. The pond — Pond 4, the one nearest the player spawn

Assumed "our pond" = Pond 4 (closest to spawn, was a small 5 m near-octagon). If you meant another, the script takes a
label (`plan_pond(..., label=...)`).

- Grown in place (old centre stays inside): **≈ 19.5 × 13.5 m, ~234 m² (about 3×), 3.2 m deep** with a 0.55 m shallow
  shelf at the edge; waterline 1.29 m. The search (`plan_pond`) picks the largest organic shape (ellipse + gentle lobes,
  7 orientations tried) that stays on the terrain square, 6 m clear of the path, and is held by the surrounding ground
  (a low lip of ≤ 80 cm built where the ground falls away). Bigger didn't fit without moving it or cutting a 3 m bank.
- Terrain tessellated to 0.4 m around it for a smooth shoreline; wet-earth shore band repainted.
- Water sheet `SM_Water_Pond_4` (the lake sheet's grid cut to a disc, `SM_Water_Disc`, then warped to the pond outline);
  zone sized to the pond; mist moved.
- **Removed 495 scatter instances** inside the new pond (grass, flowers, rocks, trees in the water/bank) and
  **re-seated 1,068** on reshaped ground.
- Photos: `Saved/GroundFix/Shots/final/pond4_*.png` (top, wide, oblique, eye level).

## 4. See-through bits at rock corners — cause found and fixed

Mesh checks found the rocks closed (holes only in MediumLarge B and Small B, 1 tiny loop each, already known). The gaps
come from the **moss material**: `M_Rock_Stone_Moss` raised the moss 3 cm with World Position Offset, pushing each
vertex along its *own* normal and gating on its own `N.z`. At sharp corners one position has several normals (split
normals), so the up-facing copy rose 3 cm and the side copy didn't → an open slit along the corner (mossy rocks only:
Large A/C, MediumLarge A, Medium B). Fix: the lift is now straight up with an amount from per-vertex data only (vertex
colour A "BroadUp", VC.g, world-position noise), identical for every copy. Same moss thickness; the slit closed in close-up
photos (`Saved/GroundFix/Shots/rocks_mossfix/zoom_LargeA_1_mossedge.png` vs `rocks_before/...`). Applied by
`Tools/World/fix_rock_corners.py`; the builder (`build_vista_assets.py`, `MOSS_LIFT`) carries the same code.
Tried and dropped: a hidden "backer" shell inside each rock (didn't address the cause, +30–70k triangles per rock).

## Tools added (Tools/World)

`run_hidden_editor.ps1` (hidden editor that stays alive for timed steps, quits on any Python error; the project path's
space breaks `-ExecCmds py`, so it uses a launcher in %TEMP%), `ground_shots.py` (scene-capture photos without a
viewport — `HighResShot`/`take_high_res_screenshot` need one), `shots_problem_spots.py`, `shots_rock_corners.py`,
`measure_terrain_collision.py`, `analyze_vista_water.py`, `analyze_rock_corners.py`, `edit_vista_ground.py`,
`fix_terrain_collision.py`, `fix_rock_corners.py`, `apply_ground_fixes.py/.ps1`, `inspect_vista_ground.py`.
C++ couldn't be built overnight: the open editor had Live Coding running.

## Still open

- Old ponds' stair-stepped shores (see 2).
- The cloud/storm work from earlier tonight is also uncommitted (separate from this).
- Not tested by walking the GASP player in PIE; collision is proven exact by measurement instead.
