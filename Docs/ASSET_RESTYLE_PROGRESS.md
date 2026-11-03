# Asset restyle progress

Last update: 2026-11-03T23:50:55.300Z
Branch: asset-restyle (from world-vista cfff82f). Local commits only, never pushed.
Brief: Docs/ASSET_RESTYLE_BRIEF.md. Handoff: Docs/WORLD_VISTA_HANDOFF.md ("Asset pipeline" section).

## Status
First full restyle pass is DONE and in Unreal (round 2 of refinements, user request 2026-11-03: "more texture, same
quality everywhere, add moss and small details from the video"). The user closed-editor blocker is gone (the user said
"close unreal and continue", 16:47; it closed cleanly). Not ALL_DONE: see "Next iteration" - the user reviews first.

Unreal previews (send/open these): outputs/World_R01/review/
- Unreal_R02_Gallery_1600.png / _1730.png - every asset alone in L_AssetReview (afternoon gold / vista start light)
- Unreal_R02_vs_Reference.png - video frames next to ours
- Unreal_R02_WorldVista.png - L_WorldVista with the new assets (layout, water, sky untouched)
Raw shots: Saved/Screenshots/Assets (AS_*_16.00 / _17.30), Saved/Screenshots/World.

## Queue
| # | Step | Status |
|---|---|---|
| 0 | Reference frames + palette | done |
| 1 | build_vista_assets.py (in place, checked) + build_vista.py layout only | done, runs clean (ASSETS_DONE problems=0) |
| 2 | L_AssetReview + Visual.AssetShots (16:00 + 17:30, gallery AS_Item_*) | done |
| 3 | Trees_R02 ancient + 4 forest (LOD0-2, screen sizes 1/.4/.14, ancient 1/.3/.1) | done in Unreal |
| 4 | Round trees R02 + LODs, far tree colours | done in Unreal |
| 5 | Grass/tall/tuft (LOD 1/.22/.07), wheat (own mesh), poppies, daisies | done in Unreal |
| 6 | Ground (M_Vista_Terrain: painted turf + dirt/pebble textures, ragged path edge, shore bands) | done in Unreal |
| 7 | Detail pieces: bushes, tuft, reeds, path stones, fallen leaves + R02b moss patch, fern, bellflowers, blue flowers, clover, mushrooms, log, stump, branches, ivy curtain (all with FT_*) | done in Unreal |
| 8 | Rocks (Rocks_R02 + painted triplanar stone + relief normals, warm-neutral) | done in Unreal |
| 9 | Cliffs + ivy (stone graph, thicker ivy) | done in Unreal |
| 10 | Effects (round additive motes, masked drift leaves, mist 0.3) | materials done (not separately reviewed) |

## Commits
- 84f52c4 Split world asset build from layout; add R02 restyle sources and review stage
- 33d54cb Restyle rocks, ground, cliffs and small effects toward the reference palette
- d9c2320 Update asset restyle progress
- 67291b3 Painted textures + relief, new forest-floor details, first Unreal import + review
- 139b0f1 Place the detail pieces in L_WorldVista; ARPGScatter NearPathMax / IncludeCircles / bIgnoreOtherScatters; wheat 0.7-1 m

## Next iteration (if the user asks / scheduled run)
- Cliffs: ivy still lighter than the video's thick mats; slabs could use more stepped breaks.
- Rocks at 17.3 h in the vista pick up a pink cast from the sunset sky (albedo is neutral-warm now).
- DONE (user OK 2026-11-03 "Yes, place them"): Tools/World/place_vista_details.py adds Detail_* scatters to L_WorldVista
  (path tufts/stones/bushes, tree-foot ferns/moss/mushrooms/clover/leaves/branches/logs/stumps, rock moss, shore reeds,
  wildflower drifts); re-runnable, nothing else in the map touched. Sheet: review/Unreal_R02_WorldVista_Details.png.
- FPS not measured in a packaged/PIE run; triangle budgets: grass 228/~60/10 tris per clump by LOD, trees 6-15k LOD0.
- make_sheet.ps1: run through the PowerShell tool (scripts are disabled for plain -File).

## Gotchas found this round
- In-place FBX re-import keeps the asset's OLD material slot 0 and adds the new geometry on a NEW slot -> set_material(0)
  left grass/rocks on WorldGridMaterial (orange in golden light). build_vista_assets.set_all() sets every slot.
- "Failed to compile ... missing input" lines while building materials are intermediate compiles of half-built graphs;
  the final compile is clean (check the shots log, not the build log). tangent_space_normal is switched in finish().
- Normal maps (TC_NORMALMAP) must be sampled with SAMPLERTYPE_NORMAL (values already -1..1).
- An editor Python error before quit_editor() leaves the editor running - kill it before the next run.
