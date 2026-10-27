# Face-up phone get-up — IMG_8611

Implemented October 26, 2026, at the user's request.

## Gameplay

- New animation: `/Game/RPG/Animations/SK_UEFN_Mannequin/RPG_GetUp_FaceUp_Capture8611`.
- Added to the production pose-matched recovery table in `RPGBodyImpactComponent.cpp` as a standing recovery, style `CapturedSitTurn`. There are now 31 clips and five styles. Original 30 assets remain intact.
- Uses the existing UEFN gameplay skeleton, root lock, DefaultSlot montage, placement/alignment, selection history and NPC playback rate (1.15x). A compatible face-up landing can select it; ordinary production pose matching decides.
- Performance: lie on back, sit up, turn through a supporting hand/knee, stand, then two finishing steps into the exact GASP idle pose. Baked at 60 Hz, 395 frames, 6.5667 seconds. Root stays identity throughout; pelvis/body travel stays in the animation.

## Source and reproduction

Original: `C:/Users/cedar/Downloads/IMG_8611.mov`, preserved unchanged.
SHA256: `A4DF79C8491D038FDE9920057B97EA131EB2A827D3E2D5D8C8A581C104E942BB`.

Video frames were rotated 90 degrees clockwise and resampled at 30 Hz from approximately 3.8 to 9.2 seconds, excluding the later walk toward the camera. The isolated capture project is at `C:/Users/cedar/Documents/Codex/2026-10-26/ok-x20/work/Mocap8611`. Its `take.json` records exact extraction times. MetaHuman Animator Markerless Motion Capture 1.0.0 in UE 5.8.4 solved all 162 body/raw frames, with no pipeline error. Its reported processed-frame counter is zero, as in the prior take; exported body data contains 162 valid frames.

- `Tools/Mocap/retarget_capture8611.py`: create staging source animation and auto-generated IK rigs/retargeter, auto-align and bake to UEFN. Set `RPG_MOCAP_BODY_JSON` to exported solved rows; enable MetaHumanBodyTracker and MetaHuman only for that editor session. Frame count follows input length.
- Extracted target data: `Tools/ProtoAnim/captures/IMG8611_uefn_retarget.json` (162 distinct frames, 91 bones).
- Bake with `RPG_SKEL=uefn python Tools/ProtoAnim/capture8611_getups.py`. Writes `capture8611_getups_uefn.json` and the contact sheet. No hand-authored reconstruction substitutes for the recorded motion. Finishing aligns heading, repairs limb hinge frames and floor contacts, follows IK markers, resamples to 60 Hz, and adds closing steps.
- Import using existing `ue_import.py`, with `RPG_SKEL=uefn`, `RPG_CLIP_FILE` pointing to that manifest and `RPG_IMPORT_ONLY=RPG_GetUp_FaceUp_Capture8611`. Existing report entries are preserved (32 total entries).
- Temporary plugin-dependent assets were moved out of Content to `Saved/Mocap8611/RetargetWorkAssets`. The shipping clip uses the existing skeleton and needs no capture plugin. Main project plugin settings are unchanged.

## Validation and limits

Evidence: `Saved/Mocap8611/`.

- `Build.log`: editor C++ build succeeded.
- `Retarget.log` and `Import.log`: successful target bake and gameplay import. Existing deprecated Unreal Python API warnings remain.
- `Structure.log`: all 395 frames pass finite/normalized transforms, fixed root, target elbow/knee hinges, shoulder limits, floor clearance and exact idle seam. Minimum skin clearance 0.478 cm. `check_capture_getup.py` now accepts optional manifest/name arguments, preserving its existing defaults.
- `Anatomy.log`: no reported body intersections or backward/sideways hinge violations. Two conservative comfort-range warnings remain: spine lateral bend reaches 39.2 degrees (4.2 over 35) during 3.13–3.82 seconds, and left ankle dorsiflexion reaches 36 degrees (1 over 35) during 3.63–3.87 seconds. Bounds were not weakened.
- `Smooth.log`: no flips; some recorded foot/knee jolts and stop-and-go are reported. This is not a wholly clean smoothness result.
- `Final30.log`: seven tests pass: Collision, GetUpCapture, GetUpCapture8611, GetUpRepeat, GetUpVariety, ReactivePhysics, ReactiveStandalone. Both recorded captures are checked for production selection, target skeleton, fixed root and evaluated idle seam; all 14 original start poses retain safe alternate styles.
- `Final30/selections.csv`: actual NPC selected and completed Capture8611 first, followed by three other recoveries. This used `RPG_GETUP_CAPTURE_REVIEW=8611` to seed a matching pose on actual physics bodies after entering Limp. Production selection and recovery run normally. Existing value `1` still reviews Capture8614. No saved map edits or gameplay forced-selection override.
- `Arms30.log`: zero arm diagnostic violations in the new capture, including its fade; 11 fade samples in an older FaceDownArmsSide_LeftLead recovery are flagged.

The rendered review shows the actual NPC lying face up, sitting/turning, rising and returning to idle. Source recording and original gameplay assets are preserved. No flight, Power System, player input, map, physics asset or shared diagnostic thresholds were changed. The pre-existing untracked `Docs/GETUP_HANDOFF.md` is preserved, and the recurring continuation remains paused.

## Rendered result

`Visual60.log`: Visual.GetUpVariety PASS at 60 Hz (screenshots at 30 Hz). Actual NPC completed Capture8611 first, then FaceUpArmsV, FaceUpArmsV_LeftLead and FaceDownSprawl. The exported preview contains 203 verified frames, 6.7667 seconds at 1116x680. It begins at the alignment fade after the seeded landing setup.

Rendered arm diagnostics: 3 flagged samples; {('RPG_GetUp_FaceUpArmsV_LeftLead', 'fade'): 3}. See `ArmsVisual60.log` for full evidence.
