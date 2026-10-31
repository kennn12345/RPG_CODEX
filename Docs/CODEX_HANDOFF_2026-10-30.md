# Codex handoff (2026-10-30)

Branch `gasp-integration`, UE 5.8, project `C:\UNREAL PROJECTS\RPG_CODEX`. Claude ran out of weekly usage; Codex
finishes. Nothing pushed. Leave the user's own uncommitted files alone (`Config/DefaultEditor.ini`,
`Content/RPG/Animations/FightStance/RPG_FightStance.uasset`, `Blender/`, `Docs/GETUP_HANDOFF.md`,
`Tools/Blender/build_fight_stance.py`, `clear_fightstance_edits.py`, `render_views.py`). Unreal must be closed for C++
builds (the user closes it). Commit only true, verified fixes. Ragdoll is whole-body or none.

## Done today (all committed, full regression 23/23 green)

| commit | what |
|---|---|
| 7c7f64b | in-game freeze (deadlock: Physical Animation held the Chaos scene lock waiting on the NPC's parallel anim, whose PoseSearch traces wanted the lock). `URPGReactivePhysicsComponent` ticks Physical Animation itself after finishing the anim outside the lock |
| be60888 | "Failed to find bone data": `URPGAnimInstance::bEvaluateOnGameThread` (`CanRunParallelWork` override) while physics drives the body (`KeepPoseFresh`) |
| d5c60a5 | third-person body shots re-aimed (`URPGCombatAimComponent` Contact* values); CrosshairAim checks where punches connect |
| eb09ac8, 1dc1e82 | player down: mesh placed absolutely (location/rotation/scale) so the capsule's follow doesn't move the simulated mesh |
| b1a33e2 | braced falls human-strength (`BraceMotorStrength` 12, moving reach clips, landing give); local reactions' pushes no longer dropped (PhysicsControl on during local reactions + 3-frame linger); wall slam uses actual displacement; `DownedPushScale` 0.5 |
| 2edb538 | `RPG.Power.Calibration` / `RPG.Power.DummyLaunch` run on the Standard Bandit NPC (`RPGPowerArena::FindReferenceNpc`) |

Docs: `Docs/HIT_OUTCOMES.md` (freeze fix, bone warnings, braced falls sections), `Docs/CROSSHAIR_AIM_2026-10-28.md`.

## Open task 1 (user asked to investigate): knockdowns travel far beyond the Power System's distance

`RPG.Power.Calibration` prints (search the log for `RPGCALIB`) Knockdown at Strength 8-10: calculated 2-5 ft, actual
15-18 ft (actor position after it gets up vs. where it was hit). Only launches are checked, so it passes.

Likely mechanism (read, not yet proven) in `ARPGNpcCharacter::React` (`Source/RPG_CODEX/RPGNpcCharacter.cpp`, ~l.246-300):
- A knockdown below `KnockdownCertainLevel` with `bCatch` is `bFallsCatching`: it first tries to catch itself.
- `Shove(Push, Result.Reaction == Knockback || bCatching)` treats it as a knockback: speed clamped to at least
  `KnockbackMinShove` (400 cm/s) for `KnockbackShoveTime` (1.5 s), Push = `DisplacementVelocity * ShoveScale` (2.5).
- After `CatchFailDelay` (0.35 s) the `CatchFailTimer` calls Fall / ragdoll with the body's speed, and the braced
  ragdoll slides on; then the get-up. Stumble + slide + get-up = 4-5 m.

PROVEN (Claude, 4 calibration runs; the RPGCALIB line now reports "down at X ft, still at Y ft" = pelvis distance when
the ragdoll starts / last limp frame):

| Strength 8 | calculated | pushed | down at | still at | final |
|---|---|---|---|---|---|
| Catch (no fall) | 1.8 ft | 588 cm/s | - | - | 14.0 ft |
| Knockdown (catch fails) | 1.9 ft | 600 cm/s | 5.6 ft | 15.4 ft | 16.8 ft |
| Knockdown (catch fails) | 1.7 ft | 421 cm/s | 1.5 ft | 4.5 ft | 6.4 ft |

Cause: `Shove(Push, Result.Reaction == ERPGPowerReaction::Knockback || bCatching)` gives every Catch and every
falls-catching Knockdown the knockback push: clamped to `KnockbackMinShove`..`KnockbackMaxShove` (400-600 cm/s) for
`KnockbackShoveTime` 1.5 s, on top of Push = `DisplacementVelocity * ShoveScale` (2.5). With `ShoveBraking` 380 that is
2-5 m of stumbling for a hit the Power System sizes at ~2 ft; a failed catch then hands that speed to the braced ragdoll,
which slides ~3 m more.

DONE (user chose 2-4 ft): catches now push `CatchMinShove`-`CatchMaxShove` (200-300 cm/s, normal shove time) instead of
the knockback push. catch_recovers 104 cm (3.4 ft), a failed catch goes down ~2 ft from the hit (catch_fails total 134 cm,
was 268-369). RPG.Npc.HitOutcomes passes.

STILL OPEN - the ragdoll slide after going down (the second half of the distance): Strength 10 straight knockdown
"down at 0.1 ft, still at 12.3 ft" (calculated 4.8); failed catch "down at 2.1, still at 6.7 ft"; a short launch reached
175% (calculated 6.5 ft, 11.4 ft) and failed the sweep once. With the human-strength brace (b1a33e2) the body rolls and
tumbles on after landing instead of digging in. Ask the user how far a knockdown should slide; options: more ground
friction while braced (the `TumbleMaterial` friction 0.35 in `URPGPhysicsReactionComponent::Launch` natural falls, or
the GASP per-body materials), stronger arms on landing (less `BraceImpactShare` give for the arms), or scale the
knockdown's ragdoll velocity in `HandlePowerHit` so arc + slide = the calculated `Displacement` (launches were tuned to
land ~100%: check they still do). Use the RPGCALIB "down at / still at" readout to measure.

