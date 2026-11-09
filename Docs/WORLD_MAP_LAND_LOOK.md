# Restrained regional ground look

Applied November 6, 2026 to `/Game/RPG/Maps/L_WorldMap_Refined` only.

Dedicated material: `/Game/RPG/Environment/WorldMap/LandLook/M_WorldMap_RegionalTerrain`.
The original blockout, approved vista assets, water materials, startup map and terrain
shapes were not changed. The prior refined level is backed up at
`Saved/WorldMapLandLook/L_WorldMap_Refined_before_land_look.umap`.

The ground now has softened regional colours for snow, cool mountain stone,
foothills, muted golden plains, central forest, dark marshes, warm woodlands,
Frostrock tundra/ice, tropical shores and charcoal volcanic land. These are region
indicators, not new mountain/volcano geometry. Most of the original broad plateaus
remain flat and still need sculpting.

A low-contrast seamless eight-metre grain tile and very restrained normal map add
surface texture. Actual vertex slope blends exposed stone into steeper banks.
Actual height gives low ground slightly darker values; mild concavity shading
follows the heightmap. There is no World Position Offset or displacement, and no
emissive lighting. Roughness is 0.95 with low specular response.

Source PNGs and palette manifest: `Tools/World/WorldMapLookSource`.
Generator: `Tools/World/make_world_map_region_textures.py` (Python, numpy, scipy,
Pillow; dependency path may be supplied via `RPG_TERRAIN_PYTHON_LIB`).
Application script: `Tools/World/apply_world_map_land_look.py` (refuses replacement
of existing look assets). Preview/reload check: `Tools/World/review_world_map_land_look.py`.
Use forward slashes in Unreal's `-ExecutePythonScript` argument.

Verified by reopening the saved map, checking its assigned material and absence of
World Position Offset, inspecting separate full-map and valley renders, and checking
the build/review logs for material compilation failures (none found). No full
gameplay or performance tests were needed for this material-only pass.

The material is intentionally a starting look, not final terrain surfacing. River
banks and coast edges retain the current blockout geometry. Nothing was committed
or pushed; the user's already-open Unreal session was left open.
