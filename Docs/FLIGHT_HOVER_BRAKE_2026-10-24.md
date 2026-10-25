# Hover and stopping revision — October 24, 2026

Completed 2026-10-24T06:21:31.588761+00:00, explicitly requested by the user after reviewing flight. Scope: distinct upright hover, smooth hover/flight transitions, and a visible momentum-bracing stop. Previous overnight automations remain removed. This was a new manual request, not an automatic resumption.

Hover and Takeoff now keep an upright, balanced body with gently bent knees and subtle breathing. The approved prone flight family is preserved. Accelerate fades in over the existing 0.45-second BlendTime instead of 0.14 seconds so it cannot suddenly replace upright hover. Takeoff fades with travel. Entry while already moving seeds the flying base immediately, under the existing global entry fade.

Brake now gathers the knees and brings the arms forward with a slight backward brace. Releasing movement at speed catches up to 0.95 pose weight, holds briefly after the stop (0.24 seconds), and settles into hover with the existing 0.38-second fade. BrakeBlendInTime is 0.13 seconds. Regular flight speeds, acceleration, steering, bank logic, physics, camera and release-to-falling behavior are unchanged.

Only Hover, Brake and Takeoff were rebaked/imported on each rig. Forward, Fast, all banks, Accelerate, Boost and Release data and assets remain unchanged; every other animation/report entry is preserved. The approved forward/fast/bank/boost/release assets are byte-identical to the live pre-change backup. No AnimInstance, movement ability or camera source changed.

Editor build succeeded with Unreal closed. All24 offline flight clip checks and both selective imports passed. Final engine checks passed: normal path, moving entry, quick braking, vertical rise/hover, repeated mirrored reentry across yaw wrap, and ground start at30/120 simulation steps. Visual capture includes the real evaluated pose through hover, acceleration, both turns, braking, release, landing and the later interruption/shadow checks. New assertions cover upright hover above75degrees, visible stopping brace, upright return and smooth torso motion. The final 60-step visual entry peaks at359.70deg/s, below500; the first candidate was visually too fast at about931deg/s in the30fps CSV and was revised. These are transition measurements, not a similarity score.

The headless fixture now waits30ground ticks before recording restoration settings, matching the initialization time naturally provided by the visual viewport warmup. Earlier A/B moving-entry checks failed before the final initial-pose correction and fixture warmup; they are retained as development evidence, not accepted results. Production movement restoration was not edited. Historical pass3 combat/hand-offset limitations remain in the previous handoff.

Review: outputs/Flight-hover-and-braking.mp4 (23seconds) and Flight-stop-to-hover.mp4 (3.6seconds). These HBB captures use the final hover/stop assets and timing; the final C correction only changes entry with existing momentum, which is checked in Pass3HBCMoving120. Both exports fully decode. Original reference and older reviews are preserved.

Backup: Saved/FlightHoverBrake_Before_20261024. Final checkpoint: Saved/FlightHoverBrake_Checked_20261024. Sources/assets/docs are copied; rebuilding is required after restoring a snapshot. No Git commit/push, purchases, credits, model changes or new automations.

Next action: user reviews the result in Unreal and makes any desired small adjustments. No autonomous work or usage-recovery task is scheduled.
