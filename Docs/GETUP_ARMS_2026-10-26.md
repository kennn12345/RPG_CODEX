# Backward arms during get-ups — 2026-10-26

Latest user feedback: arms sometimes bend backwards **while getting up**. This is a recovery-transition fix, not a new flight, Power, balance or ragdoll-asset tuning pass. Preserve the 29 authored get-ups and the pre-existing untracked GETUP_HANDOFF.md.

## Cause and change

`URPGPhysicsReactionComponent::ReturnToAnimated` deliberately pins the old ragdoll pose kinematically while fading physics out. `URPGBodyImpactComponent::StandUp` previously started the selected montage moving immediately. The target torso could begin rolling beneath the still-pinned arms, creating a bad intermediate shoulder pose even when the authored clip was safe.

`RPGBodyImpactComponent.cpp/.h` now holds that montage at its matched frame with play rate zero (keeps it active/playing for NPC ownership checks). Once the reaction reaches Animated, it eases the play rate to the requested rate over 0.14 seconds using SmoothStep. The existing 0.5–1 second pose-matched fade is retained; NPC busy duration includes that hold plus 0.14 seconds. A new knockdown cancels the held montage; an externally stopped/replaced montage clears the hold. The existing kinematic pinning and pose selection remain intact. No animations, physics assets, player controls, Power interfaces or maps were replaced.

## Reproduction and diagnostics

`RPGGetUpVarietyReview.cpp` can write actual evaluated component-space pelvis, chest, head and arm transforms to `arms.csv` with `RPG_GETUP_ARM_TRACE=1`. `RPG_GETUP_VARIETY_DIR` chooses the output folder. Both `RPG.Npc.GetUpRepeat` and `Visual.GetUpVariety` assert the montage stays on its matched frame while aligning. Set `RPG_GETUP_INTERRUPT=1` to inject one extra knockdown during alignment and assert it was exercised before all four recoveries finish.

Run `RPG_SKEL=uefn python Tools/ProtoAnim/getuparms.py <arms.csv>` to produce an adjacent analysis JSON and a nonzero exit on diagnostic violations. It measures the rendered/evaluated pose, excludes unconstrained raw falls, and checks elbow extension >= -5 degrees, elbow sideways <= 15 degrees and shoulder flexion -60..180 degrees. These broad regression bounds are not clinical anatomy or proof of every possible landing.

Evidence under `Saved/GetUpArms/`:

- `Control120`: four real falls before the fix. Six shoulder-bound violations during one FaceUpArmsOut fade; right shoulder crossed 180 to 231.58 degrees then wrapped to -97.32. No such bounds failures in the fully authored clip portion.
- `Hold120`: four real falls after the fix. Zero diagnostic violations. Fade left shoulder -24.88..166.05, right -16.50..161.04; minimum elbow -1.99 degrees, maximum sideways 7.17 degrees. Full clip portions also pass these bounds.
- `Interrupt30.log`: actual additional knockdown during the first alignment, then four completed recoveries using both knee styles; GetUpRepeat PASS. Log shows the first held LeftLead replaced by a new RightKnee get-up.
- `Build-Final.log`: C++ editor build succeeded with Unreal closed.

- `Visual60.log` / `Visual60/`: rendered four-fall integration PASS at 60 fixed steps/s, screenshots at 30 Hz. Actual fade shoulder ranges left -31.13..143.84, right -32.39..165.20; minimum elbow -0.20 degrees, sideways maximum 7.55 degrees. Zero diagnostic violations in fade and authored portions. Reviewed images around all four transitions, with both knee styles retained.
- `Final30.log`: Collision, GetUpRepeat (including asserted extra knockdown during alignment), GetUpVariety, ReactivePhysics and ReactiveStandalone all PASS at 30 fixed steps/s.

Preview: `C:/Users/cedar/Documents/Codex/2026-10-26/please-write-a-full-handoff-summary/outputs/Get-up-arms-fixed.mp4`, from the actual Visual60 sequence, labelled by selected get-up.

Existing unmatched extreme ragdoll starts, slope-contact limitations and inherited Strength5 Smoke/Power classification flake remain separate open items; no tests were weakened or Power retuned. These samples reproduce and remove the observed transition defect, not every possible joint configuration. User review of this correction is pending.
