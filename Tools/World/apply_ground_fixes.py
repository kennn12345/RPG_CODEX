"""All test-square ground fixes (2026-11-09) in one go: terrain/far-land collision at full detail, stray-water hollows
filled, river ends tucked under, Pond_4 enlarged, scatter cleaned up, rock corner gaps closed.

  RPG_GROUND_SAVE=1  -> save the results (CLOSE THE EDITOR FIRST: an open editor keeps the terrain and level locked)
  otherwise          -> apply in memory only, then RPG_GROUND_VERIFY=1 measures / photographs the result and quits.

Run (from a folder path without spaces, see Tools/World/run_hidden_editor.ps1):
  .\\Tools\\World\\run_hidden_editor.ps1 -Script "C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/apply_ground_fixes.py" -Log GroundApply"""
import os, sys, runpy, time, unreal
ROOT = 'C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/'
sys.path.append(ROOT)
import importlib
import edit_vista_ground as evg; importlib.reload(evg)
SAVE = os.environ.get('RPG_GROUND_SAVE') == '1'
VERIFY = os.environ.get('RPG_GROUND_VERIFY') == '1'

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
world = unreal.EditorLevelLibrary.get_editor_world()
if not world or 'L_WorldVista' not in world.get_path_name(): les.load_level('/Game/RPG/Maps/L_WorldVista')

runpy.run_path(ROOT + 'fix_terrain_collision.py')['fix_terrain_collision'](save=SAVE)
plan = evg.edit(save=SAVE)
rocks = os.path.exists(ROOT + 'fix_rock_corners.py') and os.environ.get('RPG_SKIP_ROCKS') != '1'
if rocks: runpy.run_path(ROOT + 'fix_rock_corners.py')['fix_rock_corners'](save=SAVE)
unreal.log(f'GROUND_APPLY_DONE save={SAVE} rocks={rocks}')

if not VERIFY:
    unreal.SystemLibrary.quit_editor()
else:
    start = time.time()
    def tick(_dt):
        if time.time() - start < 45: return                      # background Nanite / collision rebuild
        unreal.unregister_slate_post_tick_callback(state['h'])
        try:
            os.environ['RPG_NO_QUIT'] = '1'
            os.environ['RPG_WATER_TAG'] = os.environ.get('RPG_GROUND_TAG', 'after'); os.environ['RPG_GAP_TAG'] = os.environ['RPG_WATER_TAG']
            runpy.run_path(ROOT + 'analyze_vista_water.py')
            runpy.run_path(ROOT + 'measure_terrain_collision.py')
            os.environ['RPG_SHOT_TAG'] = os.environ['RPG_WATER_TAG']; os.environ['RPG_SHOT_NOLOAD'] = '1'
            runpy.run_path(ROOT + 'shots_problem_spots.py')
        except Exception:
            import traceback; unreal.log_error('HIDDEN_RUN_FAILED ' + traceback.format_exc()); unreal.SystemLibrary.quit_editor()
    state = {'h': unreal.register_slate_post_tick_callback(tick)}
