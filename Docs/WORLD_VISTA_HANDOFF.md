# Stylized world — "hero vista" slice (Claude, 2026-11-03)

User direction: build the world like POLYART's "Dreamscape: Tower" video (youtu.be/JsJi76sL-Iw) — golden-hour fantasy
forest — as a STYLE target with original content. User does NOT want: the floating chained rocks, the tower.
Birds: on hold entirely (user hasn't decided between background sky birds and killable creatures).
Sky: must run the approved day/night cycle (concept 10_Sky_Day_Night_Cycle.png: big sun + moon, stars, Milky Way,
gradual morning / noon / sunset / night). Space stays fairly small (200 x 200 m playable) but showcases everything.
Not committed. Not user-approved yet (R02 sent for review).
2026-11-03: at the user's request L_WorldVista is now the startup map (EditorStartupMap + GameDefaultMap in
Config/DefaultEngine.ini); L_DummyField is unchanged and still what the RPG.* automation tests load.

## Run it
1. Blender meshes: `blender -b --python Tools/World/export_vista_meshes.py` -> outputs/World_R01/exports (+ vista_layout.json)
2. Unreal map: `UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="<repo>/Tools/World/build_vista.py" -unattended -RenderOffscreen`
3. Photos: `-ExecCmds="Automation RunTests Visual.WorldShots+Visual.SkyShots" -TestExit="Automation Test Queue Empty"`
   -> Saved/Screenshots/World (9 cameras, 17.3 h) and Saved/Screenshots/Sky (2 cameras x 0 / 6.3 / 12 / 17.6 / 19 h).
   Contact sheets: outputs/World_R01/review/Vista_R02_Shots.png, Sky_R01_Shots.png.

## NPCs
Tools/World/add_vista_npcs.py copies the four BP_RPGNpc from L_DummyField (Standard_Bandit, Light_Scout, Heavy_Brute,
Steady_Veteran - all instance settings, via editor copy/paste) into the meadow 9-26 m ahead of the PlayerStart (which
now faces north up the path), adds NavBounds (200 x 200 m, navmesh is runtime-dynamic) and camera WS_14_PlayerSpawnNPCs.
build_vista.py runs it at the end, so rebuilds keep them. Re-run it after changing NPCs in L_DummyField.
Gotcha: right after load_level traces miss everything (no physics bodies yet) - it traces a temporary terrain copy.

## Deep water (2026-11-03)
Pool_Deep: 13 m radius, 5.3 m deep swimming hole at Blender (-45, -25) with a 2.6 m jump ledge on its north rim (LEDGE in
export_vista_meshes.py; walkable back slope, sheer drop); zone half-depth 450. Ponds deepened to ~2.4 m (POND_DEPTH).
No swimming: head-under characters walk on the bottom (slower, low gravity) - URPGWaterZoneComponent as before.
Fixes: underwater post-process had the default 1 m BlendRadius (tinted the view above the water) -> BlendRadius 0 and
BlendWeight driven per frame by the camera's immersion under the wavy surface. "Standing on the water" was shallow
ponds (0.7-1.2 m) - nothing collides at the surface (verified); the far-land skirt under the terrain is now 9 m down
(it poked through a deepened pond bed). Test RPG.Water.VistaPools drops a walker and the player into every body of water.

