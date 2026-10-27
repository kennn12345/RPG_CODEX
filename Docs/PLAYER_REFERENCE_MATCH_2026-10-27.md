# Player punch reference match — active checkpoint

Updated 2026-10-27, after default activation and final integration checks. Continue from this checkpoint; do not restart source comparison or fitting.

## Authorization and scope

Work in C:/UNREAL PROJECTS/RPG_CODEX, branch gasp-integration. Match the player in C:/Users/cedar/Downloads/ScreenRecording_10-27-2026 05-26-11_1.mov as closely as possible. The user's 97% target is a visual review goal, not an objectively certified score. It has NOT been declared met. Only left/right punches, loading, extension, follow-through, recovery, and necessary stance/body positioning are in scope. Exclude defensive weaves (including those between punches), the defensive beginning of the clip, and NPC reactions.

Preserve partial-stick walking/running, stationary stance transitions, RT-right/LT-left single-release input, three variants per hand/tier, charge tiers, and the two approved mocap get-ups. Preserve pre-existing untracked Docs/GETUP_HANDOFF.md unchanged. Local checkpoints only; no push. One agent/editor operation at a time. No user editor should be closed or killed.

Goal and heartbeat match-rpg-player-punches-to-video-reference remain active. Check ordinary usage before continuing; if unavailable, preserve the exact next action and stay quiet until available. No purchases, credits, resets, or model changes. Other unrelated automations remain paused. Notify only meaningful milestones, completion, failure or required user action.

## Current game state

The reference family is now enabled by default: rpg.ReferencePunches has constructor default 1, verified in Gameplay-Default.log. Setting it to 0 restores the previous captured family for comparison. Existing captured assets and both get-ups were preserved. Config/DefaultGame.ini includes ReferenceCombat1027 for cooking; a full package cook has not been performed.

39 assets under /Game/RPG/Animations/ReferenceCombat1027:
- Third- and first-person stationary guard.
- Three variants for each hand at Light, Heavy and Power, in both perspectives (36 punches).
- One paired third-person review sequence, never used as a runtime automatic combo.

Latest animation generation/import is T: Build-Review-T.log, Import-Review-T.log, import-report-T.json. Latest C++ build is Build-U.log, successful. Reproducible authoring is Tools/ProtoAnim/build_reference1027.py; manifest reference1027_uefn.json. Generated Source/RPG_CODEX/ReferencePunchCalibration.inl records actual tier/hand impact points. Do not replace these with older draft assets or previews.

Standard strikes use a neutral root, closed fists, planted footprint about 32cm wide/48cm stagger, anatomical fixed-length IK and about 20cm in-place body drive. Left keys: guard0, load.12/.20, extension.36/.43, recovery.82, guard1.05s. Right keys: guard0, load.12/.24, extension.36/.44, withdrawal.54/.72, guard1.05s. The right strike's entry removes the preceding filmed left punch. Variants use small phase/pose changes with common loading and impact boundaries. No defensive-weave clip or behavior was introduced.

Heavy/Power now share the new guard, .12s charge switch, .6–1.2s common hold, and 1.5s impact. Durations are 2.2/2.4s. Additional planted body drive is 8/16cm, forward lean 6/12 degrees, and fist extension 4/8cm. Fixed-length shoulder/arm IK retains standard delivery height. All variants pass strictly increasing Light < Heavy < Power body excursion and reach checks. These charged motions are style adaptations: this short reference does not show their complete family.

First-person guard/coil presentation keeps fists readable with partial head following. Through impact and early withdrawal the fist/knuckle paths match third person. Camera guard returns from .78–1.05s; earlier return caused late recontact. First-person charged coils are presentation adaptations, not filmed first-person capture.

## Integration changes

RPGPrototypeAnimationComponent resolves the reference family for all tiers/variants and both views when enabled. Input/release/charge rules remain intact. Walking still uses original analog IA_Move magnitude despite GASP normalization: partial stick200cm/s, full run500cm/s, hysteresis .57/.73. Stance fades .3s, settles .12s after stopping and remains stationary only; moving strikes retain locomotion legs.

