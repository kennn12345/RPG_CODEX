# Hit outcomes (2026-10-29)

Stagger, knockback, catch-yourself and knockdown for NPCs and the player, picked from the kind and size of the force.
Nothing here is committed until the user says so. Resume notes: the "Progress" section at the bottom.

## What decides the outcome

`URPGPowerSystem::Calculate` (all numbers: Project Settings > Game > RPG Power System).

- **Power**: punches / kicks from Strength x tier x source + momentum (unchanged). **Energy** spells from the charge
  (tap = level 5, full charge = 9; *Energy Spells*). **Body bumps** from closing speed (*Body Bumps*).
- **Reaction level** = relative power x situation x (1 + missing balance x 0.6) x hit zone. It picks the reaction;
  damage and push distance still use plain relative power, so the calibrated ladder is unchanged.

| Reaction level (standard bandit) | Outcome |
|---|---|
| < 3 | Flinch |
| 3-5 (chance) | Stagger |
| >= 6 | Knockback: driven back several steps, on its feet |
| 8-11 | Catch: stumbles with its arms out and recovers; fall chance 10% -> 100% (it falls *while trying*) |
| >= 11, or a 5 ft+ push | Knockdown / launch |

At Strength 5: a normal punch staggers, a charged punch knocks back, a full-power punch makes it catch itself (1 in 10
falls). Energy: tap staggers, half charge knocks back, full charge catches (40% fall).

**Bumps** (closing speed): < 3 m/s nudge, 3-6 stagger, 6-10 knockback, >= 10 m/s (flying) knockdown. Running into an
NPC no longer knocks it down (was any bump >= 4.5 m/s).

**Balance** (`URPGHitOutcomeComponent`, hidden): each hit drains 0.07 per reaction level, refills 0.6 s after the last
hit over 1.2 s. Three quick normal punches: stagger -> knockback -> knockback / catch.
**Hit zone** (from the bone): head staggers more easily (x1.25) and snaps back harder; pelvis / spine_01-03 doubles it
over (spine fold); legs x1.15 toward catch / knockdown; arms x0.85.
**Situation**: planted in the fighting stance x0.8, mid-attack x1.25, running x1.15, hit from behind x1.15.

## How each outcome looks

- Stagger / knockback: local physical give at the struck bone + a push GASP steps out (NPC); push + camera shake (player).
- Catch: knockback push + GASP's `M_ragdoll_reach_*` pose on the upper body (spine_03 up) toward the fall
  (`URPGAnimInstance::Brace`, `ApplyBrace`). A failed catch stumbles 0.35 s, then falls.
- Knockdown: a real whole-body ragdoll that fights the fall: `URPGPhysicsReactionComponent::SetRagdollBrace` turns the
  motors on while LIMP (arms 0.8, head 1.3, trunk 0.9, legs 0.2 of MotorStrength) driving toward the reach pose; once
  the body rests they ease to 30% before the get-up. Knocked out (or <= 5% health): fully limp, no motors.
- Walls: a push (>= 2.5 m/s) blocked by a wall slams: torso give off the wall, a hand brace toward it, ragdoll foley.
  Ledges: pushed off an edge -> braced ragdoll fall.
- Hit while down: the blow goes into the lying ragdoll (35% of the standing push) and restarts its time down. A
  knockback-or-more hit during a get-up puts it back down.
- Feedback: hit-stop + attacker camera shake for knockback (stagger strength) and catch (knockdown strength); the player
  gets a shake when hit; GASP `MSS_FoleySound_Ragdoll` on falls and wall slams.

## Code

