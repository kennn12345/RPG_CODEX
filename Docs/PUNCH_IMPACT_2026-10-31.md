# Punch contact and brace transition — 31 October 2026

This follows the approved first implementation scope from the combat audit: contact direction and the first quarter-second of the standard right punch's NPC response.

## Changes

- `RPGMeleeHitComponent::BeginPlay` orders hit detection after the mesh and an existing strike-contact component. `RPGControllerInputComponent::EnsureHelperComponents` also installs the contact prerequisite after creating the dynamic player helpers.
- Latched hits use `RPGStrikeContactComponent::GetAnimatedTravel`, so direction/speed comes from the contact producer's uncorrected hand/foot end-bone sample. This is a limb-motion estimate, rather than exact knuckle/contact-point velocity. Swept hits still use their sweep-point travel with contact correction removed. This avoids mixing a fresh fist pose with an earlier correction during confirmation.
- `RPGPhysicsReactionComponent::ApplyBraceMotors` sets `AngularTargetVelocityMultiplier=0`. Brace orientation targets and damping remain active. The abrupt standing-to-reach target change is no longer fed into the damping controller as a desired angular velocity. Powered reactions and flight follow explicitly restore the multiplier to 1.
- Melee exposes transient `LastHit` and `ConfirmedHitCount` for inspection and integration checks. `RPG.Npc.PunchImpact` uses the real Enhanced Input action, actual punch animation/contact/Power/NPC path, fresh PIE actors per case, chest/chin aims, multiple distances, Strength 5/12 and an out-of-reach miss. It checks incoming contact, one confirmation per target, standing versus whole-body response, finite velocities and excessive transition spin in the controlled fixture. The test's spin threshold is diagnostic, not a gameplay clamp.

## Evidence

Three fresh pre-change Strength-12 chest launches reproduced large early angular spikes. Maximum over all 23 bodies in the first 250 ms was 41,392–45,185 degrees/s. Disabling brace strength substantially reduced the spike; clearing cached velocities alone did not. Disabling animation angular target-velocity feed-forward retained the brace orientation drive and reduced torso/head/hand peaks to roughly 800/980/1,340 degrees/s in that diagnostic run.

Three fresh post-change launches reported incoming contact at 577–585 cm/s, with strike-direction/surface-normal dot products -0.85 to -0.87. Their all-body peak angular speeds were 1,299–1,475 degrees/s. Effective Power remained 12 and damage 59.5861. These are controlled observations, not a guarantee for every pose or a visual-quality rating. Before/after recordings include both the contact and brace changes; the separate controller diagnostic isolates the target-velocity contribution.

No punch asset, Strength ladder, input mapping, charge threshold, dedicated charged animation, mass setting or ragdoll system was changed. The earlier knockdown/ground-grip fix is retained. Charged animations remain intentionally unimplemented; placeholder charge gameplay is a separate decision.

## Verification

The Editor Development build succeeds. All 24 existing regression tests passed. The new five-case `RPG.Npc.PunchImpact` test passed separately alongside another Calibration + GroundGrip run (3/3 tests). The combined regression's earlier PunchImpact failure was a test setup issue corrected afterward: PIE transitions are deferred, so the fixture now waits after StartPIE and resets setup when the play world changes. That last change affects tests only. The production game code is the same in both validation runs.

The new integration test's chin-aimed cases struck `clavicle_r` and `spine_05`, respectively. It verifies stable contact/response, not exact chin placement. Precise NPC chin targeting remains a follow-up alongside the punch trajectory/aim review.

Launches across the two calibration sweeps measured 98–110% of their calculated distance, including a 124.2 ft measured launch. The two controlled GroundGrip runs measured 138–139 cm total, with 17–23 cm after grip. Detailed evidence is in `outputs/Impact_Fix_Results.md` in the Codex session workspace. Run `RPG.Npc.PunchImpact` alongside the existing combat/NPC/player/Power regression, with Unreal closed before rebuilding. Review the resulting falls at full speed in Unreal before accepting their appearance.

The audit's animation timing, kinetic sequencing, standing knockback distance, common impact budget, and future weapon/variant proposals remain follow-up work. This change establishes a more stable contact/physics baseline for that visual iteration.
