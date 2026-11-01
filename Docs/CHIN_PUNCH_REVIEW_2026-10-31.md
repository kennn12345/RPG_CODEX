# 105 cm head targeting and both standard punches

Implementation and automated checks are complete. The right and left standard punches are saved for your animation review in third and first person.

Open the [video review](C:/Users/cedar/Documents/Codex/2026-10-31/h/outputs/momentum_review/review.html). Each comparison shows the original footage on the left and the update on the right, with normal and quarter-speed versions. There are eight matched scenarios: both views, both hands, Strength 5 chest shots and Strength 12 chin shots. Four additional third-person views begin from a settled guard and use a wider camera through the NPC reaction and player recovery. The baseline footage includes its original misses and wrong-zone contacts.

## Targeting and contact

The filmed full-body pose now owns its root frame while it blends through GASP. Aim calibration also accounts for the evaluated root during blends. A bounded, non-stretching arm solve places the leading knuckles toward the reticle patch. The stationary camera keeps that world point steady while the body and animated eyes move; looking input updates the point.

Contact uses the target's actual sphere/capsule shapes. Palm and knuckle motion are measured separately before contact correction. Hand sweeps are ordered by actual arrival time across both samples, and the first valid contact consumes the hit for that NPC. This resolves the low-frame-rate case where a later palm/collarbone overlap could take an earlier knuckle/head landing. Continuous contact pressure begins after the confirmed standard-punch landing. Follow-through remains anchored to the aimed patch as the NPC responds.

Visible aim interpolation reads the montage clock and enters smoothly from the load pose. Quick releases and releases after holding no longer produce the temporary full hand correction caused by an uninitialized strike timestamp.

## Authored momentum

Both body and first-person clips now lead with the hips, then the chest/shoulder, then the fist. Forward delivery is delayed relative to the early lift, with stronger hip travel and torso drive. Retraction follows the moving torso and returns to the authored stance.

| Measurement | Before | Updated |
| --- | --- | --- |
| Pelvis / chest / fist speed-peak times | 0.208 / 0.208 / 0.192 s | 0.192 / 0.208 / 0.225 s |
| Right / left fist peak speed | 12.41 / 12.29 m/s | 15.23 / 15.07 m/s |
| Right / left speed at nominal impact | 3.71 / 3.61 m/s | 4.66 / 4.41 m/s |
| Pelvis forward range | 14.0 cm | 30.9 cm |

These measurements sample the authored clips in mesh space before runtime aim/contact, using the same fist point for both versions. The [motion graph](C:/Users/cedar/Documents/Codex/2026-10-31/h/outputs/momentum_review/metrics/punch_motion_comparison.png) and [full motion measurements](C:/Users/cedar/Documents/Codex/2026-10-31/h/outputs/momentum_review/metrics/motion_metrics.json) are saved. All four clips pass the anatomy checks, preserve arm lengths, keep the lead foot and pivot planted, retain identity root and match their stance seams.

Clip duration remains 0.767 s, with 47 frames at 60 Hz; load is 0.16 s and nominal impact is 0.29 s. Inputs, charge behavior and Power/Strength formulas are preserved. Charged animation assets remain unimplemented.

## Verification

- UE 5.8.4 Win64 Development Editor build passed.
- Full regression: **31/31 tests passed**, including both camera/hand input paths, held releases, stance movement, catch/brace/get-up recovery, walking ragdoll contact, NPC reactions, spells and Power.
- Targeting at **30, 60 and 120 Hz: 9/9 aggregate tests passed**, covering 111 fresh-PIE trials: 84 real hits and 27 out-of-reach misses. Dedicated chin cases cover right/left, third/first person and 90/100/105 cm; 140 cm checks remain genuine misses.
- An additional **24/24 close first-person left punches** passed at 30 Hz, alternating steady and tracking views through fresh NPC idle poses.
- Chin trials require actual head/neck contact within 15 cm of the skeletal chin marker, a visible fist path within 7 cm of the reported surface, incoming motion at the existing 150 cm/s threshold, one confirmation and the proper partial/whole-body response. Maximum measured marker error was **14.51 cm**; maximum traced-skin-patch offset was **9.47 cm**. This validates head/neck delivery across the tested poses and distances.
- Runtime arm length error stayed at numerical roundoff and measured elbow flexion stayed at 142 degrees. Peak initial NPC body spin in the three-rate checks was 2,329.6 deg/s, below the unchanged 6,000 deg/s check.
- Gameplay fist sweeps remain 6 cm. Physical palm/knuckle contact radii remain 4.5/3.5 cm. Hit zones and thresholds were preserved.
- Launch calibration measured 93–105% of the calculated distances, including a 117.1 ft launch. Ground-grip travel was 133 cm total and 16 cm after grip. Existing standing-shove tuning remains unchanged.

Read the [full regression results](C:/Users/cedar/Documents/Codex/2026-10-31/h/outputs/momentum_review/final_regression_results.txt), [targeting measurements](C:/Users/cedar/Documents/Codex/2026-10-31/h/outputs/chin_targeting/verification/verification_summary.json) and [capture measurements](C:/Users/cedar/Documents/Codex/2026-10-31/h/outputs/momentum_review/capture_measurements.json). The footage uses actual Enhanced Input actions and live NPC physics at a fixed 30 Hz capture rate. Automated coverage concentrates on the stationary reference Bandit; animation preference and broader moving combat remain for your gameplay review.

## Source and saved work

The generator and payload are `Tools/ProtoAnim/capture_punch8634.py` and `capture_punch8634_uefn.json`; the four standard TP/FP assets are in `Content/RPG/Animations/CapturedPunches`. Runtime changes are in the aim, animation, camera and melee/contact components. `RPG.Npc.ChinTargeting`, `ChinReticleHold`, `ChinCloseRepeat` and `Visual.PunchMomentumReview` provide repeatable checks and captures.

Pre-existing editor configuration, fight-stance asset and Blender/hand-off files were preserved. Work is saved in local logical commits on `gasp-integration`; nothing is pushed. Animation style is ready for your review.
