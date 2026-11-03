# Stylized water in Unreal — handoff (Claude, 2026-11-02)

User direction: the Blender water was "very far off"; target = POLYART "Dynamic Stylized Water for Unreal Engine"
(youtu.be/s9BxG1nPyxM: bold white cellular foam, very clear shallows, white river streak dashes, sun glints,
shore foam, real waves, underwater effects). User chose "Build it in Unreal", palette "in between": concept green
shallows -> video turquoise -> video deep blue. Not user-approved yet. No commits made.

## What exists (all new; startup map untouched)
- C++ `ARPGStylizedRiver` (Source/RPG_CODEX/RPGStylizedRiver.h/.cpp): spline river of any length. Bends one straight
  water strip (SM_Water_River_Strip10, lies along +X, 8 m wide, 10 m long) along the spline in ~PieceLength pieces;
  spline point scale Y = width multiplier; Width (cm). Each piece gets DEFAULT custom primitive data 0/1 = flow-UV
  offset/direction so the flow texture is seamless across pieces. No collision on water. `Rebuild()` is CallInEditor.
- `M_StylizedWater` (/Game/RPG/Environment/Water, Single Layer Water, used with spline meshes) + MI_StylizedWater_River
  (IsRiver 1) / MI_StylizedWater_Lake (IsRiver 0). One Custom HLSL node does: water depth from
  SceneDepthWithoutWater - PixelDepth; colour ShallowC (120,190,110) -> MidC (25,175,175) -> DeepC (12,82,150) over
  DeepDepth 170 cm; opacity ShallowOpacity .10 -> DeepOpacity .86 (clear shallows); river streak dashes on flow UV
  panned downstream (FlowSpeed); bold cellular foam wherever the water is shallow against ANY surface (FoamWidth 28 cm,
  FoamCell 22 cm) - shorelines, rocks, anything placed in water, live, no baking; animated fbm normals; roughness .04,
  specular .6 (sun glints). All are material parameters for tuning in the instances.
- Test map /Game/RPG/Maps/L_WaterTest: 70 m S-curve river (ARPGStylizedRiver), lake (SM_Water_Lake), test terrain
  from the Blender kit (complex collision, earth texture + underwater olive tint + caustics in M_TestTerrain_Earth),
  rocks/grass/trees from the R01 asset folders, sun/sky atmosphere/sky light/fog/post, 4 cameras tagged WaterShot.
- Tools/Water/build_water_test.py rebuilds all of it (imports, materials, map); export_test_terrain.py and
  export_river_strip_x.py are the Blender exporters it relies on.
- Visual.WaterShots automation test (Source/RPG_CODEX/RPGWaterShots.cpp) photographs every WaterShot camera to
  Saved/Screenshots/Water/ after shaders finish compiling. Latest run: Success. Comparison:
  C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs/Water_R01/review/Unreal_Water_R01_Shots.png

## Run
1. Build: Engine/Build/BatchFiles/Build.bat RPG_CODEXEditor Win64 Development -Project=<uproject> (use PowerShell; Git
   Bash mangles the quoted .bat path).
2. UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="C:/UNREAL PROJECTS/RPG_CODEX/Tools/Water/build_water_test.py"
   -unattended -RenderOffscreen   (rebuilds assets + map, saves, quits)
3. UnrealEditor.exe RPG_CODEX.uproject -ExecCmds="Automation RunTests Visual.WaterShots"
   -TestExit="Automation Test Queue Empty" -unattended -RenderOffscreen -windowed -ResX=1280 -ResY=720

