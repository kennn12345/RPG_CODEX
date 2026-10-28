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
