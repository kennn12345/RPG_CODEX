# Reference shoreline and waterway refinement

November 6, 2026. New level: `/Game/RPG/Maps/L_WorldMap_Refined`.
The original `L_WorldMap_Blockout` remains available and was not overwritten.
Neither the startup map nor the user's open editor session was saved by this work.

The refined Landscape keeps the exact 14,000 × 9,000 m bounds, 4033 × 2521 samples,
and 640 components. Shorelines follow the illustration more closely: ocean colour
is used only in a constrained band around the earlier traced coast, preserving the
interior snow/ice regions and the continental extension. This remains an approximate
interpretation of illustrated geography, not a geospatial survey.

The freshwater lake footprint was re-extracted from the reference and raised to
22 m, with a carved basin and graded shores. Nine spline waterways (about 16.6 km
total) follow the reference's main hydrology:

- Highland feeder into the lake, with a fork toward Wheatford.
- Wheatford stream into the western marshes.
- Mirewatch river to the southern coast.
- Lake outlet through the wooded valley to Mirewatch.
- Coastal woodland river to the same estuary.
- Western marsh inlet and three smaller marsh channels/braids.

River beds are carved below their descending water surfaces; banks blend into broad
valley sides. River surfaces use the existing Unreal river spline actor and strip
mesh, with a dedicated spline-enabled blue blockout material. The lake/ocean use
nonblocking preview sheets. These surfaces establish geography; swimming, currents,
water zones and final water shading are not enabled in this pass.

Planning marker XY coordinates are preserved; their heights were resampled against
the new terrain. The player start remains above Frosthaven. The full mountain range,
volcano, glaciers, towns, roads and vegetation remain later work.

## Source and tools

`Tools/World/WorldMapSourceV2` contains the revised 16-bit PNG and R16, land/lake/water
masks and `refinement.json` (all river points, widths, elevations and source hashes).
`refine_world_map_source.py` uses Python, numpy, scipy and Pillow; supply dependency
path through `RPG_TERRAIN_PYTHON_LIB` and illustrated PNG through
`RPG_WORLD_MAP_REFERENCE`. It does not use Blender.

`build_world_map_refined.py` reads the v1 level into a private unattended editor,
imports the revised Landscape using the existing editor helper, and saves to the new
level name. It refuses to replace an existing refined level. Its synchronous native
import/check temporarily supplies revised R16 bytes at the helper's fixed input path,
then restores the original bytes in a `finally` block. Original map/source hashes
are checked, and a pre-refinement map backup is in `Saved/WorldMapRefined`.

`verify_world_map_refined.py` reopens the result, checks size, the saved spline meshes
and channel collision probes, then optionally captures an overhead view. Source-to-
Landscape comparison before saving covered every one of the 10,167,193 samples with
zero mismatches. Reports: `Saved/WorldMapRefined`; logs: `Saved/Logs/WorldMapRefined*`.

Final fresh-load checks passed: 9 rivers, 224 spline-mesh pieces with no growth after
an explicit rebuild, and 81 channel collision probes (maximum error 0.13 cm; minimum
sampled water depth 3.37 m). No material compilation failures were logged. Reference
lettering is excluded from detached island extraction.

`RPGStylizedRiver.cpp` now preserves UserConstructionScript creation flags after
AddInstanceComponent, which otherwise changes the flags to Instance and caused mesh
duplication on subsequent construction runs. The editor target built successfully;
the new map passed a fresh load and repeated river rebuilds with this fix. A suffixed
editor module was built while the user's editor remained open. Restart Unreal once
to pick up this code before editing/testing the refined map in that existing session.

For manual terrain reimport, explicitly choose the PNG in `WorldMapSourceV2`;
the original `WorldMapSource` remains the unchanged v1 source.

No commits or pushes were made.
