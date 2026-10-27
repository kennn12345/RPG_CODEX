# Captured fighting stance — IMG_8619

Implemented October 27, 2026, at the user's request. The user turned to show the stance from all directions and explicitly did not want those turns played during a fight.

## Result

- New body loop: `/Game/RPG/Animations/CapturedStances/RPG_CombatStance_Capture8619`.
- Matched first-person loop: `/Game/RPG/Animations/CapturedStances/RPG_FP_CombatStance_Capture8619`.
- Each is 265 frames at 60 Hz, 4.4 seconds. Root is identity and pelvis transform is fixed in every frame. Both feet/toes stay planted. First and last poses match exactly.
- Uses the recorded steady guard, smoothing short solve jitter and closing the loop with a 0.6-second seam blend. Body-only tracking does not solve finger detail: existing rig-correct closed fists are used. First person keeps the same body/legs and fits the recorded hand variation to the established camera layout at 30% amplitude.
- The steady front view supplies the moving torso/arm performance. Four held directions provide chest-aligned footprint measurements; a robust median sets ankle spacing to approximately 29.1 cm sideways and 10.4 cm stagger. Reference turns, travel and foot repositioning are excluded from playback. This is sequential monocular footage, not a simultaneous calibrated multi-camera reconstruction.
- Combat movement and aim still own actor facing. The existing full-body stance at rest, upper-body stance while moving, combat timeout, montage blends and player steering are preserved.

## Integration and required support

`RPGPrototypeAnimationComponent::LoadFor` resolves existing serialized stance references to the new clips for `SK_UEFN_Mannequin`, without rewriting Blueprints. Other skeletons retain their prior assets. `BuildPresentationMap` now resolves both sides for the active skeleton so the fitted first-person clips are actually mapped.

The first live-input trial exposed a pre-existing mismatch: BP_RPGPlayer uses the UEFN mesh/skeleton, but punch/guard assets existed only for Manny. Their failed load prevented valid stance handoffs. `capture_stance_support8619.py` fits the existing left/right punch, charged punch and unarmed guard curves to UEFN (five body plus five first-person clips). These are new assets in CapturedStances; original Manny assets are intact. Attack timing/strike windows are derived from the existing authoring definitions. Kicks and flight are outside this addition.

`Config/DefaultGame.ini` includes CapturedStances in always-cook directories. `ue_import.py` accepts optional `RPG_IMPORT_FOLDER`; existing default folder behavior is unchanged. The import report preserves its earlier entries, now 44 total.

## Capture and rebake

Original: `C:/Users/cedar/Downloads/IMG_8619.mov`, preserved unchanged.
SHA256: `1419B466096178B1FF35E3908DA1778532238D9D21EC6522598B1E0AE4AA9111`.

The upright sequence covers 5.5–32.0 seconds at 15 Hz (398 frames). MetaHuman Animator Markerless Motion Capture 1.0.0 in UE 5.8.4 exported valid body/raw data for all 398 frames, with no pipeline error. The isolated project and solve are at `C:/Users/cedar/Documents/Codex/2026-10-26/ok-x20/work/Mocap8619`. The plugin's processed-frame counter reports zero, as with prior takes; exported data is present for every frame.

1. `Tools/Mocap/retarget_capture8619.py`: set `RPG_MOCAP_BODY_JSON` to exported body rows, enable MetaHumanBodyTracker and MetaHuman only for the retarget session, and retarget/bake to UEFN at 15 Hz. Checked-in target input is `Tools/ProtoAnim/captures/IMG8619_uefn_retarget.json`.
2. `RPG_SKEL=uefn python Tools/ProtoAnim/capture_stance8619.py` produces the two 60 Hz loops, manifest and contact sheets. Uses stable source interval 6.5–11.5 seconds, with four-view footprint checks.
3. `RPG_SKEL=uefn python Tools/ProtoAnim/capture_stance_support8619.py` produces the ten support clips in a separate manifest.
4. Import both manifests (or their merged dictionary) through `ue_import.py`, with `RPG_SKEL=uefn`, `RPG_IMPORT_FOLDER=/Game/RPG/Animations/CapturedStances`, `RPG_CLIP_FILE=<manifest>` and no prefix filter. Merge partial import reports with the saved full report.

