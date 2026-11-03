# Asset restyle brief (approved by the user, 2026-11-03)

Project: `C:/UNREAL PROJECTS/RPG_CODEX` (Unreal 5.8). Read `Docs/WORLD_VISTA_HANDOFF.md` and `Docs/ASSET_RESTYLE_PROGRESS.md`
first. The user is away: work autonomously, never wait for approval of a mesh - save each finished asset and move on.

## Goal
Restyle the world's assets one at a time so they look like the Dreamscape: Tower video (https://youtu.be/JsJi76sL-Iw):
match its quality and style, but every asset stays our own original work (no copied meshes). No floating chained rocks,
no tower, no birds.

## Performance: at least 60 FPS
Quality like the video; 60 FPS comes from spreading and drawing, not from worse assets: clusters/patches and open space,
full density near the player and lighter further out, LODs with smooth transitions, cull distances by size,
simplified/impostor forms far away, Nanite for big opaque pieces, no shadows from small foliage, shared materials and
instances, limited masked overdraw, instancing. Check the message log for performance warnings after each change.

## Do not change at all
Water (lake, ponds, rivers, deep pool, `RPGWaterZoneComponent`), the sky and day/night cycle (`ARPGSkyCycle`,
`M_Vista_Sky`), the world map layout / startup map: do NOT run `Tools/World/build_vista.py`, do not edit `L_WorldVista`
(it picks up edited assets automatically).

## Keep the shape, restyle the look
- Trees: the user's Blender ancient + four forest trees (`C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs/Trees_R01`,
  concepts `.../outputs/Approved_Environment_References/01_*, 02_*`) and the round world trees `SM_Vista_Tree_Round/Tall/Wide`.
  Keep silhouette, proportions, trunk lean, branch layout, leaf-cluster positions, size (ancient much larger), collision.
  Restyle leaf cards, clumping into soft readable masses, canopy colour ramp + backlit glow, bark colour/texture + moss,
  painterly smoothness, root-ground contact, gentle sway + flutter; LODs for every tree. `SM_Vista_Tree_Far`: match colours.
- Rocks (`.../outputs/Rocks_R01`, `/Game/RPG/Environment/Rocks`, `Tools/World/import_rocks.py`): keep silhouettes, sizes,
  collision; restyle colour, value range, surface smoothness toward the video; fix the purple/mauve cast.

## Free to change, add or replace
Grass; poppies + daisies (+ new flower types if the video has them); wheat; ground (`M_Vista_Terrain`: grass/dirt colours,
path colour/edges, grass->dirt, dirt->shore, ground->rock transitions); cliffs + ivy; mist, pollen, drifting leaves; new
detail pieces (path tufts, small bushes, fallen leaves, path-edge stones, reeds) as reusable assets with Foliage Types.

## Pay attention to
Small details up close, no hard seams between grass/dirt/rock/roots/water, soft painterly smoothness, colour harmony in
the video's palette under golden-hour light.

## How to work
1. Split the build: `Tools/World/build_vista_assets.py` updates assets only, one by name, in place, with a size + collision
   check (see `rock_mesh()` in `Tools/World/import_rocks.py`); `build_vista.py` becomes layout-only (don't run it). Review map
   `/Game/RPG/Maps/L_AssetReview` (neutral ground, golden-hour light, fixed cameras) + a `Visual.AssetShots` test
   (pattern: `Source/RPG_CODEX/RPGWaterShots.cpp`).
2. Blender trees/rocks: new versioned outputs `Trees_R02`, `Rocks_R02`; never overwrite R01.
3. Reference frames from the video via the built-in browser (480p via `setPlaybackQualityRange('large')`), saved to
   `.../outputs/World_R01/reference/`.
4. Per asset: render in L_AssetReview, side-by-side sheet (reference | ours) in `.../outputs/World_R01/review/`, change,
   re-render, up to ~4 iterations, save, commit, next.
5. Order: trees -> grass + flowers (incl. wheat) -> ground + path -> transitions + detail pieces -> rock colour/surface ->
   cliffs + ivy -> small effects.

## Git, safety, resuming
- Branch `asset-restyle` (from `world-vista`); commit each finished asset locally; never push. Never stage the user's
  unrelated files: `Config/DefaultEditor.ini`, `Content/RPG/Animations/FightStance/RPG_FightStance.uasset`, `Blender/`,
  `Docs/CLAUDE_CURRENT_HANDOFF.md`, `Docs/ENVIRONMENT_ASSET_HANDOFF.md`, `Docs/GETUP_HANDOFF.md`,
  `Docs/NERVOUS_SYSTEM_STAGE1.md`, `Tools/Blender/*.py`. Commit messages end with
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- If an `UnrealEditor` process is running, never kill it: stop and note it in the progress file.
- `Docs/ASSET_RESTYLE_PROGRESS.md`: `Last update: <ISO time>`, asset queue with status, commits, blockers; `STATUS: ALL_DONE`
  when finished. It is how a later run resumes after a usage limit.
- Gotchas (classic FBX importer flag, foliage types updated in place, material usage flags, Blender bmesh quirks, run
  commands): see `Docs/WORLD_VISTA_HANDOFF.md`.
