# RPG_CODEX — Power System (implementation notes)

Last updated: 2026-10-21. The design intent lives in the Power System spec; this file records what is actually built.

## Status

| Stage | What | Status |
|---|---|---|
| 1 | GAS stats (`URPGAttributeSet`, `URPGCharacterStatsComponent`) on player and dummy | Done, tested in game |
| 2 | Calculator (`URPGPowerSystem`), settings page, automated tests | Done, unit-tested |
| 3 | Apply results to the dummy: health damage, reactions, launches, debug readout | Done, tested headless (flat-floor launch lands at 103% of calculated). Needs an in-game look |
| 4 | Calibrate the ladder in play | Not started |

## Stats (GAS attributes)

Strength uses the 1–20 ladder directly. Mass in kg (standard bandit 82 kg / 180 lb). Durability, Stability,
Stagger Resistance, Recovery: 0–100, 50 = average human. Attack Speed: multiplier, never derived from Strength.

- Player: Strength 5 (set in `FRPGBaseStats` defaults).
- Combat test dummy = the standard test bandit: Strength 3, 82 kg, Durability 40, Stability 50, Health 100.

Console (while playing, ` key): `rpg.Stats`, `rpg.SetStat Player Strength 12`, `rpg.ResetStats`.

## How a hit is calculated

`URPGPowerSystem::Calculate(FRPGPowerInput) -> FRPGPowerResult`. Pure: it changes nothing, so it doubles as a
preview (same `RandomSeed` = same result).

1. **Effective Power** (ladder levels) = Strength × tier (Normal 1, Heavy 1.15–1.35 by charge, Power 1.6)
   × source (kick 1.15) + momentum (0.15 levels per m/s closing speed).
2. **Target Resistance** = (Mass / 82)^1 × (1 + (Stability − 50) × 0.01). The standard bandit = 1.
3. **Relative Power** = Effective Power / Resistance — "what the standard bandit would feel".
4. Independent channels:
   - **Damage** = 1 × EffectivePower^1.6 × (50 / Durability)^0.5 × 1.5 on head/neck. Not affected by mass.
   - **Structural** 0–1: ramps from relative-to-durability level 10 to 16. Calculated, not applied (no injury system).
   - **Stagger** chance ramps from level 3 to 5 (Stagger Resistance shifts it).
   - **Knockdown** chance 10% at level 5 rising to 100% at 11.
   - **Displacement**: the ladder distance at Relative Power, × a random 0.6–1.0. ≥ 5 ft = **launch**: a 20° arc
     whose flight (from standing height, 80 cm drop) plus the ragdoll's slide after landing (1100 cm/s²) totals that
     distance. Shorter: a flat shove that slides to a stop (500 cm/s² on its feet, 1100 as a ragdoll).
5. **Reaction** = strongest channel that fired: Launch > Knockdown > Stagger > Flinch.

A normal punch on the standard bandit reproduces the ladder exactly. Heavier/steadier targets read the ladder at a
lower level (a 164 kg target hit at Strength 20 moves like the bandit hit at Strength 10).

## On the combat test dummy (stage 3)

`ARPGCombatTestDummy::ReceivePowerHit`: every hit builds the input from the hit + both sides' GAS stats, then
- **Damage** lowers Health (GAS). At 0 Health it is knocked out (goes down whatever the reaction) and revives with
  full Health when it gets up.
- **Flinch / Stagger** → partial physics (struck limb yields; Stagger harder) and a slide along the ground.
- **Knockdown** → ragdoll that collapses where it stands (or slides, with displacement).
- **Launch** → ragdoll with every body thrown at the calculated velocity.
- After a ragdoll it waits until the body has settled (max 1.5 s extra), then **gets up where the body lies**. If
  the body fell off the world it reappears at its spot.
- On-screen readout of every channel per hit (`rpg.PowerDebug 0` hides it; always in the log).
- `bUsePowerSystem` (on by default) off = the older fixed prototype response; the older physics tests use that.

## Where to tune

**Edit → Project Settings → Game → RPG Power System.** Every number above, plus the ladder itself. Saved to
`Config/DefaultGame.ini`, no recompiling. All values are provisional until calibrated (stage 4).

## Tests

`RPG.Power.Ladder`, `RPG.Power.Channels`, `RPG.Power.DummyLaunch` (builds a flat 200 m floor above the level and
checks a Strength 12 launch end to end) (Tools → Session Frontend → Automation, or headless:
`UnrealEditor-Cmd RPG_CODEX.uproject -ExecCmds="Automation RunTests RPG.Power" -TestExit="Automation Test Queue Empty" -nullrhi -unattended`).

## Not built yet (by design)

Blocking, weapons/armor, injuries/dismemberment, environmental collision damage, get-up animations, magic and
projectile inputs, recovery timing.

## Known issues

- The test level's dummy stands on a raised platform with slopes and drops: real launches there travel further
  (falling and sliding downhill) or fall off the world. Distances only match the ladder on flat ground — calibrate on
  flat ground.
- `RPG.Combat.DummyWander` fails before and after the Power System (pre-existing: "worst facing vs travel").
- `RPG.Combat.PhysicsReaction` "Left kick" case fails intermittently (pre-existing; passed in the stage 3 run).

- `RPG.Combat.TestDummyFirstPerson` "Shadow setup" can fail on 1–3 of ~7,200 frames: the first-person shadow body
  switches to the charged kick clip one frame late. Pre-existing animation timing (passed in the stage 1 run,
  fails intermittently since); unrelated to the Power System. Low priority.
