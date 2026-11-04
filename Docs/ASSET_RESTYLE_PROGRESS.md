# Asset restyle progress

Last update: 2026-11-04T04:21:26.697Z
Branch: asset-restyle (from world-vista cfff82f). Local commits only, never pushed.
Brief: Docs/ASSET_RESTYLE_BRIEF.md. Handoff: Docs/WORLD_VISTA_HANDOFF.md ("Asset pipeline" section).

## Step 1 of the quality plan (user 2026-11-03: LOD popping, angular shapes, see-through trunk slits)
- Sources DONE: LODs are subsets of the LOD0 cards (same leaves, per-card seeds; keep 1/.5/.22, grow 1/1.3/1.75),
  trees switch at 0.3/0.1 (was 0.4/0.13); round-tree trunk = one 16-sided smooth sweep (R01 two open tubes slit
  halfway up); smooth bark everywhere; Blender-tree LOD1/2 drop twigs instead of decimating (no holes); leaf atlas
  colour-dilated + 3% edge fade (no dark halo / straight cuts). Verified in Blender (closed trunk, faces outward).
- RESOLVED (user: "Yes, close it"; closed cleanly): was blocked - the user opened the Unreal Editor at 17:52 (PID 6536, L_WorldVista loaded) - it locks every asset in the
  world, so the import saved ONLY the 4 forest trees; round trees, ancient, bushes, details and R02 textures failed
  to save. build_vista_assets.py now refuses to run while another editor is open and reports failed saves.
  Imported + verified in Unreal (no slit, LOD sweep stable): review/Unreal_R02b_Step1_BeforeAfter.png, Unreal_R02b_LOD_Sweep.png.
  NEXT (user decides): step 2 = one hero tree at a time against the video. Was: run build_vista_assets.py "textures trees_round tree_ancient trees_forest details",
  then Visual.AssetShots (AS_LOD_* sweep, AS_Seam_RoundTrunk) and compare with Saved/Screenshots/Assets_before_R02b.

## Step 2: hero tree (user: "start from here", 2026-11-03)
- User 1080p screenshots: World_R01/reference/User_01..07 (+ USER_REFS.md style spec). webp ones do not load in
  make_sheet (System.Drawing) - use the jpg ones (User_02/03/05) for sheets.
- New asset SM_Vista_Tree_Hero01 (export_vista_assets.hero_tree, builder "hero"), leaf atlas Leaf_Dapple_Atlas
  (small round leaves, vista_textures.py "dapple") + M_Vista_LeafDapple. NOT placed in the vista; the round trees are
  unchanged until the user approves the hero. Review: L_AssetReview AS_Hero_* (grove of 3).
- Iterations: 1 poplar-narrow crown -> 2 wider branches/bigger clumps -> 3 more clumps (mid-branch), lower canopy,
  feathered edges, lighter cores -> 4 (user: "Fuller, bigger crown") clumps 1.85-2.7 m, 11 branches, 6-8 crown clumps,
  stronger root flare -> 5/6 (user: "leaves look partially there"): denser dapple atlas (200-230 leaves per cell),
  nearly flat cards (fold 3-9 deg) facing outward from each clump + a crossed partner on half of them (a leaf face from
  every view; up-facing cards smeared from the side). 22k/15k/7k tris -> 7 (user: "keep refining"): bigger readable
  leaves (105-125 per cell, 10-14% of a cell), draped lower clump halves, hero_ramp teal shade -> warm lime, leaf SSS 1.3;
  18.6k/12.8k/6.3k tris -> 8 ("keep refining"): smaller mid clumps + small light cores (more air), softer cooler lighter
  shade ramp, LeafDapple saturation 1.05; 14.9k/11k/5.8k tris -> 9 (user: "bunches should be rounder, look like flat stacked renders"): per-VERTEX
  colour + normal from each clump ball (bunch_card takes callables), cards tangent to the ball, rounder clumps,
  LeafDapple up-bias 0.03 -> 10/11 (user: "make leaves grow from branches, fuller shapes supporting the growth"):
  hero_tree rewritten - twigs alternate along the outer 82% of every branch in 3 tiers (up/level/drooping) + a
  terminal fan; folded cards along each twig point the way it grows (bunch_card tip_dir), mostly face-up, drooping
  tips; per-vertex shading from each branch spray ellipsoid; twig tubes only in LOD0. Wider level branches (60-76 deg).
  31.5k/13k/8k tris -> 12 (user: "clumps like round bushes, sprays look flat stacked"): each branch carries a chain
  of 2-3 rounded leaf bushes (BUSH_R 1.7 m, big mid-branch -> small at the tip); cards tangent on a fibonacci sphere,
  leaning along the branch, per-vertex ball shading, dark core 0.6 r; 15.3k/11.8k/6.6k tris.
  Sheet: review/Unreal_Hero01_iter12_Bushes.png -> user: "before looked better" -> 13: REVERTED to the iter-11 branch
  sprays (export_vista_assets.py from 67da4b5) + fuller: leaf 1.9, twig_step .24, all tiers, cards every 0.2 m of twig;
  42k/17k/10k tris (optimise once approved). Sheet: review/Unreal_Hero01_iter13_FullerSprays.png
  -> 14/15 (user: "install your pick and implement it"): Fab pack Mayu's Stylized Trees (Standard License, added by the user
  to Content/StylizedTreesByMayu - NOT committed: Fab content, re-add from Fab on another machine; its M_StylizedTree got
  the instanced-mesh usage flag). Hero leaves = MI_Hero_MayuLeaves (M_StylizedTree, mask T_Leaf_Billboard_Texture_3,
  video palette, emissive 0.05), cards use the whole texture (HERO_FULL_UV). Pack elm placed in the hero grove (autumn
  red). Inspector: Tools/World/inspect_mayu_pack.py. Sheet: review/Unreal_Hero01_iter15_MayuLeaves.png.
  DECIDED (user 2026-11-03): keep the pack leaves (MI_Hero_MayuLeaves) on the hero tree.
  16/17 ("keep refining"): pack "Texture Size" only drives its billboard mode (no effect on our cards) -> own
  T_Hero_LeafSpray_2x2_Mask (2x2 tile of the pack spray, rotated) = half-size leaves at no geometry cost; Color 1
  lighter blue-green, SmoothStep Min 0.2, Emissive 0.12 (lifts shade). Sheet: review/Unreal_Hero01_iter17_SmallerLeaves.png.

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
