# Approved phone get-up integrated — October 26, 2026

User approved the side-by-side raw capture review of IMG_8614.mov and explicitly requested gameplay integration. The recording is now `RPG_GetUp_FaceDown_Capture8614` on the UEFN mannequin, bringing the recovery table to **30 clips / four styles**. Original 29 clips remain unchanged. This is an additional pose-matched prone recovery, not a forced replacement for every landing.

## Assets and runtime

- Gameplay asset: `/Game/RPG/Animations/SK_UEFN_Mannequin/RPG_GetUp_FaceDown_Capture8614`.
- `Source/RPG_CODEX/RPGBodyImpactComponent.cpp`: new Stand entry with `CapturedPush` style. Production selection/history, DefaultSlot montage, kinematic alignment fade and 0.14-second playback ramp are preserved. The new clip is selected when its sampled start fits the landing.
- Matching now also measures the torso's anatomical facing vector using the actual skeleton reference frames. Facing error contributes squared angular cost at the existing TurnCost1.5, and compatible starts require both torso and limb turns <=110degrees. This distinguishes a front/back roll even if limb positions look similar. Fade time0.5–1second uses the larger torso/limb turn. With no compatible start, the existing fallback remains; GetUpMisses identifies a torso mismatch when applicable. This does not guarantee anatomical interpolation from every impossible incoming ragdoll.
- Clip:467 frames,60Hz,7.7667seconds; runtime1.15x uses existing NPC rate. Root stays identity; capsule placement owns displacement. Recorded pelvis/body travel remains inside the animation. The first0.6seconds of the phone hold are trimmed; the remaining recorded turn is retained and its final heading aligned with GASP idle. Two closing steps over1.2seconds enter the idle stance instead of sliding the feet. Final pose exactly matches `M_Neutral_Stand_Idle_Loop` frame0.
- No player input, Power interfaces/tuning, flight, maps, physics assets or original animation assets were changed. Main `.uproject` does not enable MetaHuman plugins; the shipped animation references the existing gameplay skeleton and needs no capture plugin at runtime.

## Capture and reproducibility

Original recording SHA256: `CE5A531301BF4ABB93174678B231F40BA4A259BA9E7A75D180F69B456A33B639`. Original video is preserved outside the repository. Capture used the official MetaHuman Animator Markerless Motion Capture plugin1.0.0, installed into UE5.8.4 after repairing stale Epic launcher engine-manifest records. The initial body-only solve produced216 distinct body-pose frames at30Hz; full-editor processing is required (the Python commandlet reports processing disabled).

`Tools/Mocap/retarget_capture8614.py` documents/runs the engine IK workflow: temporary MetaHuman342-bone source, auto-generated source/target IK rigs and FBIK, source auto-alignment, mapped chains, target UEFN91-bone bake. Run with session-only `-EnablePlugins=MetaHumanBodyTracker,MetaHuman` and `RPG_MOCAP_BODY_JSON` pointing at exported solve rows. It creates new assets in `Mocap8614Work`; move them out of Content after extraction so gameplay/cooking has no source-plugin dependency. This task's temporary retarget assets are preserved under `Saved/Mocap8614/RetargetWorkAssets`, not in Content.

- Checked-in target input: `Tools/ProtoAnim/captures/IMG8614_uefn_retarget.json`.
- Finish/bake: `RPG_SKEL=uefn python Tools/ProtoAnim/capture_getups.py` -> `capture_getups_uefn.json`. Original procedural manifests are untouched.
- The finish pass preserves the recorded joint trajectory while expressing elbows/knees about the target rig's hinge frames. It raises/widens initial bracing arms enough to clear the floor, and guides the occluded left hand outside the raised thigh during3.25–5.05seconds with smooth entry/exit. Source hand/thigh intersections were up to9.7cm before this correction. IK markers follow actual hands/feet. Offline IK softening is temporarily reduced to1e-7 for exact contact solving and restored afterward; shared generator settings are not changed.
- Import with `RPG_SKEL=uefn`, `RPG_CLIP_FILE=<absolute capture_getups_uefn.json>`, `RPG_IMPORT_ONLY=RPG_GetUp_FaceDown_Capture8614` and existing `ue_import.py`. Old import-report entries were merged back;31 total entries remain.

## Evidence and limits

Evidence is under `Saved/Mocap8614/`:

- `Retarget-A.log`, `Import-A.log`: successful engine retarget/bake and final gameplay import. Existing/deprecated Python API warnings remain; no import error.
- `Structure-Final.log`: PASS all467 baked frames: finite normalized transforms, fixed root, target hinge limits, shoulder limits, skin floor clearance (minimum0.003cm), non-frozen rise and exact idle seam. Checker: `Tools/ProtoAnim/check_capture_getup.py`.
- `Anatomy-D.log`: no reported body intersections, backwards hinges or shoulder violations. A loaded left-ankle dorsiflexion37degrees remains2degrees beyond the conservative35-degree comfort threshold. The threshold was not weakened.
- `Smooth-Final.log`: no flips. Reports some recorded foot/knee jolts and stop-and-go, including contact/closing-step phases; it is not a completely clean smoothness report. Final closing-step knee-pole convergence removed an earlier large final-frame knee pop.
- `Build-D.log`: editor C++ build succeeded with Unreal closed (UE5.8.4). Earlier builds A–C also succeeded.
- `Final30.log`: Collision, GetUpCapture, GetUpRepeat, GetUpVariety, ReactivePhysics and ReactiveStandalone all PASS. New capture test checks real selection, torso-facing distinction, fixed root, target skeleton and evaluated idle seam; variety test checks30 clips/four styles and all14 original start poses with both limb and torso safety.
- `Capture30_B.log`: actual NPC selected and completed the captured recovery. First Capture30 trial failed because seeding physics immediately after KnockDown preceded PhysicsControl's deferred movement-type change. The test now seeds after0.25seconds of actual Limp; assertions remain intact.
- `FinalVisual60.log`: rendered repeated-fall integration PASS at60Hz, screenshots30Hz. Actual NPC chose Capture8614/CapturedPush first, then FaceDownArmsSide/RightKnee, FaceDownArmsSide_LeftLead/LeftKnee and FaceDownSprawl/RightKnee. Normal production selector chooses the clip; test-only `RPG_GETUP_CAPTURE_REVIEW=1` seeds a matching prone landing on actual physics bodies for the first recovery. No gameplay override or saved map changes.
- `Arms-FinalVisual60.log`: **not wholly clean**: seven early-fade samples of the incoming captured landing exceed the15-degree forearm sideways diagnostic; zero violations once animation owns the pose. Earlier30Hz repeated-fall evidence also flags some incoming ragdoll/old-clip fades. Fully authored capture has no backwards arm bends. Loose/extreme physical ragdoll configurations and their interpolation remain a limitation; no physics asset, diagnostic bound or Power test was altered to hide it.

Preview: `C:/Users/cedar/Documents/Codex/2026-10-26/please-write-a-full-handoff-summary/outputs/Getup-IMG8614-in-game.mp4` — actual NPC recovery,231 verified frames,7.7seconds,1116x680. The preview begins just before the alignment fade, after test landing setup.

Existing Strength5 Smoke/Power classification flake remains documented and was not retuned; Smoke was not part of this scoped run. Slope contact IK, get-up into a run and active balance remain outside this change. User's approval covered the raw capture; review of the finished gameplay version is pending. The recurring continuation remains paused; implementing this requested clip did not reactivate it. Preserve pre-existing untracked `Docs/GETUP_HANDOFF.md`.
