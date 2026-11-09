"""Apply the moss-lift fix in memory (no save), wait for shader compiles, then photograph the rocks against the sky."""
import os, sys, runpy, time, unreal
ROOT = 'C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/'
runpy.run_path(ROOT + 'fix_rock_corners.py')['fix_rock_corners'](save=False)   # material edit: wait for shaders
start = time.time()
def tick(_dt):
    if time.time() - start < 120: return
    unreal.unregister_slate_post_tick_callback(state['h'])
    try:
        os.environ['RPG_SHOT_NOLOAD'] = '1'; os.environ['RPG_SHOT_TAG'] = os.environ.get('RPG_SHOT_TAG', 'rocks_after')
        runpy.run_path(ROOT + 'shots_rock_corners.py')
    except Exception:
        import traceback; unreal.log_error('HIDDEN_RUN_FAILED ' + traceback.format_exc()); unreal.SystemLibrary.quit_editor()
state = {'h': unreal.register_slate_post_tick_callback(tick)}
