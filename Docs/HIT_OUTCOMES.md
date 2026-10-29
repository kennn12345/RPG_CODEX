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

## Not done

Knee-down catch (stage 3): needs the user's phone footage (or hand-keyed poses in Unreal).
