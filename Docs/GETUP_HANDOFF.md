# Get-up animations: handoff (2026-10-26)

Branch `gasp-integration`, last get-up commit `2b9b4ef`. Plan and history: `Docs/GETUP_PLAN.md`.

## What exists
- **14 get-ups**, ours, in `/Game/RPG/Animations/SK_UEFN_Mannequin/RPG_GetUp_*` (GASP's 12 are no longer used):
  FaceDown, FaceUp, SideL, SideR and start variants FaceDownArmsSide, FaceDownArmsUp, FaceDownFrog, FaceDownSprawl,
  FaceUpArmsOut, FaceUpArmsUp, FaceUpArmsV, FaceUpKneeOut, SideLCurled, SideRCurled. All end kneeling on the right
  knee, left foot forward, then rise and blend (last 0.35 s, per bone) into GASP's idle `M_Neutral_Stand_Idle_Loop`.
- **Authored in** `Tools/ProtoAnim/getups.py`: targets in UEFN component space (cm; +Y forward, +X character-left,
  +Z up, root on the floor). `pose()` builds a key; `variant()` makes a start variant (own first pose + gathering
  partial keys + the base clip from its 2nd key). Shared finish: `from_fours()`.
- **Generator channels added** (`protoanim.py`): `square` (removes the idle's hip/chest turn), `pelvis_lie` (roll onto a
  side; head_rot / foot_rot roll with it), `hand_flat_*` / `hand_dir_*` (palm flat on the floor).
- **Game side** (`RPGBodyImpactComponent.cpp`, `StandUp`): pose-matches the ragdoll against the first 0.6 s of every
  clip (bone positions + limb segment directions), rejects starts needing a limb turn > 110 deg (unless none fits),
  scores distance + turn, places the capsule from the matched frame, plays the clip as a slot montage. Clip table
  `GetUps[]` - add new clip names there. Fade time 0.5-1 s by limb turn (`NextReturnBlendTime`).
  `RPGPhysicsReactionComponent::ReturnToAnimated`: the limp body is pinned kinematic where it lies for the fade
  (don't use Static: drops out of the physics blend and the pose snaps).

## Workflow (UE's Python: `C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe`)
1. Edit keys in `getups.py`; bake: `RPG_SKEL=uefn python getups.py [name-filter]` (prints bone heights per key and
   anything below the floor; stick sheets in `previews/`).
2. Check, every time:
   - `RPG_SKEL=uefn python getupcheck.py [filter]` - joint ranges, hinges backwards/sideways, body parts through each
     other (capsules). Idle reads ok by construction.
   - `RPG_SKEL=uefn python getupsmooth.py [filter]` - stop-and-go, jolts, flips. Planted hands/feet holding still are
     correct (ignore those).
   - centre of mass vs support: `protoanim.center_of_mass(cs)` (the rise must carry it over the front foot).
3. Import (editor CLOSED): `RPG_SKEL=uefn RPG_IMPORT_ONLY=RPG_GetUp UnrealEditor-Cmd.exe RPG_CODEX.uproject
   -run=pythonscript -script=".../Tools/ProtoAnim/ue_import.py"`. A partial import overwrites
   `ue_import_report_uefn.json`: merge the old entries back.
4. Build (editor closed): `Build.bat RPG_CODEXEditor Win64 Development "-Project=<path>/RPG_CODEX.uproject" -WaitMutex`
   (quote the -Project argument; right after an editor exits, retry once - Live Coding can still hold it).
5. Film: `RPG_SHOT_CLIPS=<comma list of /Game/... paths>` + `UnrealEditor.exe ... -ExecCmds="Automation RunTests
   Visual.ClipShots" -TestExit="Automation Test Queue Empty" -unattended` -> `Saved/Screenshots/Clips/<clip>/`.
6. In game: `RPG_GETUP_TRACE=1` + `RPG.Npc.Collision` -> `Saved/GetUpTrace.csv`; `python tracesmooth.py` (pops /
   stalls; ignore the first ~0.02 s: the capsule placement frame), `python tracecarriers.py` (capsule/mesh/root moves).
   `python getupmisses.py` reads `Saved/GetUpMisses.csv`: landings no start fit, in the matching frame (ahead = hips
   to head along the ground, right, up; relative to the hips) - author new starts from them.
7. Tests: `RPG.Npc` (Collision, Smoke).

## Gotchas learned
- IK elbows/knees flip when the pole lines up with the limb, or a limb folds nearly shut while its hand passes the
  shoulder: steer the pole in partial keys, keep hands further out.
- Blending `hand_dir` between opposite directions passes through zero (flat_hand_q has no direction): go via a sideways
  direction. Flat-hand weight changing while the hand flips orientation can flip too - drop flat early.
- Elbow pole pointing up on arms stretched overhead lifts the elbow: shoulder past 180. Point elbows out along the floor.
- Euler channels interpolate per component: a body rolling should use `pelvis_lie`, not yaw/roll.
- Writing `clips_uefn.json` (80 MB) can fail with Errno 22 if something holds it briefly - just rerun.

## Known open items
- ~23% of crash landings still have no start within 110 deg (`Saved/GetUpMisses.csv`). Some are impossible ragdoll
  leg poses (foot level with the chest lying face up): the UEFN physics asset's joint limits look too loose - worth
  tightening.
- `RPG.Npc.Smoke` is flaky: the light punch (Strength 5) sometimes knocks down an 82 kg bandit (Power System tuning,
  not get-ups).
- Every get-up ends with the same right-knee kneel and rise: a mirrored finish (left knee) needs a closing step into
  the idle's left-foot-forward stance.
- Plan steps still to do: ground-contact IK while a get-up plays (slopes), get-up into a run, active ragdoll.
- The user hasn't reviewed the overnight changes in game yet - get their feedback first.
