# Attacks land where the crosshair points

October 28, 2026. Goal: punches and energy spells hit what the reticle (screen centre) is over, in both views, with a
third-person camera framed for ranged play.

## What was already there

- `ARPGHUD` draws the reticle at the screen centre; `URPGCombatAimComponent` traces from the camera through that
  point (melee targets within 3 m) and turns / tilts the torso so the strike goes there.
- `URPGEnergySpellComponent` traces the same ray (6000 cm), turns the body toward the hit point and fires each orb from
  the palm toward it. Measured: already accurate (0-5 cm).

## What was wrong and the fix

- Punches: the aim measured from `NaturalStrikePoint` (Z 131 cm), left over from the scrapped punches. The filmed
  punches land at ~146 cm, so aiming at a chin tilted the strike ~15 degrees up; measured misses 18-37 cm, most
  punches whiffed. `URPGPrototypeAnimationComponent::GetFilmedStrikePoint` now reads each filmed clip (third- or
  first-person presentation) at `FilmedPunchImpactTime`: the fist (the melee sweep's knuckle point) and spine_03, the
  turn's pivot. The aim uses the thrown hand's point (both hands' midpoint between punches) and the pivot-to-fist
  distance as the pitch radius. Rebuilding the clips needs no recalibration.
- Punches at empty air, third person (user report): with nothing under the reticle the punch went straight ahead of
  the body, but the shoulder camera's line runs ~60 cm to its right, so the fist landed well left of the reticle. While
  a filmed punch is thrown, the aim point is now where the view ray meets the fist's reach (sphere about spine_03; the
  ray's closest point when it passes outside), so on screen the fist lands on the reticle. The punch then angles
  toward the camera's side. Between punches the stance keeps facing the look direction (no idle twist).
- Camera: `ThirdPersonViewOffset` 25 -> 60 cm right, 35 -> 45 up; `ThirdPersonCombatOffset` 35/25 -> 0/15 (the
  combat total is the same 60 / 60). At 25 cm the head covered distant targets under the reticle, and starting a
  cast slid the view 35 cm sideways.

## Verification

New `RPGCrosshairAimTest.cpp`: `RPG.Combat.CrosshairAim(FirstPerson)` and `Visual.CrosshairAim(FirstPerson)`. Real
gameplay camera; for each case a dummy stands at a distance / bearing, the reticle is held on a bone, and a punch or
spell is thrown. It reports how far the fist passes from the point under the reticle, or how far the orb's line of
flight is from it. Cases: punches at chin / chest / stomach, ahead, 25 degrees either side, 70 degrees (the body
turns) and just out of reach (105 cm); spells from 1.5 m to 15 m. Pass: lands within 12 cm.

| | before | after |
|---|---|---|
| punches, third person | 18-37 cm, 6 of 8 missed | 1.2-9.7 cm, all hit |
| punches, first person | 10-31 cm, 5 of 8 missed | 3.0-9.8 cm, all hit |
| spells, both views | 0-5 cm | 0 cm |
| punches at empty air (distance from the reticle's line) | ~60 cm to the left in third person | 0.2-2 cm, both views |

Empty-air cases: right / left ahead, looking 12 degrees down, looking 30 degrees right (`air-punches.png`).

The largest punch errors are the out-of-reach case (the fist stops short) and first-person hands 4-7 cm to the side.
`RPG.Combat.FilmedRightPunch(FirstPerson)`, `RPG.Combat.StationaryStanceLocomotion(FirstPerson)`,
`RPG.Spells.Energy` and `RPG.Combat.FirstPersonSteadiness` pass. `RPG.Combat.TestDummy` fails before it starts (it
needs a dummy placed in the level; L_DummyField has none). That failure has nothing to do with this change.
Screenshots: `Saved/AimReview/` (`aim-before-after.png`, `Baseline/`).