`RPGPowerTypes.h` / `RPGPowerSettings.h` / `RPGPowerSystem.cpp` (Knockback, Catch, Energy, Body, ReactionLevel, HitZone),
`RPGHitOutcomeComponent.*` (new: balance, situation, brace / bend driver, braced falls, the player's whole response),
`RPGNpcCharacter.cpp` (`React`, `ReceiveBump`, `UpdatePushObstacles`), `RPGBodyImpactComponent.cpp` (graded bumps,
braced knockdowns), `RPGPhysicsReactionComponent.cpp` (brace motors), `RPGAnimInstance.cpp` (`ApplyBrace`),
`RPGControllerInputComponent.cpp` / `RPGMeleeHitComponent.cpp` / `RPGEnergyOrb.cpp` (player wiring).

## Tests

`RPG.Power.Outcomes` (pure), `RPG.Npc.HitOutcomes`, `RPG.Player.HitOutcomes` on `/Game/RPG/Maps/L_DummyField`.
`RPG_SHOT_OUTCOMES=1` (rendered) saves side views to `Saved/Screenshots/HitOutcomes/` and `PlayerOutcomes/`.
`RPG.Combat.PhysicsReaction`, `RPG.Power.Calibration`, `RPG.Power.DummyLaunch` need the old `ARPGCombatTestDummy`, which
L_DummyField doesn't have: they stop at setup (same as the known `RPG.Combat.TestDummy`).

## Knee-down catches (stage 3)

Hand-keyed (the user chose keyed poses over footage), built into Unreal assets: `RPG_Catch_Knee{Fwd,Back}_{Recover,Fall}_{Soft,Mid,Hard}`
in `/Game/RPG/Animations/SK_UEFN_Mannequin` (root motion). Author: `Tools/ProtoAnim/kneecatch.py`
(`RPG_SKEL=uefn python kneecatch.py` with UE's Python -> `kneecatch_uefn.json`, stick sheets in `previews/`), import:
`ue_import.py` with `RPG_CLIP_FILE=.../kneecatch_uefn.json RPG_IMPORT_ONLY=RPG_Catch_` (clips marked `root_motion` keep their root).

- Forward: lurch, stumble step with the left foot, right knee down where the right foot stood (its ball pinned, the foot
  turning about it), left hand braced on the left knee, right arm out; rise in two pushes, the back foot lifts and steps
  through, idle. Backward: recoil, the right foot lifts and steps back, kneels behind. Fall versions tip over the knee /
  sit back, then the braced ragdoll takes over.
- Soft / Mid / Hard: stumble steps 40 / 46 / 52 cm (backward: deeper step back), faster for harder.
- Layered on every clip: head and chest lag and settle, landing dip, decaying balance wobble, breathing, a 1-2 mm effort
  tremble in the bracing hand, a two-push rise.
- **Realism gate** `Tools/ProtoAnim/realism.py` (every frame, zero findings to pass): human joint ranges and capsule
  clipping (getupcheck), wrist sideways, knees over their feet, elbows never pointing forward on a hanging arm, nothing
  into the floor, planted feet never skating, no IK flips, no snaps or same-way pauses except at declared contacts.
- In game (`URPGHitOutcomeComponent::TryKneeCatch`): a catch falling forward / back (up to 60 deg off; the body turns into
  the fall over 0.25 s) becomes a knee-down one, always on a leg hit, else half the time. The hit's force across the
  catch band plus the body's own speed that way pick Soft / Mid / Hard, the play rate is tuned 0.94-1.06x within it. A
  failed catch hands the braced ragdoll the hips' tracked velocity.
- Review: `Tools/Blender/clip_review_video.py` (six-angle video + sheet for any clip; the punch pipeline otherwise).

Known: a failed catch falls gently (~0.6 m/s): the tipping clips end with the hips nearly still.

## Freeze fix (2026-10-30)

The first in-game knee-down froze Unreal: a deadlock. `UPhysicalAnimationComponent`'s tick reads the mesh's pose while
holding the Chaos scene's write lock; with the NPC's parallel animation still running it waited for that task inside the
lock, and the task (motion matching's trajectory traces, `UpdatePrediction_SimulateMontage` -> `LineTrace`) waited for
the lock. A tick prerequisite on the mesh is not enough in UE 5.8 (the mesh's tick doesn't wait for its animation), so
`URPGReactivePhysicsComponent` ticks its Physical Animation itself: it finishes the mesh's animation
(`HandleExistingParallelEvaluationTask`) outside the lock, then calls the component's tick.

Also: `URPGPhysicsReactionComponent::KeepPoseFresh` and the player's PhysicsControl in `TG_PostPhysics` (the player's
"Failed to find bone data" flood, 28,000 -> 0); PhysicsControl runs only in ragdolls and flight (`DriveWithControls`), so
the brace motors now actually drive the falls; a SLOW FRAME warning in `URPGHitOutcomeComponent`.

Test: `RPG.Npc.ReactionStress` (replays the session that froze: a flying crash, bumps, energy hits, punches into
knockdowns). Run it windowed with `-ini:Engine:[Core.System]:HangDuration=15,[Core.System]:GameThreadHeartBeatStackWalk=True`.


Bone-data warnings (2026-10-30, later): the NPC's animation ran in parallel, and the pose PhysicsControl cached was the
empty buffer the task had swapped out (readable 93 bones, editable 0): motors without targets on those frames.
`URPGAnimInstance::bEvaluateOnGameThread` (overrides `CanRunParallelWork`; both the NPC's GASP ABP and the player's
are parented to it) is set by `KeepPoseFresh` while physics drives the body. Stress test: 0 warnings (was 350-880).

## Braced falls and cleanup (2026-10-30, later)

- Braced falls looked paralyzed (most on the light bandit): once PhysicsControl actually ran, the brace motors drove
  the whole body at `MotorStrength` (40, a standing hit's stiffness) toward one still pose (the reach clip's first
  frame, GASP's standing legs). Now `BraceMotorStrength` 12 / `BraceDampingRatio` 0.7 (arms 1, head 1, trunk 0.5, legs
  0.2), the reach clips play through the fall (`FRPGBracePose::Time`), and a hard landing or a blow while down knocks
  the muscles to `BraceImpactShare` (0.2) for `BraceImpactRecover` (0.45 s). Each fall logs how much the body moves
  within itself ("braced fall: ... deg/s"). A downed hit's slide needed `DownedPushScale` 0.35 -> 0.5 (the soft body
  drags instead of sliding as a block).
- Local reactions (`URPGReactivePhysicsComponent`) were dropping their pushes ("REACTIVE impulse body failed to
  simulate"): their bodies go simulated through PhysicsControl's modifiers, and PhysicsControl was off outside ragdolls.
  It now also runs during local reactions and for 3 frames after any need, so the switch back to kinematic applies.
- Wall slams sometimes didn't happen: "the push is going nowhere" read the movement's velocity, which can still show the
  push's speed against the wall; it now uses how far the body actually moved.
- The player down in a ragdoll: the capsule follows the body for the camera, which moved the simulated mesh every frame
  (an engine warning each frame). The mesh is placed absolutely while down and returned under the capsule to get up.