Player aim/contact adjustments are scoped to reference punches. First-person charged aim uses anatomical spine rotation rather than pitching the entire chest about the eye pivot; the latter produced artificial forward lunging at close range. Only this first-person charged aim measurement uses generated impact calibration. Target acquisition retains the original nominal strike point. Third-person and standard aim retain existing behavior.

Reference fist contact retains compression/radii/fold/penetration limits. Added a 70ms pressure dwell only while reference contact remains latched to prevent small target rebound from causing repeated arm reversals. Legacy attacks retain zero dwell. Released reference Heavy/Power use larger capped swept player backoff (18/26cm per strike, 3/4.5cm per frame, rates .8/1). The release counter keys that budget so opening the hit window cannot reset it again. Held charge retains ordinary caps. NPC reaction, damage and PowerForceRules code were not changed.

## Evidence and validation

All logs below are under Saved/ReferenceCombat1027, ignored local evidence.

- Validation-T.log: all39 authored assets pass geometry/anatomy/self-contact, bone lengths, normalized transforms, neutral roots, planted contact, guard/hold seams, closed fingers, variant checks, first-/third-person strike hand and knuckle agreement, and Light/Heavy/Power grading. review-validation.json records metrics.
- Build-U.log: successful editor build with default activation.
- Contact-V.log: all24 normal/close, left/right, Light/Heavy/Power cases pass in both perspectives, with existing penetration and release continuity thresholds unchanged. Earlier contact logs contain superseded failures; use V as current evidence.
- Gameplay-Default.log: CapturedPunches and CapturedPunchesFirstPerson each pass all18 real input releases, plus CapturedStance and CapturedStanceFirstPerson pass. Actual constructor default1 verified. Both stance root tests report0deg actor yaw/0cm travel. Stance-only root tests disable aim to isolate recorded stance rotation; other integration tests keep aim enabled.
- Locomotion-Default.log: StationaryStanceLocomotion passes; walk200/run500cm/s, largest transition hand step6.67cm, stationary-only stance and three standard releases.
- Gameplay-Visual-Default.log: both Visual.CapturedPunches and Visual.CapturedPunchesFirstPerson pass, all six hand/tier cases. Captures.csv timestamps and screenshots are in GameplayTP_Default and GameplayFP_Default. No level was saved. All task editors exited after tests.

The updated review preview is exported with Tools/ProtoAnim/export_reference1027_preview.py using RPG_VIDEO_DEPS pointing to the existing video-deps directory and RPG_REFERENCE_REVIEW_OUT pointing to this chat's outputs. Export uses recorded screenshot timestamps and checks decoded dimensions/frame count. The preview is for motion review, not proof of97% matching.

## Source comparison already completed

Reference SHA256 afe8a37a0a2ade6407019e85d01635f65be77fbd9a133fe36fe35e903fa4eb41. Duration2.1433s,129frames,816x1242, variable timestamps about60Hz. Extracted frames/metadata/sheets are in this chat's work/PunchReference1027. Blue is player; red characters/UI are excluded.

Only frames72–128 supply the punch sequence: left loading72–76, extension80–84, right loading88–92, extension96–100, recovery104–128. Earlier defensive frames are excluded. Hidden joints/depth are uncertain.

Compared native RamsterZ H2H_Idle and PunchCombo01/02/03 and direct UE4-to-UEFN retargets. Combo03 is the 3D prior because its initial jab/cross peaks are about .27s apart; it is not identified as the exact source. Retarget workflow Tools/AnimImport/retarget_reference1027.py and staging /Game/RPG/Animations/Retarget/Reference1027Work are already complete. Native and UEFN source audits each have228 saved frames; SourceAuditNative, SourceAuditUEFN and SourceAuditUEFN_Yaw60 are valid. Earlier SourceAudit/V2 had dropped/cropped frames and must not be used as acceptance evidence.

reference1027_landmarks.json records12 manually observed frames with8–20px uncertainty; occluded observations omitted. fit_reference1027.py uses constrained source-prior fitting. Fit-D camera yaw55.397deg, pitch17.816deg, distance fixed3.5m, focal2021px in the original crop. Pre-export manual landmark residuals5.6–20.3px are fitting diagnostics, not independent/current accuracy. Export grounding, isolated-strike and contact adaptations changed poses afterwards. Different UE4/UEFN proportions and uncertain camera registration remain relevant.

