# Prompt for Codex: finish the rock rollout

Paste everything below into Codex if Claude's run stopped before reporting that it finished.

---

You are picking up a rock restyle in the Unreal 5.8 project at `C:\UNREAL PROJECTS\RPG_CODEX`, on the local branch `asset-restyle`. Read `Docs/ROCK_RECIPE.md` first: it lists the approved look and the rebuild commands.

The user approved the look on `Rock_Large_A` and asked for it on all 21 library rocks in `/Game/RPG/Environment/Rocks/Meshes`. That includes the new carpet moss on every rock that already has moss (Large A and C, MediumLarge A, Medium B). The user does not need previews, and wants usage kept low.

**Rules**
- Don't commit or push. The user decides that.
- Don't change cliffs, terrain, or the water test copies in `Environment/Props`. They were not approved.
- Make assets in Unreal, not Blender. Blender's bundled Python may be used only as a numpy interpreter for the texture scripts.
- Close-range look: no cracks, and no deep round pits.
- Before any script run, check that no UnrealEditor.exe is open. If the user has one open, ask them first; don't close it.

**Work out where things stopped**
1. Run `git status --short -- Content/RPG/Environment/Rocks Content/RPG/Maps`.
2. Check `Content/RPG/Environment/Textures` for `T_Stone_R03_*`, and `Rocks/Materials` for `M_Rock_Stone_Moss.uasset`. If either is missing, run the `stone_r03` build step from the recipe. The texture PNGs must exist in `Saved/StoneTest`; if they don't, run the three texture scripts first.
3. Run the `RPG_WEATHER_ALL=1` step from the recipe. It skips rocks already tagged `RockWeathered`, so it's safe to rerun. Don't remove that tag unless the rock's original mesh was restored from git, or the rock would be weathered twice.
4. Read the `WEATHER` lines in `Saved/Logs/RPG_CODEX.log`. Each rock should show `open_loops=0`, its hull count, `nanite=True`, the right material (`MI_Rock_Stone_Moss` on the 4 mossy rocks, `M_Rock_Stone` on the rest) and a size change of only a few percent. The last line should read `WEATHER_ALL done=N failed=0`.

**Check the result**
- `Tools/World/build_rock_preview.py` temporarily adds photo cameras to `L_AssetReview`, so first copy `Content/RPG/Maps/L_AssetReview.umap` somewhere safe and restore it afterwards. It should end up unchanged.
- Photograph with `RPG_ASSET_SHOTS=AS_RockPrev` and `-ExecCmds="Automation RunTests Visual.AssetShots" -TestExit="Automation Test Queue Empty"`. Photos go to `Saved/Screenshots/Assets`.
- A passing test doesn't prove the material worked. Search the log for `Failed to compile Material`. A plain grey rock means the material failed.
- An exit code of -1073741819 during editor shutdown has been harmless here, as long as the log shows the work finished and saved first.
- Then run `Visual.WorldShots` and look at the rocks in `Saved/Screenshots/World`. Watch for a pink or purple cast at 17.3 h, which has been a problem before.

**Report to the user, plainly**
- What finished, and anything that didn't.
- Open issues from the recipe: blue blotches on bare stone on mossy rocks seen from above, the blue-purple cast on upward faces, unmeasured frame cost, and untested collision.
- One decision for them: whether cliffs should get the same look. The user wants rocks and cliffs to match later.