## Rocks = the Blender rock library
Tools/World/import_rocks.py -> /Game/RPG/Environment/Rocks/{Meshes,Materials,Foliage}: all 21 Rocks_R01 rocks (6 sizes x A/B/C +
3 pebble clusters) with their UCX hulls (counts match rocks_manifest.json), M_Rock_Stone (world-projected painted stone,
tint toned from Blender's blue-grey toward neutral for the warm vista sky) + MI_Rock_Stone_Moss on Large A/C,
MediumLarge A, Medium B, Nanite, FT_* per rock. Safe to re-run with the editor closed: keeps existing rocks and foliage
types, re-imports only missing / wrong-sized ones and checks size vs the manifest. Needs the classic FBX importer
(Interchange.FeatureFlags.Import.FBX 0) - Interchange imported a rock 100x too big without hulls.
The vista scatters every variant (rocks keep off the path, the spawn/NPC clearing SPAWN_CLEAR, and water) plus shore
stones; the generated SM_Vista_Boulder_* are retired. Re-creating a deleted FoliageType in one session returns None -
foliage types are updated in place. Props/ copies of 5 rocks belong to L_WaterTest and are untouched.

## Reusable assets (R03)
/Game/RPG/Environment/World/{Meshes,Materials,Foliage}. Meshes carry their materials (drag in as-is); SM_Vista_Wheat and
SM_Vista_Daisies are their own meshes; SM_Vista_MistSheet (no collision). Foliage/FT_Vista_* = Foliage-tool types for
grass, tall grass, wheat, poppies, daisies, the 3 round trees, far tree, 2 boulders. Round trees + cliff ivy use the
Trees_R01 folded maple leaf card (same atlas, LEAF_RAMP colours, M_Vista_Leaf); dark fill blobs = M_Vista_Canopy;
trunks have UCX collision. Counts: poppies 12k, daisies 4k, 3 firefly clouds x 140.
Performance: grass/flowers/wheat/far trees/drifting cards cast no shadows; terrain, far land, boulders are Nanite
(materials flagged used_with_nanite) -> the VSM "Non-Nanite Marking Job Queue overflow" warning is gone. The
LogCrowdFollowing "Unable to find RecastNavMesh" warning fires during world cleanup on map load and is project-wide
(also in L_DummyField logs), not vista-specific. Close the Unreal Editor before running the builder (locked files).

## What exists (R02)
- C++ ARPGSkyCycle (RPGSkyCycle.h/.cpp): TimeOfDay (0-24, editable, animates in game; DayLengthMinutes 24),
  SunriseYaw 140 (sun sets at yaw -40, behind most cameras' view), NoonTilt 32. Moves real Sun + Moon directional
  lights (atmosphere lights 0/1, movable), blends sun colour, moon, sky light, haze colour, exposure bias.
  Writes custom primitive data to SkyReceivers (dome, mist, fireflies): 0-2 sun dir, 3-5 moon dir, 6 night, 7 sun height.
- Sky dome M_Vista_Sky (Is Sky, on /Engine/EngineSky/SM_SkySphere scaled ~30 km): authored gradient keyed to sun
  height (noon blue / golden / purple dusk / navy night), big soft sun disc + glow, big cratered moon, twinkling stars,
  star dust, Milky Way band. Volumetric clouds = engine material instance MI_Vista_Clouds (coverage default -0.05).
- Water (the stylized water from Docs/WATER_UNREAL_HANDOFF.md): lake (MI_StylizedWater_Lake), 4 ponds, River_A
  (spring pond -> lake) and River_B (cliff-foot spring -> west pond) as ARPGStylizedRiver with zones (wading etc).
  Terrain beds are carved below each water level out to the shore; river surfaces follow the ground downhill and ease
  into the lake/pond level (vista_layout.json: lake_z, ponds, rivers[].points x,y,z).
- Terrain: vertex R = dirt path, G = wet river bed / shoreline (terrain material blends wet earth).
  SM_Vista_Far: ring from the terrain edge out to 2.6 km (hills, low ridged mountains), uses the terrain material.
- Meshes: stacked-ledge cliffs with ivy (Cliff_A/B), round fluffy trees (Tree_Round/Tall/Wide: leaf cards + dark fill
  blobs, bent spherical normals, imported with IMPORT_NORMALS), cheap far trees, pollen motes / fireflies (glow more at
  night) and drifting leaves (WPO drift, UV1 = card centre so cards wrap whole), lake/pond mist sheets.
- ARPGScatter additions: bSkipWater (+ WaterMargin) skips ground under any water zone and hits on water actors;
  ExcludeCircles (x, y, radius) keeps trees off the lake/pond shores.
- Kept from R01: grass 60k, tall grass, wheat field, poppies (now 26k) + daisies, boulders, hero ancient tree, path.

## Gotchas (learned the hard way)
- SkyAtmosphereLightDirection node points TOWARD the light, but falls back to straight up while that light's intensity
  is 0 (sun at night) -> unusable for day/night. Use the cycle's custom primitive data instead.
- SkyAtmosphereViewLuminance on the dome rendered black here; height fog also covered the dome until
  fog_cutoff_distance (2e6 cm) - sky colour is authored in the dome material now.
- Material Parameter Collection expressions created from Python failed to compile ("Default Material will be used").
- Engine /Engine/BasicShapes/Plane has collision: scatter traces landed on the mist sheets (grass floating on water).
- Script-created materials need used_with_instanced_static_meshes = True (scattered instances otherwise grey).
- Scatter order: water first (zones must exist), ground cover, then trees/rocks.
- create_icosphere's returned verts include removed ones (filter is_valid); BMVert refs don't survive -> shading
  normals are stored in float vertex layers (NKEYS) and applied after the mesh is built.
- FBX import mirrors Blender Y (YS = -1); UV V is flipped by the importer (drifter centres store 1 + y).

## Honest gaps / next
- Golden hour (17.3) reads pastel/hazy rather than the reference's saturated warm light; rocks pick up lilac from the
  sky ambient. Sun disc at 17.6 sits behind the far hills from SKY_A.
- Clouds are few/indistinct (concept wants rounded puffs). Night ground still somewhat bright.
- Round trees read a bit "topiary" up close; fluffy canopies still short of the reference. No hanging ivy on trees.
- Lake mist barely visible. Rivers look foamy-white (shallow). Not tested by hand with the GASP player in this map.
- Birds intentionally absent (user decision pending).

## Asset pipeline (asset-restyle branch, 2026-11-03) - assets and layout are SEPARATE now
- `Tools/World/build_vista_assets.py [builders]` creates/updates ASSETS ONLY, IN PLACE (materials cleared + rebuilt in
  the same asset, meshes re-imported into the same asset, foliage types updated) -> L_WorldVista picks changes up without
  a rebuild. Builders: textures, materials, review, ground, grass, flowers, trees_round, tree_ancient, trees_forest,
  details, cliffs, effects, frozen ("all" = every one). Every import is checked: size vs the Blender manifest (new
  assets) or vs the previous asset (restyles keep their size; re-imports at a corrected scale if > 5% off) and convex
  hull count. Problems print as `ASSETS_PROBLEM ...`, the run ends with `ASSETS_DONE problems=N`.
  Never rebuilt: M_Vista_Sky, MI_Vista_Clouds, the water (frozen by the user).
- `Tools/World/build_vista.py` = LAYOUT ONLY (loads assets by path, never creates/deletes). Do not run it for restyles.
- Blender sources: `Tools/World/export_vista_assets.py` -> outputs/World_R02/exports (+manifest.json, previews/):
  round trees R02 (R01 trunk/branches/domes replayed with the same seeds; leaf-bunch canopy + LOD1/LOD2), grass, tall
  grass, tuft, wheat (own mesh with ears), poppies, daisies (own mesh), bushes A/B, reeds, path stones, fallen leaves,
  SM_Review_GroundSwatch. `export_vista_meshes.py` stays the R01 layout source (terrain, far land, vista_layout.json).
  Blender trees: outputs/Trees_R02 (see its HANDOFF.md; R01 untouched).
- Review stage: `Tools/World/build_asset_review.py` -> /Game/RPG/Maps/L_AssetReview (golden hour 17.3 frozen, same sky
  and grade as the vista, stations laid out relative to the sun, cameras `AS_<station>_Lit/_Back/_Close`).
  Photos: `-ExecCmds="Automation RunTests Visual.AssetShots"` (env RPG_ASSET_SHOTS=<prefix> = only those cameras)
  -> Saved/Screenshots/Assets. Side-by-side sheets: `Tools/World/make_sheet.ps1` -> outputs/World_R01/review.
- Reference frames + sampled palette: outputs/World_R01/reference (Ref_t0NN_*.png, PALETTE.md).
- Foliage materials use world-space soft normals (`soft_normal`): tangent-space normals flip on the back face of
  two-sided cards -> dark patches; the world-space vertex normal (bent / pulled up) is the same on both faces.
- R02b (2026-11-03): painted textures from `Tools/World/vista_textures.py` (Stone_R02 albedo + height + normal, bark
  relief, grass blade, petal, ground grass/dirt) -> builder "textures". New forest-floor assets (moss patch, fern,
  bellflowers, blue flowers, clover, mushrooms, log + stump with UCX, branches, ivy curtain) in builder "details".
  Review gallery cameras AS_Item_NN_<asset> in L_AssetReview give one framed preview per asset.
  In-place re-import gotcha: the new geometry lands on a NEW material slot - always use set_all()/assign().
- Detail placement (user OK): Tools/World/place_vista_details.py -> Detail_* ARPGScatter actors in L_WorldVista (folder
  Details), re-runnable. ARPGScatter gained NearPathMax (band along the path), IncludeCircles (keep-in circles: tree feet,
  rock rings, shores) and bIgnoreOtherScatters (trace the ground only); narrow keep-in areas get 60 tries per instance.
  Photo cameras keep a 3.5 m clear circle. Traces need a temporary terrain copy right after load_level (no physics yet).
