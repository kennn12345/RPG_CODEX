"""Apply the terrain collision fix in memory (no save); measure ~40 s later, after the background Nanite rebuild,
-> Saved/GroundFix/terrain_gap_after.json."""
import os, runpy, time, unreal
os.environ['RPG_GAP_TAG'] = 'after'
ROOT = 'C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/'
runpy.run_path(ROOT + 'fix_terrain_collision.py')['fix_terrain_collision'](save=False)
start = time.time()
def tick(dt):
    global handle
    if time.time() - start < 40: return
    unreal.unregister_slate_post_tick_callback(handle)
    runpy.run_path(ROOT + 'measure_terrain_collision.py')
handle = unreal.register_slate_post_tick_callback(tick)