Temporary plugin-dependent retarget assets were moved from Content into `Saved/Mocap8619/RetargetWorkAssets`. Shipped assets depend on the existing gameplay skeleton; no capture plugin is needed in gameplay. Main .uproject plugin settings were not changed.

## Validation and limits

Evidence is under `Saved/Mocap8619/`.

- `Build-H.log`: final C++ editor build succeeded. Build-A caught a shadowed Camera variable in the new test, corrected before later successful builds.
- `Retarget.log`, `Import-B.log`: successful capture retarget and all 12 gameplay assets imported. Existing deprecated Unreal Python API warnings remain.
- `Structure-B.log`: both final stance loops pass finite normalized transforms, fixed root/pelvis/facing, planted feet/toes, hinge checks, non-frozen hand movement and exact loop seam. Full anatomy checks report `ok` for both loops.
- `Gameplay-D.log`: CapturedStance and CapturedStanceFirstPerson PASS. Through actual combat input, a guard tap enters the recorded stance, a real right punch plays on the fitted rig, and recovery returns to the recorded stance. No capsule travel in either perspective. Maximum actor yaw variation: 0.3563 degrees in third person, zero in first person; the baked animation has no root/pelvis yaw change.
- `Gameplay-B.log`: FirstPersonSteadiness PASS after support integration, including still, slow-look and small-flick phases. Standing camera shake RMS 0.006 cm/frame-squared; left/right fist shake 0.013/0.009 cm/frame-squared. Look controls still affect aim normally.
- Earlier CapturedStance trials failed while the existing guard/punch assets lacked UEFN versions. A subsequent heading assertion caught ordinary ~1.7-degree square-up to the game's offset strike line. The final test aims along that strike line to isolate capture yaw; its 0.5-degree assertion remains unchanged. Production aim was not modified.
- The attempted existing StrikeContact suite in `Gameplay-A.log` could not start because L_DummyField contains RPG NPCs, not its required legacy ARPGCombatTestDummy. This is a fixture failure, not a passing melee-contact regression result. The new gameplay tests verify actual fitted punch playback/handoff, not damage/contact accuracy. A full package/cook has not been run.

Previous get-ups, flight, Power settings, maps and original animation assets are preserved. The pre-existing untracked `Docs/GETUP_HANDOFF.md` remains untouched; the recurring continuation remains paused.

## Final rendered verification

`Visual-B.log`: Visual.CapturedStance PASS on the actual player, using real guard/punch input and normal production stance selection. Framing includes the full body and planted feet. Captured gameplay timestamps were resampled to 30 Hz for the exported preview, preserving playback timing despite uneven screenshot intervals.

frames=199
max_actor_yaw=0.3563
max_travel_cm=0.0000
captured_stance=1


Preview: `FightStance-IMG8619-in-game.mp4`, 247 verified frames, 8.233 seconds, [1116, 728]. Full-body guard, real right punch, and return to the recorded stance were visually checked.
## Hand-height adjustment — October 27, 2026

At the user's request, lowered both stance fists by 5 cm in third person and approximately 5 cm in first person. `HAND_LOWER_CM=5.0` in `capture_stance8619.py` makes the adjustment reproducible. Arm IK follows the lowered targets; pelvis, feet, facing and loop timing are preserved. Guard/block and attack assets were not changed.

`LowerHands-Structure.log`: both bakes pass the existing structure and anatomy checks. Comparing every frame against the prior bake gives exactly 5 cm lower third-person hands, and 4.987–5 cm lower first-person hands. `LowerHands-Import.log` confirms both updated assets imported. The full import report still has 44 entries.

`LowerHands-Gameplay.log`: CapturedStance, CapturedStanceFirstPerson, FirstPersonSteadiness and Visual.CapturedStance all PASS. Third-person maximum actor yaw variation 0.3493 degrees with zero travel; first person zero yaw/travel. Rendered stance, planted feet and punch return were visually checked. Updated preview: `FightStance-IMG8619-lowered-hands.mp4`.
## Forward clearance and width — October 27, 2026

