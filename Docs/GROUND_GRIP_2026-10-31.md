# Knockdown distance and calibration completion — 2026-10-31

Continues b66cc6a and 508c00b on `gasp-integration`.

## Game changes

`URPGPhysicsReactionComponent::TryGroundSupport` checks the pelvis, upper chest, head and hands for support on an upward-facing world surface. A floor ray is followed by distance to the actual body shape; a gap up to 3 cm qualifies. Standing feet are excluded. This runs after 0.15 s only for falls already eligible for ground grip, stops once grip engages, and retains the existing pelvis-deceleration fallback. Maximum work is five 150 cm rays per active eligible fall per tick; no new simulated bodies or controllers are created. Normal Power launches do not run this support check.

The previous velocity-drop detector could miss a smooth landing. Hit notifications were evaluated but did not arrive reliably in this setup, so no notification/delegate changes are included in the final implementation.

Early ground grip alone did not resolve pre-landing travel: one straight knockdown calculated at 4.6 ft still reached about 8.7 ft before recovery. Its input velocity was derived for sliding, although the body was initially toppling through the air. `HandlePowerHit` now multiplies initial straight-knockdown horizontal velocity by `KnockdownHorizontalVelocityScale` (default 0.45) before the existing mass-normalized distribution. Vertical velocity, launches, failed-catch `Launch()` momentum and already-down hits retain their existing handling. The universal Strength/charge ladder continues to govern extreme launches.

The existing `GroundGripDamping=8`, bracing, mass table and whole-body ragdoll remain in use. This is calibrated stopping behavior, not a physiological force model or visual approval.

## Regression coverage and measurements

- `RPG.Power.GroundGrip` exercises a real Standard Bandit with a controlled 140 cm knockdown target and nominal 700 cm/s input. Across the final regression and three dedicated repeats, physical pelvis travel was 128–138 cm (4.2–4.5 ft). Travel after grip was 16–40 cm (0.5–1.3 ft), below the 61 cm limit.
- `RPG.Npc.HitOutcomes` now guards recovering-catch travel (1–5 ft including pose tolerance), failed-catch total travel (at most 6 ft), and post-grip pelvis travel (at most 2 ft). The final full run's failed catch moved 91 cm total and 19 cm after grip. Straight actual Knockdown outcomes are distinguished from the launch that the `knockdown_braced` scenario can legitimately select.
- Calibration now measures falling pelvis travel from the initial pelvis position and freezes its endpoint before the get-up blend. Final actor placement is reported separately. The launch model covers arc plus slide, rather than the chosen get-up offset.
- Launches under 8 ft calculated use a 3 ft absolute tolerance. Longer launches retain 70–135%. Straight knockdowns also require physical travel no more than calculated distance plus a 2 ft body-pose allowance; delayed catch failures retain their separate stumble/fall checks.

The reused-NPC sweep produced outliers after earlier recoveries, including a short launch at 12% and a following launch at 153%. Each Strength now starts in a fresh PIE session on the same Standard Bandit, and the test waits for at least 0.25 s of quiet feet/pelvis before striking. It fails after 5 s if that standing fixture cannot settle. This removes prior get-up, balance and body-state carryover from the calibration fixture. It does not establish identical launch distances from every gameplay pose.

Validation:

- Editor Development build succeeded.
- Full requested regression: 24/24 (the previous 23 plus GroundGrip).
- Dedicated `RPG.Npc.ReactionStress`: 3/3.
- After the test-only fixture-isolation change: three consecutive `RPG.Power.Calibration+RPG.Power.GroundGrip` runs passed, 2 tests each. Eighteen observed launches measured 94–107% of their calculated distances.
- Rendered NPC/player outcome checks passed with fixed 60 Hz timing.
- Checked logs contain zero PhysicsControl warnings, dropped reactive impulses and fully-simulated-mesh movement warnings. Compiler/engine deprecation warnings are separate.

The full regression preceded the final test-only fixture isolation; game code was unchanged during those isolated repeats. Full logs, source hashes, data and screenshots are in the Codex deliverables.

## Review still needed

Review the updated failed-catch, braced-launch and knockout contact sheets at full speed in Unreal as well as in stills. A bent/raised leg during landing remains a visual review item. The user still needs to confirm the old player flying-crash movement warning in that particular manual scenario; automated absence of the warning is not reproduction of it.

The retired-dummy-dependent `RPG.Combat.PhysicsReaction` and `RPG.Combat.TestDummy` tests remain outside this regression, as described in Claude's handoff. No migration or retirement decision was made here.

The subsequent punch-mechanics audit is read-only. Its charge-tier, timing, effective-mass and impact-budget recommendations are not implemented by this fix.
