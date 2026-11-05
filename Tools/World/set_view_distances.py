"""View distances in /Game/RPG/Maps/L_WorldVista (user 2026-11-05: grass, flowers, details and leaves "render too close" -
"add some distance and add a fade").
  UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/set_view_distances.py" -unattended -RenderOffscreen
- Every ARPGScatter with a cull distance: end = old x VIEW_MUL (at least +25 m), fade band from 65% of it (materials
  shrink instances into the ground over the band - build_vista_assets.distance_fade). Set on the instance component
  directly - NO Rebuild (a rebuild re-traces and would re-drop pieces onto tree crowns).
- Fab trees + ancient tree (/Game/FabPacks/Restyled): LOD screen sizes 1 / 0.2 / 0.06 (was 1 / 0.3 / 0.1) - full
  leaves further out. Prints VIEW lines."""
import unreal

MAP = '/Game/RPG/Maps/L_WorldVista'
# 2026-11-05 round 2 (user: "double the render distance" - the player flies fast over the map): end = 2x the ORIGINAL
# build_vista.py / place_vista_details.py cull distance (cm, by actor label) -> re-runnable, never compounds.
VIEW_MUL, FADE_START = 2.0, 0.65
ORIGINAL = {'Grass': 9000, 'GrassTall': 9000, 'GrassShore': 9000, 'Wheat': 9000, 'Poppies': 7000, 'Daisies': 6000,
            'Detail_PathTufts': 5000, 'Detail_PathStones': 3500, 'Detail_PathBushA': 12000, 'Detail_PathBushB': 10000,
            'Detail_Ferns': 6000, 'Detail_MossTrees': 5000, 'Detail_Mushrooms': 3000, 'Detail_Clover': 4000,
            'Detail_FallenLeaves': 3500, 'Detail_Branches': 3500, 'Detail_BushTrees': 10000, 'Detail_Logs': 15000,
            'Detail_Stumps': 15000, 'Detail_MossRocks': 5000, 'Detail_FernsRocks': 6000, 'Detail_Reeds': 7000,
            'Detail_Bellflowers': 5000, 'Detail_BlueFlowers': 5000}
LODS = [1.0, 0.15, 0.05]     # trees: half the original 0.3 / 0.1 screen sizes = full leaves ~2x as far
RS = '/Game/FabPacks/Restyled'
TREES = [f'{RS}/SM_Lux_Tree_{c}' for c in 'ABCDEFGHIJ'] + [f'{RS}/SM_Jabami_Tree_Ours', f'{RS}/SM_Tree_Ancient_Clumps']
def view_end(label, current): return ORIGINAL.get(label, current) * VIEW_MUL if label in ORIGINAL else current

SMES = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
for p in TREES:
    m = unreal.load_asset(p) if unreal.EditorAssetLibrary.does_asset_exist(p) else None
    if not m: continue
    SMES.set_lod_screen_sizes(m, LODS[:m.get_num_lods()]); unreal.EditorAssetLibrary.save_loaded_asset(m)
    unreal.log(f'VIEW tree {m.get_name()} LOD screen sizes {LODS[:m.get_num_lods()]}')

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem); eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les.load_level(MAP)
for a in eas.get_all_level_actors():
    if not isinstance(a, unreal.RPGScatter): continue
    old = a.get_editor_property('cull_distance')
    if old <= 0: continue
    end = view_end(a.get_actor_label(), old)
    if a.get_actor_label() not in ORIGINAL: unreal.log_warning(f'VIEW {a.get_actor_label()}: no original distance listed - left at {old / 100:.0f} m')
    a.set_editor_property('cull_distance', float(end))
    a.get_editor_property('instances').set_cull_distances(int(end * FADE_START), int(end))
    unreal.log(f'VIEW {a.get_actor_label()}: {old / 100:.0f} m -> fade {end * FADE_START / 100:.0f}-{end / 100:.0f} m')
les.save_current_level()
unreal.log('VIEW_DONE')
unreal.SystemLibrary.quit_editor()
