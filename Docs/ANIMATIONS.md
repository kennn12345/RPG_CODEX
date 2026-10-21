# RPG_CODEX — Combat animations

Last updated: 2026-10-21. **Not in use:** combat was switched back to the procedural RPG_Proto_* clips (the mocap cut looked wrong in play: combo fragments, a spinning kick instead of a teep, contorted wrists). Kept for reference.

## Current set: motion capture (RamsterZ Free Anims Volume 1)

The unarmed attacks and fighting stance are motion capture from **RamsterZ Free Anims Volume 1** (free on Fab, in
`Content/RamsterZ_FreeAnims_Volume1`, UE4 mannequin), retargeted to the UE5 Manny and cut into single attacks.

| Clip (`/Game/RPG/Animations/Mocap/`) | Source | Notes |
|---|---|---|
| `RPG_Mocap_Punch_L` (LT tap) | PunchCombo01 left jab | impact 0.14 s |
| `RPG_Mocap_Punch_R` (RT tap) | PunchCombo01 right cross | impact 0.23 s |
| `RPG_Mocap_Punch_L_Charged` (LT hold) | PunchCombo02 left hook | breathing hold 0.80–1.70 s, impact 1.94 s |
| `RPG_Mocap_Punch_R_Charged` (RT hold) | PunchCombo03 overhand right (fastest strike in the pack) | hold 0.80–1.70 s, impact 1.84 s |
| `RPG_Mocap_Kick_R` / `_Charged` (Y) | Kick02 right mid kick | chamber hold at 0.20 s, impact 0.34 s (charged 1.24 s) |
| `RPG_Mocap_Kick_L` / `_Charged` | **mirrored** right kick | identical timing |
| `RPG_Mocap_Stance` | H2H_Idle | looping fighting stance |

**Attacks step in (root motion).** The fighter's captured footwork moves the character: punches ~0.5–0.6 m, the left
hook ~0.9 m, kicks ~1.1 m. Punches thrown while moving play on the upper body with root motion off.

The guard (RT+LT) is still the procedural `RPG_Proto_UnarmedGuard`. The older procedural clips remain in
`/Game/RPG/Animations/Prototype/` (with their first-person versions).

## Pipeline (`Tools/AnimImport/`)

All headless with the editor closed; Unreal paths must use forward slashes (`\r` in a path breaks).

1. `retarget_ramsterz.py` — builds IK rigs + `RTG_UE4ToManny` and retargets the H2H clips to
   `/Game/RPG/Animations/Retarget/RamsterZ/*_Manny`; writes `ramsterz_analysis.json`.
2. `strikes.py` (plain Python) — finds each strike's wind-up / contact / recovery from the analysis.
3. `dump_poses.py` — exports every frame of the source clips to `source_poses.json` (git-ignored size: ~23 MB).
4. `compose.py` (plain Python) — cuts the strikes, blends from / to the stance, builds charged versions (rear back,
   seamless breathing loop, strike), keeps the travel as root motion, mirrors the right kick, bakes the `ik_*` bones
   onto the hands / feet (the AnimBlueprint's foot IK otherwise drags a kick back to the ground). Writes
   `mocap_clips.json` and `mocap_timings.json` (copy these into `URPGPrototypeAnimationComponent`'s defaults).
5. `mocap_import.py` — writes the clips as AnimSequences and checks them through the engine.

`UnrealEditor-Cmd.exe RPG_CODEX.uproject -run=pythonscript -script="C:/UNREAL PROJECTS/RPG_CODEX/Tools/AnimImport/<script>.py"`

## Known issues (RPG.Combat.TestDummy: 38 pass / 18 fail after the switch)

- **First person** plays the third-person clips (no first-person versions yet): hits land, but the camera-movement
  and hands-in-view checks fail. Needs first-person versions of the mocap clips.
- **Charged left hook** is held short by the strike-contact system in some cases (lands with `rpg.StrikeContact 0`).
- **Charged right (overhand) aimed at the head** misses: it comes in from above and doesn't follow upward aim well.
- **Off-axis hits**: the mocap cross sweeps ~50 cm across the body, so it can clip a target 30° off to the side
  that the test expects to miss.

## Flight poses (2026-10-21, WORK IN PROGRESS)

`RPG_Proto_FlyHover` / `FlyForward` / `FlyFast` (Tools/ProtoAnim/poses.py, section 7): 2 s floating loops. Relaxed,
loose arms in hover and normal flight (user's choice), streamlined fast flight. `URPGFlightAnimationComponent` loops
them while flying, chosen by horizontal speed (forward >= 150 cm/s, fast >= 900 or L3 fast flight), 0.35 s blends;
attacks / charges / blocks take over while they run.

**The user wants the flight animations changed later** (they approved wiring them in, not their final look).
Still to do: tilting the body into climbs / dives, upper-body-only attacks in the air, take-off / landing
transitions, the old FlightStart (FlyingUp jump) montage on flight start is now overridden next frame.
Preview tools: `Visual.ClipShots` (any clip on the dummy), `Visual.AttackShots`.
