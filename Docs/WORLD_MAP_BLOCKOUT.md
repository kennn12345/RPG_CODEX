# Full game map terrain blockout

Built November 6, 2026 in a separate level: `/Game/RPG/Maps/L_WorldMap_Blockout`.
Open that level from the Content Browser under `RPG/Maps`. The existing startup
map remains `L_WorldVista`.

## Dimensions and source

- Exact bounds: 14,000 m east-west by 9,000 m north-south. This 126 km² rectangle
  includes ocean and mainland continuation beyond the illustrated gameplay boundary.
- Landscape: 4033 × 2521 vertices, 32 × 20 components, 2 × 2 sections per component,
  63 quads per section. Horizontal spacing is 3.4722222222 m × 3.5714285714 m.
- Actor location: (-700000, -450000, 0) cm. Scale: (347.2222222222, 357.1428571429, 100).
- North is Unreal -Y; east is +X. The map center is the world origin.
- Source data is kept in `Tools/World/WorldMapSource`, copied from the approved
  November 5 Codex export. The hand-digitized shoreline is approximate.

## Included

Editable Landscape with mainland, islands, coasts, ocean bed and freshwater lake
basin. Broad land elevations reach approximately +60 m; deep seabed approaches
-60 m. Simple green terrain and blue sea-level preview water make the blockout
legible. The water sheet has no collision and no water gameplay. The lake shares
the provisional sea-level datum used by the original heightmap.

All 19 settlement coordinates are preserved as editor-only TargetPoints under
`Planning/SettlementLocations`. These are planning markers, not constructed towns.
Their Z values follow the sampled source terrain plus 1 m. The original Greenharbor
coordinate lies slightly offshore in this approximate shoreline (terrain about
-6 m there); its horizontal coordinate was preserved rather than silently moved.

The map includes a PlayerStart above Frosthaven, simple daylight, and two overview
cameras. It uses an ordinary level with one Landscape, not World Partition. World
streaming design and performance tuning should precede full world dressing.

Mountains, volcano, glaciers, detailed riverbeds, final lake height, settlement
grading, roads, buildings, vegetation and gameplay water remain later terrain/world
development. This import completes the agreed land/water starting blockout.

## Reproduction and verification

1. `Tools/World/prepare_world_map_heightmap.py` losslessly converts the 16-bit PNG
   to little-endian R16 using standard Python only.
2. Build the Development Editor target. `RPGWorldMapImport.cpp` supplies explicitly
   invoked, editor-only import and verification console commands. They refuse to
   operate on any level other than `L_WorldMap_Blockout`.
3. `Tools/World/build_world_map_blockout.py` creates the map, materials, water and
   markers; it refuses to replace an existing map. Do not delete the map to rerun
   this script after starting manual terrain edits.
4. `Tools/World/verify_world_map_blockout.py` reopens the saved level, checks its
   exact bounds, all source height samples, marker positions and terrain collision.

Reports are in `Saved/WorldMapBlockout`; logs are `Saved/Logs/WorldMapBuild.log`
and `Saved/Logs/WorldMapVerify.log`. Existing maps and environment assets were not
rebuilt. Nothing was committed or pushed.

Verified after a fresh editor launch: all 10,167,193 height samples match the source
exactly (zero mismatches), all 640 components are present, bounds are 14,000 × 9,000 m,
and all 19 marker XY positions match the source CSV. Seven terrain collision probes
across land, coast and seabed passed (largest height error about 0.14 cm). The water's
saved NoCollision profile was verified after reload. No material compilation failures
were logged. These are import/persistence checks, not full gameplay or performance tests.
