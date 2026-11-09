"""Photos of the ground/water problem spots (top-down + eye level). Env RPG_SHOT_TAG (default 'before').
After an in-memory edit (RPG_SHOT_NOLOAD=1) the Pond_4 shots follow the planned pond in Saved/GroundFix/edit_log.json."""
import sys, os, json, math, unreal
from pathlib import Path
sys.path.append('C:/UNREAL PROJECTS/RPG_CODEX/Tools/World')
import importlib, ground_shots as gs; importlib.reload(gs)
TAG = os.environ.get('RPG_SHOT_TAG', 'before')
cx, cy, W = -3000.0, 8600.0, 108.0                       # the original Pond_4
if os.environ.get('RPG_SHOT_NOLOAD') != '1':           # chained after in-memory edits: keep them
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level('/Game/RPG/Maps/L_WorldVista')
else:
    log = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())) / 'GroundFix' / 'edit_log.json'
    if log.exists():
        p = json.loads(log.read_text())['plan']; cx, cy, W = p['cx'], p['cy'], p['W']
def look(fx, fy, fz, tx, ty, tz):
    return (math.degrees(math.atan2(tz - fz, math.hypot(tx - fx, ty - fy))), math.degrees(math.atan2(ty - fy, tx - fx)))
SHOTS = [
    {'name': 'pond4_top', 'loc': (cx, cy, W + 3800), 'rot': (-89.9, 0), 'fov': 80},
    {'name': 'pond4_wide', 'loc': (cx, cy, W + 7000), 'rot': (-89.9, 0), 'fov': 80},
    {'name': 'pond4_oblique', 'loc': (cx + 2600, cy - 2600, W + 1800), 'rot': look(cx + 2600, cy - 2600, W + 1800, cx, cy, W), 'fov': 70},
    {'name': 'pond4_eye', 'loc': (cx + 2000, cy - 400, W + 250), 'rot': look(cx + 2000, cy - 400, W + 250, cx, cy, W), 'fov': 75},
    {'name': 'pool_hollow_top', 'loc': (-3700, 1800, -98 + 3800), 'rot': (-89.9, 0), 'fov': 75},
    {'name': 'pool_hollow_eye', 'loc': (-1400, 300, -98 + 260), 'rot': (-7, 152), 'fov': 75},
    {'name': 'pond3_top', 'loc': (3000, -4700, 126 + 2200), 'rot': (-89.9, 0), 'fov': 75},
    {'name': 'river_mouth_top', 'loc': (4770, 290, -460 + 1800), 'rot': (-89.9, 0), 'fov': 75},
    {'name': 'pond1_top', 'loc': (7400, -4600, -17 + 2200), 'rot': (-89.9, 0), 'fov': 75},
]
gs.shoot(SHOTS, TAG, then=unreal.SystemLibrary.quit_editor, exposure=-0.7)