## Exact next action

The coherent reference family is enabled, built, imported and integration-tested. Save this reviewable local checkpoint without pushing, leaving GETUP_HANDOFF.md untracked. Then continue the actual reference visual comparison: render current standard guard, left load/extension, right load/extension and recovery at calibrated rear-right camera (~55.4deg yaw/17.8deg pitch), normalize proportions/crop, and compare only visible punch frames72–128. Use head-directed player aim toward a frozen test-only reference target if needed; an empty neutral-pitch gameplay preview does not reproduce the filmed target height. Do not change NPC reaction behavior or save test fixtures into the game level.

Review timing, torso/hip sequencing, silhouette, elbow arc, offhand, fist orientation, reach, knee/foot pivots and recovery. Record concrete discrepancies and uncertain/occluded details. Iterate player animation only, preserving the passing controls, contact, locomotion and charge grading. Re-run checks justified by actual changes. Do not claim a current97% score from the old fit residuals, technical test pass rate, unseen charged motion or first-person adaptations. Keep the goal/heartbeat active until the visible reference target is honestly met or the user stops work.

Final default visual review: Default-family-review.jpg was inspected across six cases in both views. Third-person loaded/extended silhouettes show progressively stronger charged body drive. First-person strikes are visible, but the right Heavy pre-release sample leaves most of the hands near/below the frame; inspect its full loading/hold sequence before considering presentation final. The first case also begins during stance entry. Treat these as review observations, not a completed visual match. Updated outputs/Player-punch-preview-1027.mp4 contains313 decoded1280x408 frames over13.9s, showing both perspectives for all tiers. No task editor remains running.

## Continuation: fixed-camera projection review (2026-10-27 17:00 UTC)

Local enabled checkpoint f1be251 is saved. Added Tools/ProtoAnim/review_reference1027_projection.py and ran it successfully against the actual current T manifest. It projects six current poses through the fixed Fit-D camera, aligns only pelvis image translation, masks the reference player blue, and overlays the current joint locations. It performs no pose fitting or scale normalization. Outputs are the chat outputs/Player-punch-projection-review-1027.jpg and ignored Saved/ReferenceCombat1027/current-projection-review.json. The sheet was inspected. These are current exported-pose diagnostics, not engine screenshots or independently certified match measurements.

| Review category | Current finding | Required next review |
| --- | --- | --- |
| Handed strike family | Loading and jab/cross silhouettes track the visible source broadly. | Preserve single-hand input; check timing in motion. |
| Upper body | Projected hand discrepancies about15–53px; right extension upper-arm marker about76px, head about27–48px across these phases. | Examine signed shoulder/head offsets and retarget proportions with current engine renders before altering poses. |
| Footwork | Front knee/foot discrepancies about97–166px in several phases. | Resolve ankle-versus-toe annotation, camera depth, rig proportions and intentionally shortened planted footprint. Do not reinstate the old unreachable63cm stance merely to reduce a 2D residual. |
| Pelvis/body drive | Pelvis is aligned independently per phase for this diagnostic, so it does not validate world travel or timing. | Compare the isolated runtime drive and stationary root separately from filmed player advance. |
| Charge/first-person | These remain unseen style adaptations. FP right Heavy hold diagnostic confirms the hands leave the frame mid-hold and reappear on release/recovery. | Inspect actual held montage, camera and applied aim through time before changing arm poses; paths map to the new FP family. Saved FP-heavy-hold-diagnostic.jpg records frames61/68/75/82/89/96/103/110. |
| Technical behavior | All previously recorded controls, contact and locomotion checks still apply; no runtime/animation changes in this diagnostic continuation. | Re-run only checks affected by future actual changes. |

The visible deviations and uncertain foot registration mean the97% target is not met. Exact next work is now to resolve the front-foot/knee observation convention and matched-camera engine view, then refine shoulder/head placement if the discrepancy persists after rig/camera registration. Also isolate the first-person Heavy hold aim/camera behavior; do not blindly raise every punch or change the successful contact limits. No editor was launched in this continuation, no game assets changed, and GETUP_HANDOFF.md remains untouched. Ordinary usage was still allowed at the last check (98% of the five-hour window consumed); check again before more substantial work.
