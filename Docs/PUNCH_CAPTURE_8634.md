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

## Tight elbow, rotate on release (user request)

The overhand's lifted elbow in the pull-back swung the forearm upright and read as a wave. Now the elbow stays down
and tucked as in the stance through the pull-back (~10 cm below the shoulder) and the first third of the strike, and
the fist draws back to the cheek (`LOAD_FIST`); the load comes from the torso coil. The elbow and shoulder turn over
together in the release (`elbow_pole(turn)`, clavicle drive scaled by the same `turn`), reaching ~6 cm above the
shoulder at impact, and unwind over the return. `turn_weight` is timed on the clip clock (from 30% of the strike to
0.03 s after impact): driving it from the eased strike amount squeezed the ~75-degree turn into ~3 frames (30
deg/frame). Arc over the top reduced to 3 cm. Worst joint step 15 deg/frame at full punch speed, two smooth surges.
PIE tests pass (`Gameplay-J.log`; close-ups `arm-right.png`, `arm-left.png`).

## Whole-body chain, faster, palm-down to the chin (user request)

`capture_punch8634.py` was rebuilt so the body is organised around the arm ("as if these punches are real"):
- Kinetic chain (`CHAIN`, `weights()`): hips, torso and shoulder each have their own pull-back and drive curves,
  offset so power builds from the ground: measured fastest moments foot/pelvis/torso 0.217 s, clavicle 0.233 s,
  arm 0.25-0.27 s. Pull-back: pelvis 10, torso 22 degrees back, weight 4 cm back / 3 cm down, elbow and fist straight
  back ~10 cm, tight. Drive: pelvis 14 + torso 12 degrees through (chest ~58 degrees with the recorded turn; more put
  the lead shoulder past its range), hips 3 cm to the lead side, 5 cm forward, 4 cm down (knees bend), torso pitches
  10 / bends 6 degrees, shoulder rolls 10 degrees. The head counter-turns to stay on target.
- Feet (`foot_target`): the striking-side foot pivots on its ball as the hips drive (heel out up to 35 degrees,
  lifted 22 degrees, ~5 cm); the other foot stays planted. Checks: lead foot fixed, pivot ball within 0.3 cm.
- Faster: LOAD 0.16 s, IMPACT 0.29 s (0.13 s strike; fist peaks ~14 cm/frame, ~8 m/s), hold 0.05 s, retraction 2.2x;
  clip 0.77 s. C++ `FilmedPunchLoadTime` / `FilmedPunchImpactTime` updated; the test's window lower bound is 0.03 s
  (a held release now hits ~0.08 s later).
- Fist (`turn_fist`): pronates palm-down with the shoulder roll; palm normal at impact -0.98 (right) / -0.99 (left).
  Wrist at impact 146.5 cm (`CHIN_Z`: a same-height opponent's chin).
- Worst joint step ~26-28 deg/frame (forearm at full punch speed). PIE: all tests pass (`Gameplay-K.log`).

Review video: `Tools/Blender` (`export_anim_fbx.py` -> `apply_anim_fbx.py` onto `Blender/RPG_Player.blend`, which
`export_player_fbx.py` / `import_player_blend.py` create -> `render_punch_frames.py`), then the encoder in the work
folder. Output `Saved/Mocap8634/punches-review.mp4` (60 fps, side and front, normal and quarter speed).

## Follow-through and sketch-driven arm path (user review rounds)

Reviewed with six-angle preview sheets the user marked up by hand (`Tools/Blender/render_punch_frames.py ... all`,
`Tools/Blender/punch_review_video.py`). Contact is unchanged (straight ahead at the chin); everything around it moved:
- Follow-through (`FOLLOW_*`): just after contact the fist carries on ~9 cm forward and ~6 cm down while the
  striking shoulder drives forward and the torso pitches and rolls down over it. The strike arrives with speed
  (2u^2-u^3) instead of easing to a stop on the target.
- Wind-up (`wound_arm`): the elbow rears back and down past the back line (whichever is further back of the
  shoulder blade, ~8 cm behind its joint, and the mid back, ~11 cm behind spine_04, measured on the mesh) and out to
  the side, folded at 138 degrees with the fist pulled in by the shoulder; the shoulder blade retracts with it
  (`LOAD_CLAVICLE`). Measured ~7 cm past the back line on both hands (the lead side needed the back-line measure: its
  shoulder starts further forward). Earlier tries measured in the turned chest frame and folded the arm into the chest.
- Strike path (`STRIKE_KEYS`, `strike_path`): keyed to the user's third sketch: at 0.217 s the arm already drives out
  forward and up (~23 degrees), at 0.250 s it reaches up and forward (~26 degrees), then comes down onto the chin.
  Fist on a cubic Hermite through the keys; the elbow hint is the part of each keyed elbow off the shoulder-fist line,
  slerped at constant speed (using the elbow's own direction near a straight arm flipped the solver).
- Fists stay in line with the forearms (straightened over the pull-back, re-straightened after smoothing, eased back
  to the stance's wrist over the settle); the guard forearm stands upright (`GUARD_ELBOW`).
- Body chain starts earlier to keep up with the arm (`CHAIN`: torso from 10%, shoulder from 22% of the strike).
- First person: the striking arm follows the body clip (winds up low out of view, snaps in as it fires), blending to
  the camera guard only at the ends; blends take the short way round (`short_nlerp`, slerped poles).
Checks: all four clips pass; worst joint step ~38 deg/frame at the launch out of the wind-up; no end-frame jump.
PIE: all punch and stance tests pass (`Gameplay-Q.log`).

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