At the user's request, moved both stance hands 5 cm forward and increased total hand spacing by 5 cm (2.5 cm outward per hand). The prior 5 cm lowering is retained. Parameters are `HAND_FORWARD_CM=5.0`, `HAND_SPREAD_CM=5.0` and `HAND_LOWER_CM=5.0` in `capture_stance8619.py`.

Every third-person frame evaluates to exactly +5 cm forward, +/-2.5 cm outward and unchanged height relative to the lowered stance. The first-person left arm initially hit its reach limit; `fit_view_shoulder` now protracts the clavicle only as far as needed, using a continuous reach solve without stretching bones. Final first-person offsets are approximately +5.0–5.15 cm forward and 2.5 cm outward per hand, with height preserved within 0.01 cm. Torso, feet, facing and timing remain intact.

`ForwardSpread-Structure-C.log`: both loops pass structure, hinge, planted-foot, seam and full anatomy checks. `ForwardSpread-Import-B.log`: both final assets imported; original report entries remain preserved (44 total). `ForwardSpread-Gameplay.log`: CapturedStance, CapturedStanceFirstPerson, FirstPersonSteadiness and Visual.CapturedStance all PASS. Actual player travel remains zero; third-person yaw variation is the same 0.3493 degrees from normal aim behavior. Rendered stance and punch return were visually checked.

Updated preview: `FightStance-IMG8619-forward-wider.mp4`, 247 verified frames at 30 Hz (8.233 seconds), 1118x730, exported from gameplay timestamps.

## Wider, square stance and retracted elbows — October 27, 2026

The user requested wider, squared foot placement and elbows a few centimeters farther back, then explicitly restricted this change to the stance. Only the two captured stance animation assets are updated. Every punch animation, punch manifest, timing and runtime selection remains unchanged; their starts/recoveries have not been rebaked to the new stance.

The ankle spacing increases from 29.122 to 37.122 cm (4 cm outward per foot), with front-to-back stagger reduced from approximately 10.44 to 2 cm. Each foot now points 10 degrees outward from forward. The captured pelvis/chest already face forward, so their orientation is preserved. Feet remain planted, and root/pelvis transforms and the 4.4-second loop timing are unchanged.

Both body elbows move exactly 3 cm backward in every frame. First-person elbows move approximately 3.2–3.8 cm backward. Arm segments retain their original lengths, and the wrists follow the retracted elbows by a small amount to preserve a natural bend. The prior hand-offset parameters remain in the generator; the elbow correction is applied afterward. Typical body wrist movement is about 1 cm back, 1.5 cm outward and 2–3 cm down; first-person wrists follow back approximately 3 cm.

Reproducible parameters in `capture_stance8619.py`: `FOOT_WIDTH_EXTRA_CM=8`, `FOOT_STAGGER_CM=2`, `FOOT_TOE_OUT_DEGREES=10`, `ELBOW_BACK_CM=3`. `bring_elbow_back` keeps the upper-arm length fixed, chooses a minimally displaced wrist on the forearm's reach sphere, and reconstructs the rig's elbow hinge. Both loops pass existing structure and full anatomy checks (`Saved/Mocap8619/WideSquare/Structure-A.log`); frame-by-frame measurements are in `WideSquare/compare.py`.

`WideSquare/Import-B.log` confirms both assets saved with zero errors. The first commandlet import encountered Windows file locks from the user's open editor and saved neither asset. The saved editor session was closed normally to release the locks before the successful import; no map or unrelated assets were saved. The import report retains all 51 prior entries. Gameplay verification and the stance-only preview are recorded under the same evidence directory.

`WideSquare/Gameplay-A.log`: CapturedStance, CapturedStanceFirstPerson, FirstPersonSteadiness and Visual.CapturedStance all PASS. The live input test still plays an existing punch and returns to the updated stance. Maximum third-person actor yaw is 0.3563 degrees with zero travel; first person also has zero travel. The rendered stance was inspected with full-body framing. `FightStance-wider-square.mp4` exports only the settled stance before the test's unchanged punch: 79 verified frames at 30 Hz, 2.633 seconds, 1014x598. A matching PNG is included. The user's editor was reopened on L_DummyField after verification.