(Earlier proposal, kept for reference:) how far should a catch stumble? Proposal: catches use
their own push (`Shove(Push, false)` capped at `MaxShove`, or a Catch-specific min/max ~150-300 cm/s), so a catch is a few
steps and a failed catch falls near where it stumbled. Then re-run `RPG.Npc.HitOutcomes` (catch_recovers, catch_fails,
knee_*, combo_escalates, from_behind) and `RPG.Power.Calibration`, make contact sheets, get user approval, commit.
Consider adding a catch/knockdown distance check to `RPGPowerCalibrationTest.cpp` once the user picks the intent.

Also: the calibration sweep is not fully stable: in ~1 of 10 sweeps a short Strength 10 launch lands at ~60% of a 5-6 ft
calculation (a short low arc tumbles short). Every long launch is 89-120%. If it bothers CI, judge short launches
(< 8 ft calculated) on an absolute tolerance (e.g. within 3 ft) instead of 70-135%.

## Open task 2: user review pending

- New braced-fall look: contact sheets in `Saved/HitOutcomePreviews` (`RPG_SHOT_OUTCOMES=1`, see run commands). In
  `HitOutcomes_13_catch_fails` the NPC lands on its back with one leg up ~0.5 s. User has not approved the new look yet.
- Player "Attempting to move a fully simulated skeletal mesh" (was 180 per downed fall in flight) can't be reproduced in
  automation; ask the user to confirm it's gone after a flying crash.

## Leftovers (not urgent)

- `RPG.Combat.PhysicsReaction` and `RPG.Combat.TestDummy` still need the retired `ARPGCombatTestDummy` placed in the
  level; port like 2edb538 or retire (ask the user).
- Benign log noise, no fix needed: CrowdManager "Unable to find RecastNavMesh" (dynamic navmesh builds after world
  init), PoseSearch "databases AsyncBuildIndex in progress" on first play, GASP sample console-variable perf warnings,
  editor layout/HTTP shutdown.

## How to build and test

Build (Unreal closed):
`"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" RPG_CODEXEditor Win64 Development "-Project=C:\UNREAL PROJECTS\RPG_CODEX\RPG_CODEX.uproject" -WaitMutex`

Full regression (windowed, ~10 min):
`UnrealEditor.exe "C:\UNREAL PROJECTS\RPG_CODEX\RPG_CODEX.uproject" /Game/RPG/Maps/L_DummyField -ExecCmds="Automation RunTests RPG.Npc.Collision+RPG.Npc.ReactionStress+RPG.Npc.HitOutcomes+RPG.Player.HitOutcomes+RPG.Npc.Smoke+RPG.Npc.ReactivePhysics+RPG.Npc.ReactiveStandalone+RPG.Npc.GetUpRepeat+RPG.Spells.Energy+RPG.Spells.LegBalance+RPG.Combat.FilmedRightPunch+RPG.Combat.CrosshairAim+RPG.Combat.StanceToggle+RPG.Combat.StationaryStanceLocomotion+RPG.Power" -TestExit="Automation Test Queue Empty" -unattended -NoSplash "-ini:Engine:[Core.System]:HangDuration=15,[Core.System]:GameThreadHeartBeatStackWalk=True" -abslog=<log>`
Expect 23/23 Success. Also grep the log: 0 `LogPhysicsControl: Warning`, 0 `REACTIVE impulse body failed`.

Freeze guard: run `RPG.Npc.ReactionStress` windowed 3x (same flags); it froze at step 8 before 7c7f64b.

Knockdown previews: set `RPG_SHOT_OUTCOMES=1`, run `RPG.Npc.HitOutcomes+RPG.Player.HitOutcomes` with
`-NoSound -UseFixedTimeStep -FPS=60 -RenderOffscreen -ResX=1280 -ResY=720`, frames land in
`Saved/Screenshots/HitOutcomes|PlayerOutcomes` (contact sheets were built with a small PowerShell System.Drawing script).

If the editor hangs: dump thread stacks (a dbghelp stack walker was used; UE's bundled Python is at
`Engine\Binaries\ThirdParty\Python3\Win64\python.exe`) and map offsets with the DLL exports.
