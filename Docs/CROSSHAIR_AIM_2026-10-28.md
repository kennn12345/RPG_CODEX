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
- Camera, final (user review): one reticle at the screen centre, two framings. With the shoulder camera in melee, the
  reticle's line ran 60 cm right of the body, so punches only met enemies on the player's right and every fight
  happened from one side. Now exploring and melee use `ThirdPersonViewOffset` (20 right, 60 up): nearly straight
  behind, so punches land straight ahead, enemies can be fought from either side, and an empty-air punch goes straight
  out. While a spell is held / cast, and for `SpellCameraHold` (1 s) after, the camera eases a further
  `ThirdPersonCombatOffset` (40 right) over the shoulder, so the body doesn't cover distant targets. While it slides
  (and 0.4 s after, as the lagging arm catches up), `HoldReticleSpot` turns the view each frame from where the camera
  actually is, keeping the reticle on the spot it was on (a look input re-picks the spot). Without that, a tap cast
  drifted 6-23 cm (one miss). The torso now also follows the aim at strike speed from the wind-up, because the
  0.16 s load was too short for the calm follow.
- (Intermediate, superseded: shoulder framing all the time, 60 right / 45 up.)

## Verification

New `RPGCrosshairAimTest.cpp`: `RPG.Combat.CrosshairAim(FirstPerson)` and `Visual.CrosshairAim(FirstPerson)`. Real
gameplay camera; for each case a dummy stands at a distance / bearing, the reticle is held on a bone, and a punch or
spell is thrown. It reports how far the fist passes from the point under the reticle, or how far the orb's line of
flight is from it. Cases: punches at chin / chest / stomach, ahead, 25 degrees either side, 70 degrees (the body
turns) and just out of reach (105 cm); spells from 1.5 m to 15 m. Pass: lands within 12 cm (spells) / 15 cm (punches: low shots can stop on the dummy's guard arm).

| | before | after |
|---|---|---|
| punches, third person | 18-37 cm, 6 of 8 missed | 1.2-9.7 cm, all hit |
| punches, first person | 10-31 cm, 5 of 8 missed | 3.0-9.8 cm, all hit |
| spells, both views | 0-5 cm | 0 cm |
| punches at empty air (distance from the reticle's line) | ~60 cm to the left in third person | 0.2-2 cm, both views |

Empty-air cases: right / left ahead, looking 12 degrees down, looking 30 degrees right (`air-punches.png`).

Final camera (`camera-modes.png`; spells now aimed, pressed, then left alone while the camera slides): punches at
targets 1.2-8.9 cm third person / 1.4-9.2 cm first person, all hit, with enemies on either side; empty-air punches
0.2-2 cm; spells 0.1-1.2 cm third person / 1.2-2.4 cm first person. Repeated runs vary ~5 cm: low punches can stop
on the dummy's raised guard arm (struck bone clavicle), so punches pass at 15 cm.

The largest punch errors are the out-of-reach case (the fist stops short) and first-person hands 4-7 cm to the side.
`RPG.Combat.FilmedRightPunch(FirstPerson)`, `RPG.Combat.StationaryStanceLocomotion(FirstPerson)`,
`RPG.Spells.Energy` and `RPG.Combat.FirstPersonSteadiness` pass. `RPG.Combat.TestDummy` fails before it starts (it
needs a dummy placed in the level; L_DummyField has none). That failure has nothing to do with this change.
Screenshots: `Saved/AimReview/` (`aim-before-after.png`, `Baseline/`).
