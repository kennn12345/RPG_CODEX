# Filmed right punch — IMG_8634

Implemented October 28, 2026, at the user's request, then revised the same day (pull-back load, lead foot further out,
closed fists, smoother in-betweens). First melee clip after the melee reset; the left hand, kicks and guard still only
enter combat and hold the stance.

## Result and controls

- Third person: `/Game/RPG/Animations/CapturedPunches/RPG_Punch_R_Capture8634`; first person:
  `RPG_FP_Punch_R_Capture8634` (paired in `BuildPresentationMap`). 71 frames at 60 Hz, 1.167 s.
- Right trigger: the press plays an authored pull-back (torso turns the right shoulder back 12 degrees, rear fist draws
  back ~7 cm and drops, weight settles back/down) to the load pose at 0.20 s and waits there while held. Release throws
  the recorded punch; full extension (hit-window centre) at 0.42 s; ~0.1 s held out, recorded retraction, settle into
  the stance. A charged (Heavy/Power) release throws the same punch; there are no charged clips yet.
- Starts and ends exactly in `RPG_FightStance_Capture8635`. Root identity, feet planted, bone lengths fixed. While
  moving, the existing upper-body layer plays it over locomotion.

## Revision: bigger load and follow-through (user request)

- Pull-back: torso 20 degrees (spine_01-03) + pelvis 7 degrees, rear fist ~11 cm back, weight 3 cm back / 2 cm down.
- Follow-through (`DRIVE_*`): torso 14 degrees + pelvis 6 degrees through the target, right clavicle 8 degrees forward,
  chest lean 4 degrees, hips 2.5 cm forward. Rises from 40% of the strike, peaks 0.05 s after impact, holds while the
  arm is out, unwinds over 0.35 s. Measured: chest yaw ~-45 to -52 degrees at impact (was ~-30), shoulder +22 cm, fist
  reach 43.5 cm third person / 35.9 cm first person in PIE.
- The head counter-turns the accumulated yaw (and lean), so the eyes and first-person camera stay on the target.
- Smoothness fixes: the strike reaches for 97% of the rig's arm length (`REACH_LIMIT`, no shoulder-solver snap); the
  right elbow is steered from the stance elbow to out-to-the-side with the strike (`elbow_pole`); smoothing averages
  fist positions and re-solves the arms (averaging joint rotations wobbled the fist). First person: the left guard keeps
  last frame's elbow direction and eases home over the last 0.3 s, the strike hand's camera-guard blend spans 0.2 s, and
  the left fist steps 4 cm out during the drive. Max joint step now ~11 deg/frame in both views (was up to 59).
- First-person stance: its lead fist is capped at 93% of arm length (`cap_reach` in capture_stance8635.py); the old
  camera-guard layout locked that arm straight (7 degrees bend, now 43), which spun its twist.
- Stance hand-off (`UpdateCombatStance`): after an attack that is blending out, the stance fades in over the attack's
  own blend-out instead of the longer locomotion blend. The old timing let the idle show through for ~0.1 s, dropping
  the arms after every punch. Verified in PIE frames (`end-frames.png`).

## Left punch (mirrored)

The video has only right punches. `capture_punch8634.py` now builds both hands (`use_side`): the left punch samples the
recorded right punch mirrored frame by frame (`flight_symmetry.mirror_pose`) and fits it onto the same, unmirrored
stance (lead left foot stays forward). Every authored turn and sideways offset flips with the striking side: pull-back
turns the left shoulder back, follow-through drives it through, elbow steering, guard hand, first-person guard
clearance and clavicle drive swap sides. Measured: chest yaw -25 at load / +45 at impact (right punch +25 / -45), same
5.5 cm hip drive and 146 cm impact height. Assets: `RPG_Punch_L_Capture8634`, `RPG_FP_Punch_L_Capture8634`.

First person: an arm in the camera guard is capped at 93% reach (`cap_reach(..., limit)`), relaxing to the punch's
97% as the striking hand leaves the guard, and the fit starts from the first-person stance's clavicles, so the clip
joins `RPG_FP_FightStance` within 0.33 cm. Worst joint step ~13 deg/frame in all four clips. The right punch's
third-person clip is unchanged by the refactor (zero difference); its first-person clip got smoother (14.5 -> 11.2).

`CapturedPunchClip` returns either hand's clip. `RPGFilmedPunchTest` covers right/left tap and held/Heavy release in
both views (left fist travel 44.0 cm third person, 20.6 cm first person, where the lead guard already rests far out);
all pass with the stance tests (`Saved/Mocap8634/Gameplay-H.log`, `review-both.png`).

## Overhand (user request) and left aim

