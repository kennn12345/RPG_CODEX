# Rock recipe (approved on Rock_Large_A, November 2026)

The look was settled one edit at a time on Large A, then applied to the 21 library rocks in `/Game/RPG/Environment/Rocks/Meshes`.
Not covered: cliffs and terrain (still on Stone_R02), and the 5 water test copies in `Environment/Props`.

## Approved edits
1. **No streaks.** Horizontal bands filtered out of the stone texture.
2. **Finer surface detail.** A grain, chip and pit layer on a 1.2 m tile, value 1.0, relief 0.45.
3. **Colour.** Tint 0.47 / 0.57 / 0.68: slightly darker with a bit of blue, staying near light grey so rocks can match the cliffs later.
4. **Weathered shape.** Most edges rounded by varying amounts, some patches near sharp, bent edge lines, and shallow flat chips. No chips near chunk joins, because deep round pits looked unnatural. The user chose this softer version over adding chips back.
5. **Carpet moss.** Colour 68 / 98 / 22, soft low-detail pile on an 80 cm tile, relief 0.45, smooth edges, 3 cm raised + 2 cm bump. Only on the mossy rocks: Large A and C, MediumLarge A, Medium B.
6. **No cracks.** Long cracks, hairlines and plate seams removed: "they dont look good".
7. **No stray moss.** Moss only where the broad surface faces up (stored in vertex colour alpha). This cleanup was the last step and was not separately approved.

## How to rebuild
Run these in order, with the editor closed. Blender's interpreter is used for numpy only; no Blender scene is involved.

```
"C:/Program Files/Blender Foundation/Blender 5.2/5.2/python/bin/python.exe" -I Tools/World/destripe_stone_texture.py
"C:/Program Files/Blender Foundation/Blender 5.2/5.2/python/bin/python.exe" -I Tools/World/make_stone_detail.py
"C:/Program Files/Blender Foundation/Blender 5.2/5.2/python/bin/python.exe" -I Tools/World/make_moss_texture.py
RPG_VISTA_ASSETS=stone_r03 UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/build_vista_assets.py" -unattended -RenderOffscreen
RPG_WEATHER_ALL=1 UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/weather_rock.py" -unattended -RenderOffscreen
```

- Weathering changes meshes in place and tags each one `RockWeathered`, so a rerun skips it. To redo a rock, restore its original mesh from git first.
- `build_vista_assets.py rocks` re-imports the older meshes from FBX and would undo the weathering. Run the weathering again after it.
- `Rocks/Test` and the `T_StoneTest_*` textures are the leftover trial on Large A. Nothing in the game uses them.

## Known open issues
- Blue blotches on bare stone on mossy rocks, seen from above. They move between runs, so a shadow glitch from the raised moss is the best guess. Unconfirmed.
- Rocks read mauve in L_WorldVista at its default 17.3 h. On one rock in WS_01_Path, RGB went from 113/88/68 to 99/72/85, so blue now exceeds green. The tint is the likely cause; changing it is the user's call.
- Bare stone reads blue at 16:00 and purple at 17:18 on upward faces.
- MediumLarge B and Small B each report one small open loop (a hole) in the mesh; not spotted in the two-sided photos.
- Weathered rocks have far more triangles: about 2,300 to 88,000 vertices each, up from about 2,000 triangles or fewer.
- The frame cost of the extra detail layer, the raised moss and the higher triangle counts is unmeasured.
- Collision shapes are unchanged and untested against the weathered shapes.
