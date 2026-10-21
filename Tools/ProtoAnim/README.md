# Prototype combat animation generator

Builds rough, in-place combat clips for SK_Mannequin from key poses + two-bone IK.
The root bone is identity on every frame, so clips cannot tip the character over.

1. Edit key poses/timing in `poses.py`.
2. `python poses.py && python preview.py`  (use UE's bundled Python:
   `C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe`)
   -> `clips.json`, validation printout, stick-figure sheets in `previews/` (side / front / top per key).
3. Import: in the editor, Tools > Execute Python Script > `ue_import.py`
   (or headless with the editor closed: `UnrealEditor-Cmd.exe RPG_CODEX.uproject -run=pythonscript -script=<path>/ue_import.py`).
   Writes `/Game/RPG/Animations/Prototype/RPG_Proto_*` and `ue_import_report.json`.

`skeleton_dump.json` (from `dump_skeleton.py`) holds the skeleton + MM_Idle stance; `probe.json` supplies the fist finger pose.

## Timing / overlap
Each key in `poses.py` carries a `lags` dict (seconds per channel group, see `GROUPS`) so body parts
reach a key in sequence (legs/hips -> lower spine -> upper spine -> shoulder -> fist) instead of on the
same frame. `python analyze.py Punch` (or `Kick`) prints per-frame hip/chest yaw, strike-limb speed,
elbow angle, fist orientation and planted-foot slide, to check the chain and the impact stop.

## Diagnostics (run after `python poses.py`)
- `analyze.py [Punch|Kick]` - hip/chest yaw, strike speed, elbow/knee angle, fist orientation, foot slide.
- `smoothness.py [name]` - per-bone rotation speed per frame; each part should rise and fall once (no zig-zags).
- `twist.py [name]` - upper-arm twist relative to the clavicle; big spikes show as a shoulder "pop" on the mesh.
- `clipcheck.py [name]` - how close arms get to the torso/head (negative = inside; guard fists on the jaw read a few cm negative).

Pose channels worth knowing: `guard_follow_*` (guard hand rides with the head), `hand_rel_*`/`hand_off_*`
(hand target as an offset from the moving shoulder), `loop_*` (arc bump on top of a strike), and keys can be
partial (`only(...)`) or override easing per body part (5th key element).

## Frame rate
Clips are baked at 60 fps (`FPS` in protoanim.py) so fast snaps are not rounded off, and `ue_import.py` sets each
clip's sample rate to match via `URPGPrototypeAnimationComponent::SetClipSampleRate` (read-only to Python; needs the
C++ module built). Clip lengths are padded to a whole number of 30 fps frames: a length that isn't trips an
assertion in UE's animation compression and leaves an asset that crashes on load (delete it and re-import).
Both punches play `PUNCH_SPEED` (1.1x) faster than their authored key times (`faster()` in poses.py).