Both punches now throw like a ball: the pull-back draws the fist back and up toward the ear (`LOAD_FIST`) with the
elbow lifted up and out (`ELBOW_LOADED`, 16-18 cm above the shoulder vs 14 cm below in the stance); the strike arcs
over the top (`OVERHAND_ARC`, fist 153 cm mid-strike, down to 142 cm at impact) while the elbow rolls to out-to-the-side
(`ELBOW_STRUCK`). Elbow directions blend by slerp (a straight-line blend flipped the elbow in ~4 frames) and stay
mostly out to the side (an "up" hint lines up with the arm at the top of the arc). The torso pitches down 12 degrees
over spine_01-03 and bends 6 degrees toward the lead side (`DRIVE_LEAN`, `DRIVE_SIDE_BEND`; measured 8 degrees chest
pitch at impact), hips sink and drive (`DRIVE_HIPS` 3 cm forward, 3 cm down; 4.5 cm measured); the neck takes back
60% so the eyes stay on target, and the guard fist travels with the head (it otherwise met the dropping head).
`LEFT_AIM`: the left fist lands 1.98 cm further left at impact (measured against a build without it). Worst joint
step ~15 deg/frame, as smooth bells (cocking, then the throw). PIE: all tests pass; reach 49.1 / 49.6 cm third person,
42.5 / 27.4 cm first person (`Gameplay-I.log`, `review-both.png`).

## Code

`RPGPrototypeAnimationComponent.cpp`: `CapturedPunchClip` returns the clip for the right hand, tier 0, on
SK_UEFN_Mannequin; the reset's no-strike stub lets an attack through when its hand has a clip; a charged release with
no charged clip continues the tap punch; `FilmedPunchLoadTime` / `FilmedPunchImpactTime` (0.20 / 0.42 s) must match
`LOAD` / `IMPACT` in the builder. The folder is already in `DirectoriesToAlwaysCook`.

## Capture and fit

Original: `C:/Users/cedar/Downloads/IMG_8634.mov` (60.9 s, HEVC, stored sideways), preserved unchanged.
SHA256: `0217E18D3ADDDB30DAED3442C01E5CF01EA53B0BCC8EBE13EFD10ED8CFC65DD6`.

1. Whole clip at 30 Hz, rotated upright (1,824 frames) into `.../ok-x20/work/Mocap8634`; MetaHuman Markerless solve:
   1,824/1,824 valid body frames, ~16.6 min.
2. `Tools/Mocap/retarget_capture8634.py` (30 Hz, session-only `"-EnablePlugins=MetaHumanBodyTracker,MetaHuman"`);
   target data `Tools/ProtoAnim/captures/IMG8634_uefn_retarget.json`. Work assets in `Saved/Mocap8634/RetargetWorkAssets`.
3. A reach/elbow scan found 20 right punches (five per facing). Used: 51.9 s, right side to the camera (arm never
   occluded), least guard-hand drift. Source window 51.33 (stance) / 51.80 (full extension) / 52.27 (retract starts;
   the video holds ~0.5 s) / 52.77 (guard).
4. `RPG_SKEL=uefn python capture_punch8634.py` builds and checks both clips. Recorded rotations are applied as changes
   from the recorded stance onto the filmed stance. Toward extension the fist follows the recorded position relative
   to the shoulder; its height above the shoulder is halved because the solve's fist climbs 22 cm while the video
   shows ~9 cm (chin to mouth level). Hips never rise above the near-straight stance and sink 1.5 cm into the drive.
   Gaussian temporal smoothing (radius 3, sigma 1.5 frames) then re-planted feet; fist jolt peak 1.23 cm/frame^2.
   The elbow swings from down-and-out to out-to-the-side as the arm extends (smooth, no flip).
5. Import with `ue_import.py`, `RPG_IMPORT_FOLDER=/Game/RPG/Animations/CapturedPunches`.

## Verification

Evidence in `Saved/Mocap8634/`. Build succeeded (`Build-D.log`). `Gameplay-D.log`: `RPG.Combat.FilmedRightPunch`,
`RPG.Combat.FilmedRightPunchFirstPerson`, `Visual.FilmedRightPunch` (new, `Source/RPG_CODEX/RPGFilmedPunchTest.cpp`)
and both stationary-stance tests pass. Per view: tap, held/Heavy release and left hand. One strike window per punch,
opening 0.30 s after a tap release and 0.17 s after a held release; fist reach 39 cm (third person) / 31 cm (first);
the left hand releases nothing. `review.png` compares video and game phases; `impact-fist.png` shows the closed fist.
Not checked: contact against an NPC target (no contact test exists since the reset).