## Gotchas found (each cost a debugging round)
- Blender FBX -> Unreal mirrors Y. The river strip is exported pre-turned to lie along +X (export_river_strip_x.py).
- FBX re-import with replace_existing keeps stale import settings: the builder deletes the asset before importing.
- SetCustomPrimitiveDataFloat is runtime-only (not saved, not copied to PIE): use SetDefaultCustomPrimitiveDataFloat.
- Rocks_R01 FBXs import at 1/100 scale (their export's unit setting); the builder imports them at x100 for now.
  The rock exports themselves should be fixed.
- Adding source files changed unity batching and exposed a name clash: renamed a local `Segments` -> `SegmentSum`
  in RPGCharacterContactComponent.cpp (no behaviour change).

## Not done yet (from the video's feature list) - SUPERSEDED: see the update at the end
Real wave displacement (WPO/Gerstner) and an ocean; animated shore waves rolling in; crest foam on waves; foam is a
thin line around rocks (video's is thicker - raise FoamWidth or add a second band); underwater fog/god rays/caustic
light function; buoyancy; water volume + wading/deep-water movement with the real player (no swim animation exists -
do not claim swimming). Not tested in PIE with the player character walking into the water.

## Update: "implement everything missing" (2026-11-02, later) - all built and tested
New C++ (Source/RPG_CODEX):
- RPGWaterWaves.h: FRPGWaveSettings + Gerstner height (4 waves, fixed ratios) shared with the material's WPO.
- URPGWaterZoneComponent (box; TOP = water surface): surface height incl. waves, FindZone registry; characters inside:
  wading slows (WadeStartDepth 35 cm -> WadeSlowestFactor .6 at chest), head under -> walk along the bottom at
  UnderwaterSpeedFactor .45 with UnderwaterGravityScale .35 (floaty jumps), normal walk animations, NO swimming;
  originals restored on exit; Walking/Falling only (flight untouched); speed cap scales whatever max speed is current
  (sprint/GASP safe). Spawns an underwater post-process (its box = volume) and a caustic decal on the bed.
- ARPGWaterBody (lake/pond/ocean): zone + surface mesh at the box top. ARPGStylizedRiver now also creates one zone per
  piece (current along the spline = FlowSpeed cm/s, Waves, UnderwaterMaterial, CausticMaterial).
- URPGBuoyancyComponent (8 corner pontoons by default, lift/damping/current drag) + ARPGFloatingProp (physics mesh).
- Tests: RPG.Water.Buoyancy, RPG.Water.Wading (RPGWaterTests.cpp, headless -nullrhi); Visual.WaterShots (8 cameras).
Material (build_water_test.py): Gerstner WPO waves (WaveHeight/Length/Speed/Dir/Steep - MUST match the zone Waves;
the builder writes both from one WAVES table), wave normals + crest value, crest foam (CrestFoam), shore waves = foam
lines on depth contours moving toward the shore (ShoreWaves/Spacing/Speed/Range), rock/object foam ring from mesh
distance fields (RockFoamWidth 45 cm) in addition to depth foam, two-sided (seen from underwater).
M_UnderwaterPP (depth fog, tint, slanted light rays), M_WaterCaustics (deferred decal, animated cell-edge caustics),
sun light-shaft bloom/occlusion on. Instances: MI_StylizedWater_River / _Lake (gentle waves, light shore waves) /
_Ocean (30 cm waves, crest foam, shore waves). Test map adds an ocean + beach (SM_Water_OceanTile 160 m, 40k tris;
SM_TestTerrain_Beach from Tools/Water/export_ocean_beach.py), floating barrels/crates, a drifting river log,
TargetPoints WaterTest_Dry/Wade/Deep, cameras WS_05..08.
Results (2026-11-02): RPG.Water.Buoyancy PASS (props settle within 14-42 cm of the surface, river log drifted 4 m
downstream); RPG.Water.Wading PASS (CharacterMovement walker: dry 600 cm/s, wading 414, head-under 287, gravity .35
under water, restored on land); Visual.WaterShots PASS. Contact sheet:
outputs/Water_R01/review/Unreal_Water_R02_Shots.png.
Honest limits: the wading SPEED test uses a plain CharacterMovement walker because the GASP player's animation-driven
locomotion does not move properly headless; the zone logic (states, gravity) was also verified on the real player in
an earlier run, but the GASP player's in-water speed still needs a visual / hands-on check. Water surface height for
buoyancy ignores the small horizontal Gerstner shift. Shore waves are depth-contour foam (no breaking wave geometry);
the ocean is one 160 m tile (no infinite ocean / LOD). Player swimming does not exist by design (no swim clips).
