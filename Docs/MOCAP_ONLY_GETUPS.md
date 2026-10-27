# Mocap-only get-ups — October 27, 2026

User requested removal of every get-up except the two added through video mocap.

## Final state

The only get-up animation assets left under Content are:

- `/Game/RPG/Animations/SK_UEFN_Mannequin/RPG_GetUp_FaceDown_Capture8614` — IMG_8614, CapturedPush.
- `/Game/RPG/Animations/SK_UEFN_Mannequin/RPG_GetUp_FaceUp_Capture8611` — IMG_8611, CapturedSitTurn.

Both retained .uasset files are byte-for-byte unchanged (SHA256 verified before/after removal). Removed 41 other animation assets: 29 generated recoveries plus 12 stock GASP get-ups. Removed their 29 generated preview sheets. Backups are outside Content in `Saved/MocapOnly/RemovedAssets` and `RemovedAuthoring`; prior Git commits also preserve them.

## Runtime and authoring

- `RPGBodyImpactComponent.cpp`: production table contains exactly the two captures. Existing candidate start sampling, pose/torso/limb scoring, compatible-start preference, history tie-breaking, placement, alignment hold, playback ramp and idle handoff remain intact. Moving intent cannot load a removed run/crouch clip. History cannot force an opposite-facing recovery merely for variety.
- `RPGCombatTestDummy.cpp/.h`: removed the old four stock clip properties and placement offsets. The older dummy now chooses between the same two captures and evaluates pelvis/head placement from the actual selected capture's first frame.
- `RPGGetUpVarietyReview.cpp`: asserts every completed recovery is one of the captures. `RPG_GETUP_CAPTURE_REVIEW=both` seeds alternating captured landing poses on actual physics bodies, then verifies production selection and completion of both. Ordinary unseeded falls need not alternate styles when their poses match the same capture.
- Pruned retired entries from `clips_uefn.json` (14), `getup_variety_uefn.json` (15), and `ue_import_report_uefn.json` (29). Retained all unrelated animation entries.
- `ue_import.py` skips every RPG_GetUp_* except the two captures, preventing old manifests from recreating retired assets.
- Procedural `getups.py` and `getup_variety.py` CLI generation is retired; shared helper functions remain available to the capture bakers/checkers. `check_getup_variety.py` now validates the mocap-only manifests and both retained bakes.

## Verification

Evidence: `Saved/MocapOnly/`.

- `Build-A.log`: C++ editor build succeeded, including UHT after legacy property removal.
- `Removal.log`: full synchronous asset-registry scan and loaded referencer checks found no asset-package referencers to the 41 retired sequences. All were deleted through Unreal's asset API. Exactly two get-up assets remain. Main player, NPC, current dummy and legacy dummy Blueprints compile as BS_UP_TO_DATE. No Blueprint/map asset files were resaved by this check.
- `removal-report.json`: exact deleted/retained paths and retained hashes.
- `Structure.log`: both captured bakes pass finite normalized transforms, fixed root, target hinges/shoulders, floor clearance and exact GASP idle seam. Manifest policy passes.
- `Both30.log`: seven tests PASS at 30 Hz: Collision, GetUpCapture, GetUpCapture8611, GetUpRepeat, GetUpVariety, ReactivePhysics and ReactiveStandalone. NPC completed Capture8614, Capture8611, Capture8614, Capture8611 through the real selector and recovery path. Production table reports two clips/two styles; both starts and synthetic side orientations are exercised with standing/moving intent and prior-history inputs.
- `Natural60.log`: unseeded GetUpRepeat PASS at 60 Hz. Four ordinary throws completed using Capture8614, Capture8614, Capture8611, Capture8611. No retired clip was selected.
- Numeric arm diagnostics: the seeded run reports zero violations. The ordinary run flags eight incoming ragdoll-fade samples for Capture8611; zero fully authored clip-stage violations. Some ordinary landings have no start inside the existing 110-degree limb bound (the logs report up to 160 degrees for an incoming forearm). The existing slow alignment fallback remains; its thresholds and diagnostic bounds were not weakened. With only two recorded starts, coverage of unusual limb arrangements is narrower than the removed 31-clip pool. Recovery completion is verified, not a claim that every arbitrary ragdoll transition is visually flawless. No video/render capture was run for this cleanup.

The approved fighting stance, combat hand spacing, flight, Power settings, physical assets, maps and capture performance data are unchanged. Pre-existing untracked `Docs/GETUP_HANDOFF.md` is preserved. The recurring continuation remains paused.
