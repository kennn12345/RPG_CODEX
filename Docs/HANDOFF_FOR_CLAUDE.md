# RPG_CODEX handoff for Claude — October 26, 2026

Scope: everything after Claude's `ccbbbf8` (2026-10-23). Verified against Git, current working files, Saved status/evidence, and the Codex chat “Make flight demo video downloadable.” Paths below are relative to `C:\UNREAL PROJECTS\RPG_CODEX` unless absolute. This is a handoff, not authorization to resume the expired overnight flight automation.

## 1. Git state and recovery

- Branch: `gasp-integration`, tracking `origin/gasp-integration`; remote `https://github.com/kennn12345/RPG_CODEX.git`.
- Latest implementation commit: `7974c0a1c508795367ca6a4acbc0847468f51a3f`, committed/pushed October 24 at 23:01 America/Chicago. Annotated recovery tag: `checkpoint/pre-spell-aura-2026-10-24`, also pushed.
- Exactly ONE implementation commit after `ccbbbf8`: `7974c0a` — checkpoints accepted flight poses/transitions, directional ground/hover movement, camera, spells and dummy leg responses before the aura experiment. Many development passes were saved as local snapshots rather than individual commits.
- This document is committed/pushed in a subsequent documentation-only commit; obtain its exact hash with `git log -1 -- Docs/HANDOFF_FOR_CLAUDE.md` (a commit cannot include its own hash).
- Only local/remote branches found: `gasp-integration` and `main`. Fresh fetch/remote-head check: main remains `2a17eec70d9fd06e71a5019daa2eb7d4e1718934` (“Docs: GASP integration handoff brief (for Codex, then Claude)”). No changes from this work were merged into main; no new branch was found in refs/reflog.
- **Working tree intentionally remains dirty.** The user requested a GitHub checkpoint BEFORE experimenting with the spell. Preserve that separation: this handoff commit stages only this document. The exact dirty list is in Appendix A. The experiment is saved locally, not committed/pushed. Do not reset, clean, checkout over, or blindly stash these files.
- Saved snapshots, logs, generated reviews and local Python dependencies are generally ignored/local, not guaranteed present on GitHub. Rebuild after restoring source snapshots; snapshots do not supply matching DLLs.

## 2. Flight architecture and final behavior

`Source/RPG_CODEX/RPGFlightAnimationComponent.h/.cpp` replaces montage switching with continuous full-body animation-proxy blending. `ResolveFlightClips` resolves the correct skeleton's 12 clips; `StartFlightAction` handles Takeoff/Accelerate/Boost/Release. Common loop phase prevents phase resets. Hover/Forward/Fast/Brake plus authored mirrored ordinary/fast banks blend continuously. `RPGPrototypeAnimationComponent.cpp` avoids the competing old flight montage.

Final presentation: **upright hover and takeoff**, flat balanced ordinary/fast forward flight, authored outside-arm brace/inside-arm tuck in banks, visible momentum catch when stopping, then upright hover. Release to falling preserves the deliberate opposing arm/leg stride; ground locomotion owns contact. Earlier `Docs/FLIGHT_HANDOFF_2026-10-23.md` describes an intermediate horizontal hover and 600/1600 speeds: those are superseded by the hover/brake and speed passes below.

`UpdateBodyReaction` pitches along the flight path and banks about the hips. Horizontal speed drives turn demand, so pure ascent cannot activate a bank. Below 50 cm/s horizontal speed the travel heading is invalidated; pure rise and subsequent hover retain controller-facing rather than GASP's zero-yaw fallback. `ResetBodyReaction` releases lean with the same eased pose weight and updates private pitch/roll/yaw from the restored transform, preventing interrupted-resume jumps. Moving StopFlight/StartFlight reentry keeps the active Release sample and fades into flight over BlendTime rather than replaying Takeoff; repeated toggles preserve continuity and pivot-position residuals.

Accelerate is armed only in Travel .1–.85; crossing .85 consumes it. It fades with boost weight. Takeoff fades with travel, moving entry seeds the flying base, braking blends over unfinished acceleration/boost, and Takeoff/Release retain transition priority. Upright-to-flight acceleration fade was increased .14 → .45 s. Stopping catches up to .95 brake weight, holds .24 s after stopping, then fades .38 s.

`RPGAnimInstance.h/.cpp`: `PreUpdate` copies flight clips/phase/weights/action, flow, rear sway, directional-hover and spell snapshots to the worker proxy and shadow. Evaluation order: main graph → `ApplyFlight` → `ApplyDirectionalHover` → guard → spine aim → `ApplyFlow` → `ApplyRearSway` → `ApplySpellPose` → strike contact. `ApplyFlow` still conjugates component-space rotations through the accumulated parent rotation and normalizes; its delta angle now multiplies by `1-DirectionalHover.Weight` so hover's authored pose owns the result. `ApplyRearSway` hinges pelvis about spine_01, counter-rotates the waist to preserve upper-body bank, and bends calves only about the anatomical knee hinge.

`UpdateLimbFlow`: only upper arms/thighs use small damped inertia; fixed 120 Hz substeps (maximum 8) and bounded input acceleration (6000 cm/s²), outward clearance, finite initialized arrays. The previous output rotation is copied BEFORE clearing FlowRotations, then removed from measured limb direction to avoid feedback. Arm limit is min(FlowMaxAngle,8°); legs have their own limit. Rear turn/climb lag and calf settling were added later because the user found the back half too stiff.

`RPGPhysicsReactionComponent::SetFlightFollow` itself has **no diff since ccbbbf8**. Flight calls `SetFlightFollow(false)` unconditionally. `bFollowThrough=false` remains; enabling its serialized property does not opt back into the rejected physics behavior. Legacy bracing/wingsuit/rudder/shoulder/spine steering properties remain for asset compatibility but are INACTIVE. Edit authored bank/brake poses rather than stacking them back on.

### Flight tuning, ccbbbf8 → final source defaults

| Setting | Old → current |
|---|---|
| BlendTime | .35 → .45 s |
| MaxPitch | 50 → 75° |
| AccelLean | 10 → 0 |
| BankPerTurn | .6 → .8 |
| MaxBank | 55 → 45° |
| HoverReaction | .5 → 1 |
| ReactionSpeed | 6 → 8 |
| FlowStiffness | 60 → 90 |
| FlowStiffnessAtSpeed | .6 → .8 |
| FlowDamping | .55 → 1 |
| FlowInertia | 16 → 2.5 |
| FlowDrag | 2.5 → .8 |
| FlowMaxAngle | 28 → 5° (arm hard ceiling 8°) |
| FlowTurn | .3 → .08 |
| Inactive FlowBrace/FlowBraceSharp/FlowBraceMax | 22/12/80 → 24/8/65 |
| Inactive WingsuitArms/WingsuitLegs | 55/18 → 24/7 |
| Inactive ShoulderDip/ShoulderDipMax | 45/55 → 22/28 |
| Inactive TorsoArch/RudderPerForce/RudderPerTurn/RudderMax | 18/25/18/30 → 8/10/8/18 |

New defaults: ForwardFullSpeed=650 cm/s; PoseResponseTime=.24 s; ExitBlendTime=.9 s; ReleasePoseHoldTime=.3 s; GroundBlendOutTime=.16 s; ActionBlendOutTime=.12 s; BrakeBlendInTime=.13 s; BrakeBlendOutTime=.38 s; CruisePathPitchScale=1; FullTurnPoseRate=50°/s; RearTurnLag=.16 s; RearSwayMaxAngle=9°; RearSwayStiffness=48; RearSwayDamping=.85; LegFlowMaxAngle=8°; RearKneeFlex=3.5°. New inactive compatibility property FlowElbowBrace=45. Unchanged: ForwardSpeed=150, FastSpeed=900 (pose references, NOT gameplay speed), ClimbPitch=1, TurnFollowSpeed=5, bLimbFlow=true, bFollowThrough=false, BraceStiffen=3, WingsuitForce=2.5, ElevatorPerDegree=.35, ElevatorMax=20, FlapBrake=2500 (active authored-brake deceleration threshold), FlapArms=30/FlapLegs=8 (inactive). Appendix C includes exact header and pose diffs, including unchanged context and new fields.

Movement: `RPGMovementAbilityComponent` adds FlightAcceleration=2000 and FastFlightAcceleration=3600 cm/s² and temporary tick prerequisites AFTER GASP pre-CMC and BEFORE CharacterMovement; GASP had overwritten acceleration with 800. Restore acceleration/settings and only prerequisites added by flight on exit. Later NormalFlightSpeedScale=1.1 changes normal MaxFlySpeed 600 → 660 once at BeginPlay; FastFlightSpeed 1600 → 2000 (+25%). Directional hover remains 320; temporary pre-movement cap restores normal/fast speed on exit/landing/toggle. FlyingBrakingDeceleration remains 1500. Existing look-pitch/rise input is retained; directional hover suppresses travel look-pitch coupling.

Camera: boost adds up to +8 FOV/+140 cm over .35 s and restores on exit. Later raised third-person shoulder framing: base socket +25 right/+35 up cm; combat adds +35 right/+25 up (total +60/+60), smooth .15 s; spell weights now activate combat framing alongside melee AimAlpha. Editable fields in `RPGCameraModeComponent.h`; original spring-arm collision and first-person path retained.

### Generator and actual imports

`Tools/ProtoAnim/poses.py` rewrites FLY_HOVER/FLY_FORWARD/FLY_FAST with `flight_pose`, actual shoulder/hip-relative `flight_target`, paired limb geometry, toe-out cancellation and relaxed hands. IMPORTANT: the early FLY_HOVER variable remains a horizontal action seed; final `FLIGHT_HOVER` is reassigned to upright `FLY_HOVER_IDLE`. Do not mistake the first assignment for the exported hover.

`hover_loop`: defaults T=4/keys_n=64; old rise 3 → 1.6*amp, fore/aft drift 3 → .8*amp, pelvis pitch 3 → 1*amp, roll 1.2 → .65*amp; removes inherited asymmetric idle breathing and running/kicking feel. Final forward loop amp=.25/legs=(.4,.4), fast=.12/(.2,.2), banks=.20/(.3,.3), fast banks=.12/(.2,.2). Final upright hover=.65/(.65,.65); brake=.35/(.5,.5). All eight loops share four-second phase; periodic neighbor sampling fixes seam velocity. Actions use solved-pose quaternion quintic curves with shared key velocities rather than IK interpolation through straight-leg singularities.

New `flight_symmetry.py`: level_pose removes standing twist and balances shoulder pivots; canonical right banks reflect through real reference frames; flatten_pose maintains prone travel; release_stride_pose keeps the intentional opposed stride. Fast legs went .97/.02 extension/lift → .92/.18 (P1) → .88/.24 (P2) → **.96/.08 final SpeedBalance**. Final neutral knees about 33°, versus P2 ~48°; later sway adds transient calf bend. Release keys 0/.22/.48/.75 s; exported .767 s; last key held during fade. Full exact authored coordinate changes are preserved in Appendix C instead of hiding them behind prose.

YES: all flight clips were rebaked and reimported for both Manny `/Game/RPG/Animations/Prototype/` and UEFN `/Game/RPG/Animations/SK_UEFN_Mannequin/`. 12 per rig: Hover, Forward, Fast, Brake, BankL/R, BankFastL/R, Takeoff, Accelerate, Boost, Release. Hover/brake pass selectively replaced Hover/Brake/Takeoff; later SpeedBalance replaced Fast/BankFastL/R/Boost/Release. Subsequent sway/hover posing is runtime source only. `skel_profile.py` has **no diff** since ccbbbf8; RPG_SKEL=uefn continues selecting UEFN dump/clips/import folder. Non-flight clips were preserved; partial import reports must be merged with preserved full reports. Speed-balance audit reports are checked in.

## 3. Everything else touched

- `BP_RPGPlayer`: no Blueprint asset change since baseline. Controller helper auto-adds new energy-spell and directional-hover components at runtime. Existing player consumes updated C++/AnimInstance/flight assets. No claim of a fresh Blueprint compile sweep in this handoff.
- `RPGGASPInputBridgeComponent`: new bDirectionalGroundMovement uses existing GASP eight-way run databases, WantsToStrafe/view-facing during side/back movement and stop; side/back cancels forward sprint. Existing starts/stops/diagonals preserved; backward clip selection verified on actual motion matching. No GASP database or animation edits.
- `RPGDirectionalHoverComponent`/Types: four-direction view-relative airborne pose, diagonals, smooth reverse/stop; light forward stays hover, >65% forward action in cone permits travel after reversal; spells retain hover. Idle-only visual bob 1.6 cm amplitude/3.4 s, capsule stationary. Knee lifts first, then opens at hip, then foot tucks inward; opposite arm follows its own leg and clears torso. Smooth stationary turns/handoff replace stick-direction snapping.
- Final new hover defaults: HoverSpeed=320, PoseResponse=.22 s, SideKneeLift=30°, SideKneeBend=46°, SideKneeOut=22° (intermediate 12→20, then final 22), SideHipOpen=40°, SideFootTuck=14°, FootTuckResponse=.35 s, TrailingArmMinOut=8°, TrailingArmSpacing=5.08 cm, SideBodyLean=12°, SideChestCounterLean=2°, SideArmLean=14°, SideArmResponse=.24 s, KneeDriftTime=.4 s, HoverTurnSpeed=150°/s, HoverTurnAcceleration=540°/s². Hip opening return .16→.22 s. Actual rendered turn peak ~173°/s; nominal speed property is not a promise of a hard instantaneous observed limit. Spell aiming keeps faster authority.
- `RPGCombatTestDummy.h/.cpp`: separate ReceiveEnergyHit_Implementation, energy hit counters/interface, single damage application, wandering pause, no shove, health depletion → existing ragdoll. Leg hits select existing directional GASP catch steps, cap root travel to EnergyTapStepDistance=22/EnergyChargedStepDistance=48 cm through StaggerRootMotionScale. Repeated hits do not restart an active catch step. Melee resets scale to 1.
- `RPGPhysicsReactionComponent`: HandleEnergyHit calls existing powered reaction (dazed at charge>=.4), records actual bone, caps impulse by body mass × lerp(180,450,charge); EnergyLegMotorScale=.9 replaces struck-leg standing 2.5 temporarily then recovers. Opposite leg retains support. General Power System algorithm unchanged. Existing whole-body ragdoll/get-up logic remains; directional staggers and reactive get-ups Claude already had before ccbbbf8 are not new Codex work here.
- `L_DummyField`, right-cross assets/authoring, Power System files, project plugin list and `.uproject`, `skel_profile.py`: no tracked changes since ccbbbf8. No map was saved by review harnesses (temporary PIE floors/targets/cameras). Only Config change is `/Script/UnrealEd.ProjectPackagingSettings` +DirectoriesToAlwaysCook=(Path="/Game/RPG/Spells/Energy").

## 4. Energy spells and local experiment

New `RPGEnergySpellComponent`, `RPGEnergyOrb`, `RPGEnergySpellTypes`, tests and `Tools/Spells` implement native effects without purchased VFX packs. LB left/RB right, independent held charge, release once, cap/hold at 1.5 s; RT/LT punches and guard retained. Ground/slow hover casting, reticle/cursor aim with body rotation and anatomical two-bone IK; palms outward; per-hand 12° forward half cone, torso aim 8°, wait for alignment/raised arm before release, cancel unreachable release after 2 s. Melee/guard/root motion/death/possession loss/travel flight cancel. Zero pose weight restores old path exactly.

Earlier size/speed evolution: BaseRadius=5→10 cm; projectile 2200→3300 cm/s; FullSizeMultiplier=2. Baseline commit therefore has 10/20 cm radii. Local aura experiment changes radius **10→15 cm**, full **20→30 cm**, both +50%. Range=6000 cm; damage 6–28, impulse 1200–5000 before mass cap unchanged. Swept movement and initial palm-to-muzzle sweep prevent tunneling/wall skips; source actor ignored; movement capsules skipped while skeletal collision remains eligible. Impact visual expands to twice incoming diameter, no radial damage.

Local experiment also changes spell reach clamp .89–.98→.95–.987, formula .965-.065*charge+extension*(.015+.065*charge) → .978-.028*charge+extension*(.009+.028*charge); target down offset 4→1.5 cm, outward 2→1 cm, elbow pole outward .45→.30 limb lengths. Maintains bend/recoil. Three pooled static clusters (27 segments) flicker/relocate only while button remains held at full charge. Clear synchronously on release before extension delay, never accompany projectile/impact. Radius/aspect/emissive pulse; new M_EnergyStatic material. User has NOT yet given an explicit post-experiment visual approval: last message only asked whether saved; earlier “looks goods” approved pre-experiment hover/current game.

## 5. Assets, tools and downloads

Appendix B lists every tracked new Content/Tools file since baseline, plus the untracked static material. Nine added Fly assets per rig; three existing per rig modified. Energy materials Core/Ring/Shell committed, Static local only. No new paid asset or installed project plugin is evidenced. A Fab projectile pack was discussed but the implemented effect is original/native.

Reference on PC: `C:\Users\cedar\Downloads\IMG_8594.mp4`; preserved review copy `C:\Users\cedar\Documents\Codex\2026-10-23\referenced-chatgpt-conversation-this-is-an\outputs\Reference-flight-original.mp4` (SHA256 9c7ecce593d0e4288711691a995f8e2444591221bb3499c6eca4f430e7ed2ad3). Review videos and verification JSON live beside it and under project Saved. Local `work/video-deps/av` proves PyAV/FFmpeg-backed video dependencies were installed for export; this is outside the repository. No standalone system ffmpeg installation is verified by this audit; do not infer one from those libraries. UE 5.8 bundled Python is available at `C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe`.

## 6. User feedback, preferences and scope

User explicitly approved actual flight: “flight itself looks very good … everything else is great do not change actual flight,” asking distinct upright hover, smooth transitions and stopping brace. After that: “looks good we did it small tweaks from here out.” Later forward flight should retain forward momentum; requested backward/side run and hover while looking forward, idle-only bob, softer rear-half sway, leading knee lift then outward drift/opening, inward foot tuck, opposite arm matching leg with another two inches clearance. Latest pre-experiment approval: “looks goods,” then larger/straighter/static-pulsing spell request. Latest requested flight refinements have implementations; no pending new flight feature authorization is implied.

Earlier reference goals: flat balanced neutral flight, fix wrong right-turn shoulder lift, mirror banks, natural bracing/wingsuit-like silhouettes and shoulder dip into turns, relaxed anatomy and smooth follow-through. Final system realizes turns primarily through authored mirrored clips plus bounded rear sway, not the old additive steering knobs. Do not restore exaggerated legacy systems simply because older wishes mention them.

Judge actual evaluated Unreal motion, both directions and transitions against the supplied reference; technical checks complement visual review. Preserve PROJECT_STATUS.md evidence language: automated/headless or scripted PIE coverage is not manual user testing; logs show actions occurred, not that the user approved their appearance. Requested 97% then 99% are subjective visual aspirations, NOT measured similarity scores. Preserve bone lengths, no inverted knees/rubbery limbs or body clipping. Keep physics flight follow-through off. Whole-body ragdoll remains the existing rule for true limp/knockout rather than partial-body fake knockdown; localized nonlethal powered flinch/stagger remains allowed. Close Unreal before EVERY C++ build. No purchases/credits/model changes. User dislikes repetitive permission prompts and authorized necessary work within scope.

The October 23 overnight flight task had a hard stop 8 PM America/Chicago and 95% five-hour usage cutoff; it stopped and its automations were disabled/deleted. Later manual requests were separate authorization. Never revive that expired schedule. A “don't film to save usage” preference was mentioned in the handoff request as an example; this audit did not locate its exact original wording. Do not invent an absolute prohibition: past captures were explicitly requested/reviewed. For this handoff no new filming was done; ask/follow latest user steering before substantial new capture work.

## 7. Known problems and incomplete work

- Broad historical combat suite still fails: original-source control 36 unique failure descriptions, flight pass3C 35, all a subset (no new descriptions). Do not call combat green. No broad combat rerun after latest hover/spells; real GASP NPC/dummy rebuild and remaining combat animations were earlier future jobs, not completed here.
- Older headless Pass3DStrafeLeft showed unexplained 42–55 cm right hand/elbow offset even before strafe; diagnostic/stress/later checks passed. Cause UNKNOWN, not a proven fix. Defensive zero initialization is not proof of cause. Evidence retained.
- Initial moving point light spell candidate hit UE 5.8 GPU Scene stale-light ensure; removed light, emissive-only accepted effect passed.
- Initial enlarged-ball leg test hit hanging hand instead of knee; test aim lowered to shin, production collision unchanged. StaticAuraLeg120A fails as fixture; B passes calf_l/r with unchanged substantive assertions. Other early fixture/cache/height and initial entry candidates failed then were revised; do not count them as accepted passes.
- UE engine/toolchain deprecation warnings remain (e.g. Character.h APawn::GetMovementBase C4996, Chaos radius/margin accessors); latest accepted builds succeeded. No confirmed new Blueprint compile error from this work; no exhaustive Blueprint/package verification performed.
- No packaged build, exhaustive obstacle/long-session test, original-data animation match or recreated wind audio. Local aura experiment awaits user play/visual approval and intentional decision to keep/tune/revert/commit. Preserve baseline recovery tag.

## 8. Tests and evidence

This handoff reran `Tools/ProtoAnim/flightcheck.py` with bundled UE Python for Manny AND RPG_SKEL=uefn: all 24 clips PASS, plus upright hover/flat cruise/level shoulders/mirrored turns/bone lengths and release-stride checks. Current peak local speeds: Manny Hover 4.12/Forward 1.45/Fast .78/Boost 57.79/Release 452.29 deg/s; UEFN 4.61/1.62/.79/57.17/452.29. No production source/assets altered or rebaked for this handoff. No engine build/test rerun needed for documentation-only commit; following are prior recorded results.

- Editor builds with Unreal closed: passed across accepted passes, including SpellStatic-Build-A.log.
- `RPG.Flight.Transitions` and `Visual.FlightReview`: accepted 30/60/120 fixed-step paths cover input, mirrored banks/yaw seam, climb/dive, quick brake, moving/ground entry, vertical rise→hover, full/partial combat yield, rapid/repeated flight toggles, camera/acceleration/tick/settings restoration, shadow and landing.
- Before/after: vertical facing error 161.54°→<.001°, bank .802→0; pause error 130.45°→<.001°. Full interruption stale-lean resume 2702.9→~88.7°/s, partial 1207.1→434.3 (<600 bound); moving reentry old 521.3°/s at60 and1053.8 at120→227/231.2; hip rate705.3→~29.6 cm/s. Upright hover initial candidate ~931°/s revised to359.70 (<500). These are regression measurements, not similarity/performance claims.
- `RPG.Movement.Directional` / `Visual.DirectionalMovement`: passed headless 30/120 and rendered60 across later passes. Actual backward GASP clip selected; all direction bits, 320 hover speed/restoration, idle bob ~3.19 cm peak-to-peak, zero drift, moving casts, flight/landing checked. `RPGHoverTurnTest.cpp` adds braking side-pulse handoff, left/right stationary turn and yaw wrap; accepted120/rendered60, settled error<.2°.
- `RPG.Spells.Energy` / `Visual.EnergySpell`: passed accepted headless120/rendered60 (earlier30 too), independent/full charges, single release, actual aim/impact, wall/capsule traces, punches/cancel/zero overlay restoration. Latest static tests verify actual render components, no early/released/projectile arcs, relocation, shape pulse, elbow extension without lockout, ground+hover gameplay/profile cameras.
- `RPGEnergyLegBalanceTest.cpp`: both legs, tap/charge real projectiles, local force and22/48cm recovery steps; accepted120/rendered60. Example charged impulse2700 vs tap1080 kg cm/s.
- Final hover arm spacing: Directional-HoverArmGap120A.log, Directional-HoverArmGapVisualA.log, Spell-HoverArmGapSpell120A.log pass. Static experiment: Spell-StaticAuraLeg120B.log and Saved/SpellStatic_Status.md identify final checks. Accepted logs report no Error entries; earlier failing controls remain intentionally.

Detailed local evidence/status chronology: Saved/FlightPass3_Status.md; FlightHoverBrake_Status.md; EnergySpell_Status.md; SpellFacing_Status.md; SpellPalm_Status.md; CameraFraming_Status.md; BackwardRun_Status.md; DirectionalMovement_Status.md; HoverArms_Status.md; SpeedBalance_Status.md; FlightSway_Status.md; HoverTurn_Status.md; HoverBalance_Status.md; HoverOpen_Status.md; HoverFoot_Status.md; HoverArmGap_Status.md; SpellStatic_Status.md. These are LOCAL and more recent than older Docs. Their checked overlays layer over earlier full snapshots; don't restore an old full snapshot alone and assume it includes later changes.

## 9. Next steps

1. Read this file, inspect `git status` and compare baseline tag vs working spell experiment. Rebuild only with Unreal closed if continuing code work.
2. Have user play/review latest local spell aura and decide whether to retain/tune it. Baseline is already protected. No outstanding decision blocks this documentation handoff.
3. Preserve accepted flight and make only user-requested small changes. If continuing combat/NPC work, inspect existing broad-suite failures first and treat it as its own scope; do not claim flight tests validate combat.
4. Eventually run package/cook and broader Blueprint/combat validation when relevant; current prototype has no such acceptance claim.

## Appendices generated directly from Git/current files

The following are audit data (tabs expanded and trailing whitespace trimmed for Markdown) captured before the documentation commit. Paths/status distinguish committed baseline additions from the deliberately uncommitted experiment. Header/pose diffs provide every changed default and authored coordinate without relying on reconstruction from prose.

### Appendix A — intentionally uncommitted experiment at audit time
```text
 M Content/RPG/Spells/Energy/M_EnergyCore.uasset
 M Content/RPG/Spells/Energy/M_EnergyRing.uasset
 M Content/RPG/Spells/Energy/M_EnergyShell.uasset
 M Docs/ENERGY_SPELL_2026-10-24.md
 M Source/RPG_CODEX/RPGAnimInstance.cpp
 M Source/RPG_CODEX/RPGEnergyLegBalanceTest.cpp
 M Source/RPG_CODEX/RPGEnergyOrb.cpp
 M Source/RPG_CODEX/RPGEnergyOrb.h
 M Source/RPG_CODEX/RPGEnergySpellComponent.cpp
 M Source/RPG_CODEX/RPGEnergySpellComponent.h
 M Source/RPG_CODEX/RPGEnergySpellTest.cpp
 M Tools/Spells/build_energy_materials.py
?? Content/RPG/Spells/Energy/M_EnergyStatic.uasset
```
### Appendix B — all tracked changes since ccbbbf8 (A=new file, M=modified)
```text
M    Config/DefaultGame.ini
A    Content/RPG/Animations/Prototype/RPG_Proto_FlyAccelerate.uasset
A    Content/RPG/Animations/Prototype/RPG_Proto_FlyBankFastL.uasset
A    Content/RPG/Animations/Prototype/RPG_Proto_FlyBankFastR.uasset
A    Content/RPG/Animations/Prototype/RPG_Proto_FlyBankL.uasset
A    Content/RPG/Animations/Prototype/RPG_Proto_FlyBankR.uasset
A    Content/RPG/Animations/Prototype/RPG_Proto_FlyBoost.uasset
A    Content/RPG/Animations/Prototype/RPG_Proto_FlyBrake.uasset
M    Content/RPG/Animations/Prototype/RPG_Proto_FlyFast.uasset
M    Content/RPG/Animations/Prototype/RPG_Proto_FlyForward.uasset
M    Content/RPG/Animations/Prototype/RPG_Proto_FlyHover.uasset
A    Content/RPG/Animations/Prototype/RPG_Proto_FlyRelease.uasset
A    Content/RPG/Animations/Prototype/RPG_Proto_FlyTakeoff.uasset
A    Content/RPG/Animations/SK_UEFN_Mannequin/RPG_Proto_FlyAccelerate.uasset
A    Content/RPG/Animations/SK_UEFN_Mannequin/RPG_Proto_FlyBankFastL.uasset
A    Content/RPG/Animations/SK_UEFN_Mannequin/RPG_Proto_FlyBankFastR.uasset
A    Content/RPG/Animations/SK_UEFN_Mannequin/RPG_Proto_FlyBankL.uasset
A    Content/RPG/Animations/SK_UEFN_Mannequin/RPG_Proto_FlyBankR.uasset
A    Content/RPG/Animations/SK_UEFN_Mannequin/RPG_Proto_FlyBoost.uasset
A    Content/RPG/Animations/SK_UEFN_Mannequin/RPG_Proto_FlyBrake.uasset
M    Content/RPG/Animations/SK_UEFN_Mannequin/RPG_Proto_FlyFast.uasset
M    Content/RPG/Animations/SK_UEFN_Mannequin/RPG_Proto_FlyForward.uasset
M    Content/RPG/Animations/SK_UEFN_Mannequin/RPG_Proto_FlyHover.uasset
A    Content/RPG/Animations/SK_UEFN_Mannequin/RPG_Proto_FlyRelease.uasset
A    Content/RPG/Animations/SK_UEFN_Mannequin/RPG_Proto_FlyTakeoff.uasset
A    Content/RPG/Spells/Energy/M_EnergyCore.uasset
A    Content/RPG/Spells/Energy/M_EnergyRing.uasset
A    Content/RPG/Spells/Energy/M_EnergyShell.uasset
A    Docs/CAMERA_FRAMING_2026-10-24.md
A    Docs/DIRECTIONAL_MOVEMENT_2026-10-24.md
A    Docs/ENERGY_SPELL_2026-10-24.md
A    Docs/FLIGHT_HANDOFF_2026-10-23.md
A    Docs/FLIGHT_HOVER_BRAKE_2026-10-24.md
M    Source/RPG_CODEX/RPGAnimInstance.cpp
M    Source/RPG_CODEX/RPGAnimInstance.h
M    Source/RPG_CODEX/RPGCameraModeComponent.cpp
M    Source/RPG_CODEX/RPGCameraModeComponent.h
M    Source/RPG_CODEX/RPGCombatTestDummy.cpp
M    Source/RPG_CODEX/RPGCombatTestDummy.h
M    Source/RPG_CODEX/RPGControllerInputComponent.cpp
A    Source/RPG_CODEX/RPGDirectionalHoverComponent.cpp
A    Source/RPG_CODEX/RPGDirectionalHoverComponent.h
A    Source/RPG_CODEX/RPGDirectionalMovementTest.cpp
A    Source/RPG_CODEX/RPGDirectionalMovementTypes.h
A    Source/RPG_CODEX/RPGEnergyLegBalanceTest.cpp
A    Source/RPG_CODEX/RPGEnergyOrb.cpp
A    Source/RPG_CODEX/RPGEnergyOrb.h
A    Source/RPG_CODEX/RPGEnergySpellComponent.cpp
A    Source/RPG_CODEX/RPGEnergySpellComponent.h
A    Source/RPG_CODEX/RPGEnergySpellTest.cpp
A    Source/RPG_CODEX/RPGEnergySpellTypes.h
M    Source/RPG_CODEX/RPGFlightAnimationComponent.cpp
M    Source/RPG_CODEX/RPGFlightAnimationComponent.h
A    Source/RPG_CODEX/RPGFlightAnimationTypes.h
A    Source/RPG_CODEX/RPGFlightReviewTest.cpp
M    Source/RPG_CODEX/RPGGASPInputBridgeComponent.cpp
M    Source/RPG_CODEX/RPGGASPInputBridgeComponent.h
A    Source/RPG_CODEX/RPGHoverTurnTest.cpp
M    Source/RPG_CODEX/RPGMovementAbilityComponent.cpp
M    Source/RPG_CODEX/RPGMovementAbilityComponent.h
M    Source/RPG_CODEX/RPGPhysicsReactionComponent.cpp
M    Source/RPG_CODEX/RPGPhysicsReactionComponent.h
M    Source/RPG_CODEX/RPGPrototypeAnimationComponent.cpp
A    Tools/GASP/check_directional_assets.py
A    Tools/GASP/review_directional.ps1
M    Tools/ProtoAnim/clips.json
M    Tools/ProtoAnim/clips_uefn.json
A    Tools/ProtoAnim/flight_symmetry.py
A    Tools/ProtoAnim/flightcheck.py
M    Tools/ProtoAnim/poses.py
A    Tools/ProtoAnim/speed_balance_import_report.json
A    Tools/ProtoAnim/speed_balance_import_report_uefn.json
M    Tools/ProtoAnim/ue_import_report.json
M    Tools/ProtoAnim/ue_import_report_uefn.json
A    Tools/Spells/build_energy_materials.py
A    Tools/Spells/review_energy.ps1
```
### Appendix C — exact flight settings and generator diff (ccbbbf8 to handoff working files)
```diff
diff --git a/Source/RPG_CODEX/RPGCameraModeComponent.h b/Source/RPG_CODEX/RPGCameraModeComponent.h
index 5054c69..8c6aaab 100644
--- a/Source/RPG_CODEX/RPGCameraModeComponent.h
+++ b/Source/RPG_CODEX/RPGCameraModeComponent.h
@@ -28,6 +28,7 @@ class RPG_CODEX_API URPGCameraModeComponent : public UActorComponent
 public:
     URPGCameraModeComponent();
     virtual void BeginPlay() override;
+    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
     virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

     /** Camera height above the capsule center in first person, used only if the mesh has no head bone. */
@@ -57,7 +58,19 @@ public:
      * forward, Y right, Z up), so the reticle sits beside the character instead of on its back. Zero to turn off.
      */
     UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Camera")
-    FVector ThirdPersonCombatOffset = FVector(0.f, 45.f, 20.f);
+    FVector ThirdPersonCombatOffset = FVector(0.f, 35.f, 25.f);
+
+    /** Raised shoulder framing even outside combat. Added to the original boom socket offset. */
+    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Camera")
+    FVector ThirdPersonViewOffset = FVector(0.f,25.f,35.f);
+
+    /** Speed presentation only during third-person flight; restored when flight ends. */
+    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Camera|Flight")
+    bool bFlightSpeedCamera = true;
+    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Camera|Flight", meta = (ClampMin = "0", ClampMax = "30"))
+    float FlightBoostFOV = 8.f;
+    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Camera|Flight", meta = (ClampMin = "0"))
+    float FlightBoostDistance = 140.f;

     /** Hidden in first person (with everything below it: the head), so it never sits in the camera. */
     UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Camera|First Person")
@@ -90,6 +103,10 @@ public:
     void ApplyCameraAdjustInput(FVector2D StickInput, float DeltaSeconds);

 private:
+    void UpdateFlightPresentation(float DeltaTime);
+    void ClearFlightPresentation();
+    float FlightArmOffset = 0.f, FlightFOVOffset = 0.f;
+    float CombatCameraWeight = 0.f;
     USpringArmComponent* GetCameraBoom() const;
     class UCameraComponent* GetCamera() const;
     /** Where the first-person camera wants to be this frame, relative to the body (actor space). */
diff --git a/Source/RPG_CODEX/RPGFlightAnimationComponent.h b/Source/RPG_CODEX/RPGFlightAnimationComponent.h
index 573e943..19c0662 100644
--- a/Source/RPG_CODEX/RPGFlightAnimationComponent.h
+++ b/Source/RPG_CODEX/RPGFlightAnimationComponent.h
@@ -2,18 +2,13 @@

 #include "CoreMinimal.h"
 #include "Components/ActorComponent.h"
+#include "RPGFlightAnimationTypes.h"
 #include "RPGFlightAnimationComponent.generated.h"

-class UAnimMontage;
 class UAnimSequence;

-/**
- * Flight poses: while the character is flying, loops hover / forward / fast flight (RPG_Proto_Fly*) on the full body,
- * picked by flight speed (fast flight on L3 always uses the fast pose), cross-fading when it changes. An attack montage
- * takes over the slot while it plays; the flight loop comes back on its own afterwards. Stops when flight ends.
- * The whole body also reacts to the flight (turning the mesh about the hips): pitches up climbing and down diving, leans
- * into speeding up and back when slowing, banks into turns - a little at a hover, fully at speed.
- * Not yet: upper-body-only attacks in the air.
+/** Continuous flight pose blending, aerodynamic body lean and bounded animation-side follow-through.
+ * The ground animation graph and combat remain in control whenever the flight weight is zero.
  */
 UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
 class RPG_CODEX_API URPGFlightAnimationComponent : public UActorComponent
@@ -22,6 +17,8 @@ class RPG_CODEX_API URPGFlightAnimationComponent : public UActorComponent

 public:
     URPGFlightAnimationComponent();
+    virtual void BeginPlay() override;
+    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
     virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

     UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation")
@@ -31,118 +28,184 @@ public:
     UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation")
     TSoftObjectPtr<UAnimSequence> Fast;

-    /** Horizontal flight speed (cm/s) above which the forward pose plays instead of hover... */
+    /** Nominal hover-to-forward speed (cm/s); the continuous blend begins at 60% of this value. */
     UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "0"))
     float ForwardSpeed = 150.f;

-    /** ...and above which the fast pose plays. */
+    /** Boost blend reference speed (cm/s): starts at 80%, fully streamlined at 145%. */
     UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "0"))
     float FastSpeed = 900.f;

     UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "0"))
-    float BlendTime = 0.35f;
+    float BlendTime = 0.45f;
+
+    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation")
+    TSoftObjectPtr<UAnimSequence> Brake;
+    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Authored Clips")
+    TSoftObjectPtr<UAnimSequence> BankL;
+
+    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Authored Clips")
+    TSoftObjectPtr<UAnimSequence> BankR;
+
+    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Authored Clips")
+    TSoftObjectPtr<UAnimSequence> BankFastL;
+
+    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Authored Clips")
+    TSoftObjectPtr<UAnimSequence> BankFastR;
+
+    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Authored Clips")
+    TSoftObjectPtr<UAnimSequence> Takeoff;
+
+    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Authored Clips")
+    TSoftObjectPtr<UAnimSequence> Accelerate;
+
+    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Authored Clips")
+    TSoftObjectPtr<UAnimSequence> BoostClip;
+
+    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Authored Clips")
+    TSoftObjectPtr<UAnimSequence> Release;
+
+
+    /** Speed at which the forward silhouette is fully extended (cm/s). */
+    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "1"))
+    float ForwardFullSpeed = 650.f;
+
+    /** Smoothing of speed/pose changes, seconds. */
+    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "0.05"))
+    float PoseResponseTime = 0.24f;
+
+    /** Time to return to the ground/falling graph when flight is cancelled. */
+    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "0.05"))
+    float ExitBlendTime = 0.9f;
+    /** Let the catch/stride begin before blending it away into the falling graph. */
+    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "0", ClampMax = "0.5"))
+    float ReleasePoseHoldTime = 0.3f;
+    /** Floor contact takes priority over the longer airborne release. */
+    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "0.05"))
+    float GroundBlendOutTime = 0.16f;
+
+    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "0.05"))
+    float ActionBlendOutTime = 0.12f;
+    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "0.01"))
+    float BrakeBlendInTime = 0.13f;
+    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "0.01"))
+    float BrakeBlendOutTime = 0.38f;
+
+    /** Runtime blend diagnostics: hover->forward, boost and braking. */
+    UPROPERTY(VisibleAnywhere, Category = "RPG|Flight Animation")
+    FVector PoseBlend = FVector::ZeroVector;
+    UPROPERTY(VisibleAnywhere, Category = "RPG|Flight Animation")
+    float FlightWeight = 0.f;

     /** Climb / dive: the body pitches by this fraction of the flight path's angle (up to MaxPitch). */
     UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "0"))
     float ClimbPitch = 1.f;
+    /** Share of the flight-path pitch followed during ordinary cruise. */
+    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "0", ClampMax = "1"))
+    float CruisePathPitchScale = 1.f;
+    /** Heading change in degrees per second that reaches the full authored corner pose. */
+    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "1"))
+    float FullTurnPoseRate = 50.f;
     UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "0"))
-    float MaxPitch = 50.f;
+    float MaxPitch = 75.f;
     /** Degrees of lean per 1000 cm/s^2 of acceleration: forward when speeding up, back when slowing, sideways in turns. */
     UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "0"))
-    float AccelLean = 10.f;
+    float AccelLean = 0.f;
     /** Bank into turns: degrees per degree-per-second of turning, at FastSpeed (less when slower), up to MaxBank. */
     UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "0"))
-    float BankPerTurn = 0.6f;
+    float BankPerTurn = 0.8f;
     UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "0"))
-    float MaxBank = 55.f;
+    float MaxBank = 45.f;
     /** How much of all this at a hover (1 at FastSpeed and above). */
     UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "0", ClampMax = "1"))
-    float HoverReaction = 0.5f;
+    float HoverReaction = 1.f;
     /** How quickly the body follows (higher = snappier). */
     UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "0.1"))
-    float ReactionSpeed = 6.f;
-    /** Follow-through while flying: arms, legs and lower back are springs pulled back to the animated pose. Momentum
-     *  swings them off it (they lag when speeding up, swing on when stopping, swing out in turns), drag sweeps them back
-     *  at speed, and the pull back weakens the faster you fly. Head and upper chest are not moved. */
+    float ReactionSpeed = 8.f;
+    /** Small, damped inertia on the upper arms and thighs. Authored clips own steering,
+     *  shoulders and spine; no extra bracing or shoulder tilt is stacked onto them. */
     UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through")
     bool bLimbFlow = true;
     /** Pull back to the pose (higher = stiffer, quicker to return). */
     UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "1"))
-    float FlowStiffness = 60.f;
+    float FlowStiffness = 90.f;
     /** At FastSpeed the pull back is this fraction of FlowStiffness (harder to hold the pose at speed). */
     UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "0.05", ClampMax = "1"))
-    float FlowStiffnessAtSpeed = 0.6f;
+    float FlowStiffnessAtSpeed = 0.8f;
     /** Bounce: lower = more overshoot and swinging back and forth, 1 = settles without overshoot. */
     UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "0.05", ClampMax = "2"))
-    float FlowDamping = 0.55f;
+    float FlowDamping = 1.f;
     /** How hard momentum swings the limbs (per 1000 cm/s^2 of speeding up, slowing or turning). */
     UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "0"))
-    float FlowInertia = 16.f;
+    float FlowInertia = 2.5f;
     /** How hard the air sweeps them back (per 1000 cm/s, growing with speed). */
     UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "0"))
-    float FlowDrag = 2.5f;
+    float FlowDrag = 0.8f;
     /** Largest swing of any joint, degrees. */
     UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "0"))
-    float FlowMaxAngle = 28.f;
+    float FlowMaxAngle = 5.f;
     /** Turning swings the limbs too (they lag behind the body's turn). */
     UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "0"))
-    float FlowTurn = 0.3f;
-    /** Turning at speed: the outer arm braces out from the body (turning left, the right arm), driven by the turn's
-     *  sideways force (speed x how sharply you turn): this many degrees per 1000 cm/s^2 of it, up to FlowBraceMax. */
-    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "0"))
-    float FlowBrace = 22.f;
-    /** Extra bracing that grows with the square of the sideways force: hard, fast corners brace far more. */
-    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "0"))
-    float FlowBraceSharp = 12.f;
-    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "0"))
-    float FlowBraceMax = 80.f;
-    /** A braced arm stiffens: at full brace its pull to the brace is this many times stronger (1 + this), and the forearm
-     *  is held straight, so it barely wobbles. */
-    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "0"))
-    float BraceStiffen = 3.f;
-    /** Wingsuit: in fast, hard corners both arms and both legs spread out from the body (degrees at full spread), by the
-     *  turn's sideways force times speed: full spread at WingsuitForce (1000 cm/s^2 units) at FastSpeed, fading as you slow. */
-    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Steering", meta = (ClampMin = "0"))
-    float WingsuitArms = 55.f;
-    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Steering", meta = (ClampMin = "0"))
-    float WingsuitLegs = 18.f;
-    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Steering", meta = (ClampMin = "0.1"))
-    float WingsuitForce = 2.5f;
-    /** Turning at speed, the upper chest rolls so the inside shoulder drops (turning left, the left shoulder): degrees at
-     *  full turn force (WingsuitForce) at FastSpeed, up to ShoulderDipMax. */
-    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Steering", meta = (ClampMin = "0"))
-    float ShoulderDip = 45.f;
-    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Steering", meta = (ClampMin = "0"))
-    float ShoulderDipMax = 55.f;
-    /** The lower back bends sideways into the turn (the body curves into it), degrees at full turn force at FastSpeed. */
-    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Steering", meta = (ClampMin = "0"))
-    float TorsoArch = 18.f;
-    /** Wingsuit rudder: legs swing to the outside of the turn by the turn's force at speed (degrees at full force). */
-    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Steering", meta = (ClampMin = "0"))
-    float RudderPerForce = 25.f;
-    /** Steering, like a plane's control surfaces (degrees at full effect): the legs swing together to the outside of a
-     *  turn (rudder) per radian-per-second of turning; climbing swings them forward and under, diving lets them trail
-     *  back (elevators) per degree of flight-path angle; braking hard spreads the arms and opens the legs (flaps). */
-    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Steering", meta = (ClampMin = "0"))
-    float RudderPerTurn = 18.f;
-    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Steering", meta = (ClampMin = "0"))
-    float RudderMax = 30.f;
-    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Steering", meta = (ClampMin = "0"))
-    float ElevatorPerDegree = 0.35f;
-    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Steering", meta = (ClampMin = "0"))
-    float ElevatorMax = 20.f;
-    /** Full flaps at this much slowing down (cm/s^2). */
-    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Steering", meta = (ClampMin = "1"))
-    float FlapBrake = 2500.f;
-    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Steering", meta = (ClampMin = "0"))
-    float FlapArms = 30.f;
-    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Steering", meta = (ClampMin = "0"))
-    float FlapLegs = 8.f;
+    float FlowTurn = 0.08f;
+    /** Lower-body follow-through, separate from the authored torso bank and the upper-arm springs. */
+    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Rear Sway", meta = (ClampMin = "0", ClampMax = "0.5"))
+    float RearTurnLag = 0.16f;
+    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Rear Sway", meta = (ClampMin = "0", ClampMax = "15"))
+    float RearSwayMaxAngle = 9.f;
+    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Rear Sway", meta = (ClampMin = "1"))
+    float RearSwayStiffness = 48.f;
+    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Rear Sway", meta = (ClampMin = "0.2", ClampMax = "2"))
+    float RearSwayDamping = 0.85f;
+    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Rear Sway", meta = (ClampMin = "0", ClampMax = "12"))
+    float LegFlowMaxAngle = 8.f;
+    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Rear Sway", meta = (ClampMin = "0", ClampMax = "6"))
+    float RearKneeFlex = 3.5f;
+    // Retained for serialized asset compatibility. Steering is authored in the
+    // mirrored bank/brake clips; these former additive steering settings are inactive.
+    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
+    float FlowBrace = 24.f;
+    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
+    float FlowBraceSharp = 8.f;
+    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
+    float FlowBraceMax = 65.f;
+    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
+    float BraceStiffen = 3.f;
+    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
+    float FlowElbowBrace = 45.f;
+    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
+    float WingsuitArms = 24.f;
+    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
+    float WingsuitLegs = 7.f;
+    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
+    float WingsuitForce = 2.5f;
+    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
+    float ShoulderDip = 22.f;
+    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
+    float ShoulderDipMax = 28.f;
+    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
+    float TorsoArch = 8.f;
+    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
+    float RudderPerForce = 10.f;
+    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
+    float RudderPerTurn = 8.f;
+    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
+    float RudderMax = 18.f;
+    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
+    float ElevatorPerDegree = 0.35f;
+    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
+    float ElevatorMax = 20.f;
+    /** Deceleration that fully engages the authored braking pose, in cm/s squared. */
+    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "251"))
+    float FlapBrake = 2500.f;
+    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
+    float FlapArms = 30.f;
+    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
+    float FlapLegs = 8.f;
     /** The body turns after the view instead of snapping with it (higher = quicker to catch up). */
     UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "0.1"))
     float TurnFollowSpeed = 5.f;

-    /** Hips, legs and arms trail the chest and head on physics while flying (URPGPhysicsReactionComponent::SetFlightFollow). */
+    /** Legacy setting retained for existing assets. Physics follow-through stays disabled; use bLimbFlow. */
     UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction")
     bool bFollowThrough = false;   // off: physics follow-through jittered (2026-10-23); an animation-side follow-through replaces it
     /** The body turns about this height above the feet (the hips). */
@@ -152,21 +215,39 @@ public:
 private:
     /** Body reaction: turns the mesh about the hips; restores it when flight ends. */
     void UpdateBodyReaction(class USkeletalMeshComponent* Mesh, const FVector& Velocity, float DeltaTime);
-    void ResetBodyReaction(class USkeletalMeshComponent* Mesh);
+    void ResetBodyReaction(class USkeletalMeshComponent* Mesh, float DeltaTime = 0.f, bool bGrounded = false, float PoseReleaseAlpha = -1.f);
     void UpdateLimbFlow(class USkeletalMeshComponent* Mesh, class UAnimInstance* Instance, const FVector& Accel, const FVector& Velocity, bool bFlying, float DeltaTime);
+    void ResolveFlightClips(class UAnimInstance* Instance);
+    UPROPERTY(Transient)
+    TArray<TObjectPtr<UAnimSequence>> ResolvedClips;
+    UPROPERTY(Transient)
+    TObjectPtr<class USkeleton> ResolvedSkeleton;
+    void StartFlightAction(int32 ClipIndex);
+    int32 ActionIndex = INDEX_NONE;
+    float ActionTime = 0.f;
+    float ReleaseTravel = 1.f;
+    bool bReleaseReentry = false;
+    float ReleaseReentryBlend = 1.f;
+    bool bAccelerationArmed = true, bBoostArmed = true;
+    float TurnPose = 0.f;
+    float Phase = 0.f;
+    float BrakePoseWeight = 0.f;
+    float StopBraceHold = 0.f;
+    bool bWasFlying = false, bVelocityInitialized = false;
+    FVector FilteredAcceleration = FVector::ZeroVector;
+    float FilteredTurnRate = 0.f;
     TArray<FVector> FlowAngle, FlowSpeed, FlowDir;
+    FVector2D RearAngle = FVector2D::ZeroVector, RearVelocity = FVector2D::ZeroVector;
+    float RearKneeAngle = 0.f, RearKneeVelocity = 0.f;
     float SideForce = 0.f;
     float FlowWeight = 0.f;
     FVector FlowLastVelocity = FVector::ZeroVector;
     bool bReacting = false;
+    bool bTravelHeadingValid = false;
+    bool bVerticalFacingActive = false;
     FVector RestLocation = FVector::ZeroVector;
     FQuat RestRotation = FQuat::Identity;
-    FVector LastVelocity = FVector::ZeroVector;
     float LastHeading = 0.f, Pitch = 0.f, Roll = 0.f;
     float SmoothedYaw = 0.f, TurnRateRad = 0.f;

-    UPROPERTY(Transient)
-    TObjectPtr<UAnimMontage> Playing;
-    UPROPERTY(Transient)
-    TObjectPtr<UAnimSequence> PlayingClip;
 };
diff --git a/Source/RPG_CODEX/RPGMovementAbilityComponent.h b/Source/RPG_CODEX/RPGMovementAbilityComponent.h
index 23433e0..4ef33c3 100644
--- a/Source/RPG_CODEX/RPGMovementAbilityComponent.h
+++ b/Source/RPG_CODEX/RPGMovementAbilityComponent.h
@@ -22,6 +22,7 @@ public:
     URPGMovementAbilityComponent();

     virtual void BeginPlay() override;
+    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
     virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

     // ---------------- Tuning ----------------
@@ -41,6 +42,12 @@ public:
     UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Flight")
     float FlyingBrakingDeceleration = 1500.f;

+    /** Flight-only steering authority. The ground acceleration is restored on exit. */
+    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Flight", meta = (ClampMin = "1"))
+    float FlightAcceleration = 2000.f;
+    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Flight", meta = (ClampMin = "1"))
+    float FastFlightAcceleration = 3600.f;
+
     /** Fly where you look: pushing forward climbs when looking up and dives when looking down. */
     UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Flight")
     bool bFlightFollowsLookPitch = true;
@@ -53,9 +60,12 @@ public:
     UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Flight", meta = (ClampMin = "10", ClampMax = "85"))
     float FlightMaxPitch = 80.f;

-    /** Flight speed with L3 fast flight on. Normal flight uses the movement component's Max Fly Speed. */
+    /** Applied once to the character's normal Max Fly Speed at BeginPlay. Hover keeps its own speed. */
+    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Flight", meta = (ClampMin = "0.1"))
+    float NormalFlightSpeedScale = 1.1f;
+    /** Flight speed with L3 fast flight on. */
     UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Flight")
-    float FastFlightSpeed = 1600.f;
+    float FastFlightSpeed = 2000.f;

     UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dodge")
     float DodgeSpeed = 1400.f;
@@ -91,6 +101,7 @@ public:
     /** While true and flying, the character rises straight up (hold A). */
     UFUNCTION(BlueprintCallable, Category = "RPG|Flight")
     void SetFlightRiseHeld(bool bHeld) { bFlightRiseHeld = bHeld; }
+    bool IsFlightRiseHeld() const { return bFlightRiseHeld; }

     /** L3 while flying. Returns true if fast flight is now on. */
     UFUNCTION(BlueprintCallable, Category = "RPG|Flight")
@@ -118,6 +129,17 @@ private:

     /** Adds the climb/dive and hold-A rise on top of the Left Stick input while flying. */
     void ApplyFlightVerticalInput();
+    void UpdateFlightAcceleration(UCharacterMovementComponent* Movement, bool bFlying);
+    void ConfigureFlightTickOrder(UCharacterMovementComponent* Movement, bool bFlying);
+    void UpdateHoverSpeed(UCharacterMovementComponent* Movement, bool bFlying);
+    void RestoreHoverSpeed(UCharacterMovementComponent* Movement);
+    bool bHoverSpeedOverride = false;
+    float HoverBaseFlySpeed = 0.f, HoverAppliedSpeed = 0.f;
+    TWeakObjectPtr<UActorComponent> FlightPreMovementTick;
+    bool bAddedFlightPrePrerequisite = false;
+    bool bAddedFlightMovementPrerequisite = false;
+    bool bFlightAccelerationOverride = false;
+    float GroundAcceleration = 0.f;

     bool bSprinting = false;
     float WalkSpeedBeforeSprint = 0.f;
diff --git a/Tools/ProtoAnim/flight_symmetry.py b/Tools/ProtoAnim/flight_symmetry.py
new file mode 100644
index 0000000..b5e8d37
--- /dev/null
+++ b/Tools/ProtoAnim/flight_symmetry.py
@@ -0,0 +1,162 @@
+"""Flight-only reflection and level pose construction; never changes the combat authoring rig."""
+import protoanim as p
+
+REFERENCE = p.to_cs(p.REF)
+
+def other_side(bone):
+    if bone.endswith('_l') and bone[:-1]+'r' in p.PARENT:
+        return bone[:-1]+'r'
+    if bone.endswith('_r') and bone[:-1]+'l' in p.PARENT:
+        return bone[:-1]+'l'
+    return bone
+
+def reflect_position(v):
+    return [-v[0],v[1],v[2]]
+
+def reflect_rotation(q):
+    # Reflection in X changes the two other components of an axial rotation.
+    return [q[0],-q[1],-q[2],q[3]]
+
+def mirror_pose(local):
+    cs=p.to_cs(local)
+    desired={}
+    for bone in p.BONES:
+        source=other_side(bone)
+        correction=p.qmul(p.qconj(reflect_rotation(REFERENCE[source][0])),REFERENCE[bone][0])
+        desired[bone]=(p.qnorm(p.qmul(reflect_rotation(cs[source][0]),correction)),reflect_position(cs[source][1]))
+    out={}
+    for bone in p.BONES:
+        parent=p.PARENT[bone];q,t=desired[bone]
+        if parent is None:
+            out[bone]=(q,t)
+        else:
+            pq,pt=desired[parent];inv=p.qconj(pq)
+            out[bone]=(p.qnorm(p.qmul(inv,q)),p.qrot(inv,p.sub(t,pt)))
+    out['root']=([0.,0.,0.,1.],[0.,0.,0.])
+    return out
+
+def level_pose(local):
+    """Remove the standing idle's twist and lateral bias without shortening the limbs."""
+    cs=p.to_cs(local);reflected=p.to_cs(mirror_pose(local))
+    rotations={b:p._nlerp(cs[b][0],reflected[b][0],0.5) for b in p.BONES}
+    out={}
+    for bone in p.BONES:
+        parent=p.PARENT[bone]
+        q=rotations[bone] if parent is None else p.qmul(p.qconj(rotations[parent]),rotations[bone])
+        # Keep the actual skeleton's segment lengths, rather than averaging CS positions.
+        t=list(p.REF[bone][1])
+        if bone=='pelvis':
+            t=list(local[bone][1]);t[0]=0.
+        out[bone]=(p.qnorm(q),t)
+    out['root']=([0.,0.,0.,1.],[0.,0.,0.])
+    return out
+
+def flatten_pose(local):
+    """Put the hip-to-chest axis in the horizontal plane, about the hips."""
+    out={b:(list(q),list(t)) for b,(q,t) in local.items()}
+    cs=p.to_cs(out);direction=p.sub(cs['spine_05'][1],cs['pelvis'][1])
+    import math
+    angle=math.degrees(math.atan2(direction[2],direction[1]))
+    out['pelvis']=(p.qnorm(p.qmul(p.qaxis([1,0,0],-angle),out['pelvis'][0])),out['pelvis'][1])
+    return out
+
+def blend_pose(a,b,u):
+    # Quintic easing has no velocity or acceleration discontinuity at the ends.
+    u=max(0.,min(1.,u));u=u*u*u*(u*(u*6.-15.)+10.)
+    return {bone:(p._nlerp(a[bone][0],b[bone][0],u),p.lerp(a[bone][1],b[bone][1],u)) for bone in p.BONES}
+
+def action_curve(solved,tension=0.7):
+    """Solved rotations with shared key velocities; no IK between flight keys.
+
+    Quintic Hermite segments share key velocities and zero parametric acceleration.
+    Quaternion normalization preserves derivative continuity. Entry/exit
+    velocity is zero, while intermediate poses no longer produce a brief stop.
+    """
+    times=[t for t,_ in solved]
+    tracks={}
+    for bone in p.BONES:
+        rotations=[];translations=[]
+        for _,pose in solved:
+            q=list(pose[bone][0])
+            if rotations and sum(a*b for a,b in zip(rotations[-1],q))<0:
+                q=[-v for v in q]
+            rotations.append(q);translations.append(list(pose[bone][1]))
+        def derivatives(values,quaternion=False):
+            out=[]
+            for i,value in enumerate(values):
+                if i==0 or i==len(values)-1:
+                    out.append([0.]*len(value));continue
+                dt=times[i+1]-times[i-1]
+                v=[(b-a)/dt for a,b in zip(values[i-1],values[i+1])]
+                if quaternion:
+                    radial=sum(a*b for a,b in zip(value,v))
+                    v=[b-a*radial for a,b in zip(value,v)]
+                out.append([tension*a for a in v])
+            return out
+        tracks[bone]=(rotations,translations,derivatives(rotations,True),derivatives(translations))
+    def sample(t):
+        t=max(times[0],min(times[-1],t))
+        i=min(max((j for j,kt in enumerate(times) if kt<=t),default=0),len(times)-2)
+        dt=times[i+1]-times[i];u=(t-times[i])/dt
+        u2=u*u;u3=u2*u;u4=u3*u;u5=u4*u
+        h0=1-10*u3+15*u4-6*u5;h1=1-h0
+        v0=u-6*u3+8*u4-3*u5;v1=-4*u3+7*u4-3*u5
+        out={}
+        for bone,(rotations,translations,rvel,tvel) in tracks.items():
+            def curve(values,velocities):
+                return [h0*a+h1*b+dt*(v0*c+v1*d) for a,b,c,d in zip(values[i],values[i+1],velocities[i],velocities[i+1])]
+            out[bone]=(p.qnorm(curve(rotations,rvel)),curve(translations,tvel))
+        return out
+    return sample
+
+def bank_pose(level,turn):
+    """One canonical right turn; the left turn is its exact skeletal reflection."""
+    local={b:(list(q),list(t)) for b,(q,t) in level.items()}
+    cs=p.to_cs(local)
+    forward=p.norm(p.sub(cs['spine_05'][1],cs['pelvis'][1]))
+    side=p.norm(p.sub(cs['upperarm_l'][1],cs['upperarm_r'][1]))
+    up=p.norm(p.cross(side,forward))
+    def rotate(bone,axis,degrees):
+        q=p.qmul(p.qaxis(axis,degrees),cs[bone][0]);parent=p.PARENT[bone]
+        local[bone]=(p.qnorm(p.qmul(p.qconj(cs[parent][0]),q)),local[bone][1])
+        p.descend(cs,local,parent)
+    # Gentle curvature; the component supplies the whole-body bank. No shoulder shrug.
+    rotate('spine_02',up,-2.5)
+    rotate('spine_05',up,5.)
+    for s,sign in [('l',1),('r',-1)]:
+        upper='upperarm_'+s;elbow='lowerarm_'+s
+        direction=p.norm(p.sub(cs[elbow][1],cs[upper][1]))
+        rotate(upper,p.cross(direction,p.mul(side,sign)),30. if s=='l' else 2.)
+        # Long outside arm, tucked inside elbow, without moving the shoulder pivot.
+        direction=p.norm(p.sub(cs[elbow][1],cs[upper][1]))
+        forearm=p.norm(p.sub(cs['hand_'+s][1],cs[elbow][1]))
+        hinge=p.cross(direction,forearm)
+        if p.length(hinge)>0.001:
+            rotate(elbow,hinge,-15. if s=='l' else 16.)
+        thigh='thigh_'+s;calf='calf_'+s
+        direction=p.norm(p.sub(cs[calf][1],cs[thigh][1]))
+        rotate(thigh,p.cross(direction,side),-4.)
+        direction=p.norm(p.sub(cs[calf][1],cs[thigh][1]))
+        shin=p.norm(p.sub(cs['foot_'+s][1],cs[calf][1]))
+        hinge=p.cross(direction,shin)
+        if p.length(hinge)>0.001:
+            rotate(calf,hinge,6.)
+    return mirror_pose(local) if turn<0 else local
+
+
+def release_stride_pose(local):
+    """Balanced core with the deliberate airborne running gesture retained.
+
+    Neutral flight is mirrored. Release ends in an opposing arm/leg stride;
+    averaging that gesture would pull both feet together before locomotion.
+    """
+    out=level_pose(local)
+    roots={'upperarm_l','upperarm_r','thigh_l','thigh_r'}
+    for bone in p.BONES:
+        ancestor=bone
+        while ancestor:
+            if ancestor in roots:
+                out[bone]=(list(local[bone][0]),list(out[bone][1]))
+                break
+            ancestor=p.PARENT[ancestor]
+    return out
diff --git a/Tools/ProtoAnim/poses.py b/Tools/ProtoAnim/poses.py
index e4a926a..646b975 100644
--- a/Tools/ProtoAnim/poses.py
+++ b/Tools/ProtoAnim/poses.py
@@ -378,98 +378,184 @@ def living_loop(base, T=2.0, scale=1.0):
 COMBAT_STANCE = living_loop(STANCE)
 UNARMED_GUARD = living_loop(GUARD, scale=0.8)   # a held guard is tighter: a little less of everything

-# ---- 7. Flight (2026-10-21): hover, forward and fast loops, 2.0 s each ----
-# Arms relaxed and loose in hover and normal flight (the user's choice: a calm, floating look), streamlined in fast
-# flight. Feet are free (no planted pivot, no balance solver). The head holds level in the world by itself, so as the
-# body tilts forward the eyes stay on where it is flying. The game picks the loop from the flight speed and tilts the
-# whole body further into climbs and dives on top of these.
-FLY_FREE = dict(balance=[0], pivot_l=[0], pivot_r=[0], foot_turn_l=[0], foot_turn_r=[0],
+# ---- Flight: one coordinated family, with a shared four-second phase. ----
+# Targets are rotated WITH the pelvis, rather than left fixed in world space. This keeps
+# elbows long and knees natural as the body streams into horizontal flight.
+FLY_FREE = dict(balance=[0], pivot_l=[0], pivot_r=[0], foot_turn_l=[0], foot_turn_r=[20],
                 guard_follow_l=[0], guard_follow_r=[0], guard_raise_l=[0], guard_raise_r=[0],
-                hand_rel_l=[0], hand_rel_r=[0], fist_roll_l=[0], fist_roll_r=[0], clav_l=[0, 0, 0], clav_r=[0, 0, 0])
-# Hover: nearly upright with a slight lean, right knee raised and bent (foot pointed), left leg hanging with the toes
-# pointed, arms loose and a little out, hands open and relaxed with the palms toward the ground - like floating in place underwater.
-FLY_HOVER = S(**FLY_FREE, pelvis_off=[0, 0, 6], pelvis_rot=[0, -6, 0], spine_lo=[0, 1, 0], spine_hi=[0, 1, 0],
-              head_rot=[0, -2, 0],
-              foot_r=[-12, 8, 32], foot_rot_r=[0, -40, 0], knee_pole_r=[-0.2, 1, 0.2],
-              foot_l=[10, -4, 16], foot_rot_l=[-10, -45, 0], knee_pole_l=[0.1, 1, 0],
-              hand_l=[27, 6, 86], elbow_pole_l=[0.4, -1, 0], hand_r=[-28, 6, 86], elbow_pole_r=[-0.4, -1, 0],
-              hand_open_l=[1.5], hand_open_r=[1.5], wrist_out_l=[15], wrist_out_r=[15])
-# Flight set (2026-10-23), one family: the body angle grows with speed (hover ~6 deg, forward 45, fast 75), the head
-# looks where it is going (head_rot holds the head level in the world, so only a touch of lift), arms lie along the body, legs straight along it (loose: one a little bent), hands open
-# and relaxed. In game the hips, legs and arms also trail the chest on physics (the ribbon follow-through).
-FLY_FORWARD = S(**FLY_FREE, pelvis_off=[0, 4, 4], pelvis_rot=[0, -55, 0], spine_lo=[0, -3, 0], spine_hi=[0, 2, 0],
-                head_rot=[0, 4, 0],
-                foot_r=[-9, -56, 32], foot_rot_r=[0, -60, 0], knee_pole_r=[-0.1, 1, -0.3],
-                foot_l=[9, -50, 38], foot_rot_l=[-6, -55, 0], knee_pole_l=[0.1, 1, -0.3],
-                hand_l=[30, -4, 86], elbow_pole_l=[0.3, -0.7, 0.7], hand_r=[-30, -4, 86], elbow_pole_r=[-0.3, -0.7, 0.7],
-                hand_open_l=[1.3], hand_open_r=[1.3], wrist_out_l=[0], wrist_out_r=[0])
-# Fast: streamlined, nearly horizontal, chin up looking ahead, legs straight and together, arms back along the sides.
-FLY_FAST = S(**FLY_FREE, pelvis_off=[0, 8, 6], pelvis_rot=[0, -85, 0], spine_lo=[0, -3, 0], spine_hi=[0, 4, 0],
-             head_rot=[0, 8, 0],
-             foot_r=[-6, -82, 74], foot_rot_r=[0, -80, 0], knee_pole_r=[0, 0.3, -1],
-             foot_l=[6, -80, 78], foot_rot_l=[0, -80, 0], knee_pole_l=[0, 0.3, -1],
-             hand_l=[22, -6, 92], elbow_pole_l=[0.3, -0.3, 1], hand_r=[-22, -6, 92], elbow_pole_r=[-0.3, -0.3, 1],
-             hand_open_l=[1.2], hand_open_r=[1.2])
-
-def flight_loop(base, bob, sway, T=2.0):
-    """A floating loop: the living stance's breath and hand drift, plus a slow bob of the whole body and a little
-    swing in the free legs (whole cycles, so it wraps seamlessly)."""
-    keys = living_loop(base, T, scale=0.6)
-    out = []
-    for t, d, ease, lags in keys:
-        u = 2 * math.pi * t / T
-        d = dict(d)
-        d['pelvis_off'] = [d['pelvis_off'][0], d['pelvis_off'][1], d['pelvis_off'][2] + bob * math.sin(u)]
-        d['foot_l'] = [d['foot_l'][0], d['foot_l'][1] + sway * math.sin(u + 0.6), d['foot_l'][2] + bob * math.sin(u) + 0.5 * sway * math.sin(2 * u)]
-        d['foot_r'] = [d['foot_r'][0], d['foot_r'][1] + sway * math.sin(u + 2.2), d['foot_r'][2] + bob * math.sin(u) + 0.5 * sway * math.sin(2 * u + 1)]
-        out.append((t, d, ease, lags))
-    return out
-def hover_loop(base, T=4.0, keys_n=64, amp=1.0, legs=(4.5, 2.5)):
-    """Natural floating (2026-10-21): several gentle motions at different rhythms instead of one bob, and the free
-    parts lagging behind the body. All whole cycles of T, so the loop wraps seamlessly.
-      float     rise and sink 2.5 cm, once per loop
-      sway      the body drifts forward / back 3 cm and the torso tips +-3 deg with it (a quarter cycle after the float)
-      roll      a slight side-to-side lean, twice per loop, so it never reads as a simple pendulum
-      legs      swing after the body (dragged, hanging): opposite to the sway and later in phase, the dangling leg more
-      arms      drift after the shoulders a beat late (follow-through)
-    plus the living stance's breath, and (2026-10-23) each hand and foot drifting on its own slow rhythm and the fingers
-    opening and closing a touch, so nothing is ever still. amp scales it all (forward / fast flight use the same loop,
-    smaller and tighter)."""
-    breath = living_loop(base, T, scale=0.5)
-    out = []
-    for i in range(keys_n + 1):
-        t = T * i / keys_n
-        u = 2 * math.pi * t / T
-        d = dict(sample(breath, t, GROUPS))
-        rise = 3.0 * amp * math.sin(u)
-        fwd = 3.0 * amp * math.sin(u + math.pi / 2)
-        tip = 3.0 * amp * math.sin(u + math.pi / 2 - 0.35)          # the torso tips a touch after the drift starts
-        roll = 1.2 * amp * math.sin(2 * u + 0.8)
-        po, pr = d['pelvis_off'], d['pelvis_rot']
-        d['pelvis_off'] = [po[0] + 0.8 * amp * math.sin(2 * u + 0.8), po[1] + fwd, po[2] + rise]
-        d['pelvis_rot'] = [pr[0], pr[1] - tip, pr[2] + roll]
-        sh = d['spine_hi']
-        d['spine_hi'] = [sh[0], sh[1] - 0.5 * tip, sh[2] - 0.6 * roll]   # the chest settles back a little over the hips
-        # legs: dragged behind the body's drift (opposite and late), the hanging left leg swinging more
-        lag = u + math.pi / 2 - 1.1
-        for side, amt, ph in (('l', legs[0], 1.4), ('r', legs[1], 0.3)):
-            f = d['foot_' + side]
-            d['foot_' + side] = [f[0] - 0.4 * roll + 0.7 * amp * math.sin(3 * u + ph), f[1] - amt * math.sin(lag),
-                                 f[2] + rise + 0.8 * amp * math.sin(2 * u + ph) + 0.5 * amp * math.sin(3 * u + 2 * ph)]
-        # arms: follow the shoulders a beat late
-        alag = u + math.pi / 2 - 0.7
-        for side, s in (('l', 1), ('r', -1)):
-            h = d['hand_' + side]
-            ph = 0.0 if side == 'l' else 2.1
-            d['hand_' + side] = [h[0] + s * amp * (0.8 * math.sin(2 * u + 1.9) + 0.6 * math.sin(3 * u + ph)), h[1] - 2.0 * amp * math.sin(alag) + 0.6 * amp * math.sin(3 * u + ph + 1),
-                                 h[2] + rise + amp * (1.2 * math.sin(alag + 0.5) + 0.5 * math.sin(3 * u + ph + 2))]
-            if 'hand_open_' + side in d:   # fingers ease open and closed a touch
-                d['hand_open_' + side] = [d['hand_open_' + side][0] + 0.15 * amp * math.sin(2 * u + ph)]
-        out.append((t, d, 'smooth' if i in (0, keys_n) else 'flow', {}))
+                hand_rel_l=[0], hand_rel_r=[0], fist_roll_l=[0], fist_roll_r=[0],
+                clav_l=[0, 0, -2], clav_r=[0, 0, 2], reach_l=[0.985], reach_r=[0.985],
+                wrist_out_l=[4], wrist_out_r=[4], hand_open_l=[1.0], hand_open_r=[1.0])
+
+def flight_pose(tilt, hands, feet, knees, offset=(0, 0, 5), head=2):
+    # Author in Manny's coordinates; protoanim maps the finished targets to UEFN.
+    pivot = [0, 0, 91]
+    rot = _pa.qeuler(0, -tilt, 0)
+    def point(v):
+        return _pa.add(_pa.add(pivot, _pa.qrot(rot, _pa.sub(v, pivot))), list(offset))
+    pose = S(**FLY_FREE, pelvis_off=list(offset), pelvis_rot=[0, -tilt, 0],
+             spine_lo=[0, -1, 0], spine_hi=[0, 3, 0], head_rot=[0, head, 0],
+             hand_l=point(hands[0]), hand_r=point(hands[1]),
+             elbow_pole_l=[0.65, -0.75, -0.1], elbow_pole_r=[-0.65, -0.75, -0.1],
+             foot_l=point(feet[0]), foot_r=point(feet[1]),
+             foot_rot_l=[0, -tilt-20, 0], foot_rot_r=[0, -tilt-20, 0],
+             knee_pole_l=knees[0], knee_pole_r=knees[1])
+    # At forward/boost angles, anchor targets to the actual rotated hip/shoulder.
+    # Static retarget offsets around the idle stance otherwise shorten the legs in flight.
+    if tilt > 20:
+        cs=_pa.to_cs(_pa.build_pose(pose))
+        for side,sgn in [('l',1),('r',-1)]:
+            leg=sum(_pa.length(_pa.IDLE[b+'_'+side][1]) for b in ['calf','foot'])
+            arm=sum(_pa.length(_pa.IDLE[b+'_'+side][1]) for b in ['lowerarm','hand'])
+            legvec=[sgn*(2 if tilt>80 else 4), -1, -leg*(1.025 if side=='l' else 1.005)]
+            armvec=[sgn*(9 if tilt>80 else 19), -5, -arm*0.96]
+            ft=_pa.add(cs['thigh_'+side][1],_pa.qrot(rot,legvec))
+            ht=_pa.add(cs['upperarm_'+side][1],_pa.qrot(rot,armvec))
+            if _pa._MAP:
+                m=_pa._MAP[side]
+                ft=_pa.add(m['hip_m'],_pa.mul(_pa.sub(ft,m['hip']),1/_pa.LEG_K))
+                ht=_pa.add(m['sh_m'],_pa.mul(_pa.sub(ht,m['sh']),1/_pa.ARM_K))
+            pose['foot_'+side]=ft;pose['hand_'+side]=ht
+    # Cancel the idle stance's toe-out BEFORE pitching the feet with the body.
+    # Boost toes trail along the shin rather than forming a right-angle air brake.
+    for side in ['l','r']:
+        toe=_pa.sub(_pa.IDLE_CS['ball_'+side][1],_pa.IDLE_CS['foot_'+side][1])
+        toe_yaw=math.degrees(math.atan2(-toe[0],toe[1]))
+        pose['foot_turn_'+side]=[-toe_yaw-_pa.FOOT_BASE_TURN[side]]
+        pose['foot_rot_'+side]=[0,-tilt-(60 if tilt>20 else 30),0]
+    return pose
+
+# Flight starts horizontal, with a level shoulder line and paired limbs.
+FLY_HOVER = flight_pose(89, ([49, 4, 102], [-48, 3, 104]),
+                       ([12, -3, 13], [-12, 8, 28]), ([0.15, 1, 0], [-0.15, 1, 0]))
+# The chest leads, arms sweep back with room for the shoulder/turn reactions.
+FLY_FORWARD = flight_pose(89, ([36, -5, 91], [-35, -6, 93]),
+                         ([11, -4, 11], [-10, -1, 16]), ([0.12, 0.6, -0.8], [-0.12, 0.6, -0.8]), head=5)
+# Narrow boost silhouette with relaxed trailing knees; both sides stay paired.
+FLY_FAST = flight_pose(89, ([27, -7, 87], [-27, -7, 89]),
+                      ([7, -3, 9], [-7, -1, 12]), ([0.1, 0.15, -1], [-0.1, 0.15, -1]), head=8)
+# Air brake / return to hover: broad arms, feet come under the hips before settling.
+FLY_BRAKE = flight_pose(62, ([55, 14, 110], [-55, 14, 110]),
+                       ([17, 14, 25], [-16, 10, 33]), ([0.2, 1, 0], [-0.2, 1, 0]), head=-2)
+
+def hover_loop(base, T=4.0, keys_n=64, amp=1.0, legs=(2.5, 1.8)):
+    """Periodic breathing and drag: the hips lead, hands/feet follow with different phases.
+    All clips share a phase. No running/kicking cycle is added to forward flight."""
+    out=[]
+    for i in range(keys_n+1):
+        t=T*i/keys_n; u=2*math.pi*t/T
+        d={k:list(v) for k,v in base.items()}
+        rise=1.6*amp*math.sin(u)
+        sway=0.8*amp*math.sin(u+0.6)
+        d['pelvis_off'][0] += 0.45*amp*math.sin(2*u)
+        d['pelvis_off'][1] += sway
+        d['pelvis_off'][2] += rise
+        d['pelvis_rot'][1] += 1.0*amp*math.sin(u-0.3)
+        d['pelvis_rot'][2] += 0.65*amp*math.sin(2*u+0.4)
+        d['spine_hi'][1] += 0.65*amp*math.sin(u-0.55)
+        for side,sgn,ph,leg in [('l',1,0.0,legs[0]),('r',-1,1.1,legs[1])]:
+            h=d['hand_'+side]; f=d['foot_'+side]
+            h[0] += sgn*0.7*amp*math.sin(u-0.8+ph)
+            h[1] += sway-0.8*amp*math.sin(u-0.7+ph)
+            h[2] += rise+0.6*amp*math.sin(2*u+ph)
+            f[0] += sgn*0.4*amp*math.sin(u-1.0+ph)
+            f[1] += sway-leg*math.sin(u-1.0+ph)
+            f[2] += rise+0.5*amp*math.sin(2*u+ph)
+            d['hand_open_'+side][0] += 0.06*amp*math.sin(u+ph)
+        out.append((t,d,'flow',{}))
     return out
-FLIGHT_HOVER = hover_loop(FLY_HOVER)
-FLIGHT_FORWARD = hover_loop(FLY_FORWARD, T=3.0, keys_n=48, amp=0.6, legs=(3.0, 3.5))
-FLIGHT_FAST = hover_loop(FLY_FAST, T=2.0, keys_n=32, amp=0.3, legs=(1.5, 1.5))
+FLIGHT_HOVER=hover_loop(FLY_HOVER)
+FLIGHT_FORWARD=hover_loop(FLY_FORWARD,amp=0.4,legs=(0.8,1.0))
+FLIGHT_FAST=hover_loop(FLY_FAST,amp=0.18,legs=(0.35,0.45))
+FLIGHT_BRAKE=hover_loop(FLY_BRAKE,amp=0.45,legs=(1.0,1.2))
+
+# ---- Second pass: authored turns and one-shot flight transitions. ----
+def flight_target(pose, side, hand=None, foot=None):
+    """Targets relative to the actual rotated shoulder/hip, with inverse authoring mapping."""
+    cs=_pa.build_pose(pose,return_cs=True)
+    rot=_pa.qeuler(*pose['pelvis_rot'])
+    for channel,offset,bone,scale,key in [('hand',hand,'upperarm',_pa.ARM_K,'sh'),('foot',foot,'thigh',_pa.LEG_K,'hip')]:
+        if offset is None:continue
+        target=_pa.add(cs[bone+'_'+side][1],_pa.qrot(rot,offset))
+        if _pa._MAP:
+            m=_pa._MAP[side];target=_pa.add(m[key+'_m'],_pa.mul(_pa.sub(target,m[key]),1/scale))
+        pose[channel+'_'+side]=target
+    return pose
+
+# These targets are relative to the shoulders/hips, so neither side inherits
+# the standing idle's arm/leg stagger. The final bake removes its torso twist.
+for base,spread,extension in [(FLY_HOVER,0.43,0.86),(FLY_FORWARD,0.33,0.91),(FLY_FAST,0.18,0.98)]:
+    # Boost keeps a soft knee but streams the feet closer to the body line.
+    # This also supplies matching keys to boost entry and flight release.
+    leg_extension,leg_lift=(0.96,0.08) if base is FLY_FAST else (0.97,0.02)
+    for side,sign in [('l',1),('r',-1)]:
+        arm=sum(_pa.length(_pa.IDLE[b+'_'+side][1]) for b in ['lowerarm','hand'])
+        leg=sum(_pa.length(_pa.IDLE[b+'_'+side][1]) for b in ['calf','foot'])
+        flight_target(base,side,[sign*arm*spread,arm*0.04,-arm*extension],
+                      [sign*leg*0.07,-leg*leg_lift,-leg*leg_extension])
+        base['knee_pole_'+side]=_pa.qrot(_pa.qeuler(*base['pelvis_rot']),[sign*0.12,1,0])
+FLIGHT_HOVER=hover_loop(FLY_HOVER,amp=0.45,legs=(0.7,0.7))
+FLIGHT_FORWARD=hover_loop(FLY_FORWARD,amp=0.25,legs=(0.4,0.4))
+FLIGHT_FAST=hover_loop(FLY_FAST,amp=0.12,legs=(0.2,0.2))
+
+# Both directions sample the same canonical loop, then the baked right turn is
+# reflected through the skeleton for the left turn. They cannot drift apart.
+FLIGHT_BANK_L=FLIGHT_BANK_R=hover_loop(FLY_FORWARD,amp=0.20,legs=(0.3,0.3))
+FLIGHT_BANK_FAST_L=FLIGHT_BANK_FAST_R=hover_loop(FLY_FAST,amp=0.12,legs=(0.2,0.2))
+
+# The global entry blend still comes from locomotion, but every authored flight
+# entry key is flat. Acceleration changes limb extension rather than rolling a shoulder.
+F_LIFT_LOAD=with_(FLY_HOVER,pelvis_off=[0,0,3])
+F_LIFT_PRESS=with_(FLY_HOVER,pelvis_off=[0,0,7],spine_hi=[0,4,0])
+FLIGHT_TAKEOFF=[(0,F_LIFT_LOAD,'smooth',{}),(0.32,F_LIFT_PRESS,'flow',{}),
+                (0.65,FLY_HOVER,'flow',{}),(1.0,FLY_HOVER,'smooth',{})]
+F_DRIVE_READY=with_(FLY_HOVER,spine_hi=[0,2,0])
+F_DRIVE_OVER=with_(FLY_FORWARD)
+FLIGHT_ACCELERATE=[(0,FLY_HOVER,'smooth',{}),(0.14,F_DRIVE_READY,'flow',{}),
+                  (0.48,F_DRIVE_OVER,'flow',{'leg_l':0.05,'leg_r':0.05,'arm_l':0.035,'arm_r':0.035}),
+                  (0.90,FLY_FORWARD,'smooth',{})]
+F_BOOST_LOAD=with_(FLY_FORWARD)
+F_BOOST_OVER=with_(FLY_FAST)
+FLIGHT_BOOST=[(0,FLY_FORWARD,'smooth',{}),(0.18,F_BOOST_LOAD,'flow',{}),
+              (0.48,F_BOOST_OVER,'flow',{'arm_l':0.035,'arm_r':0.035,'leg_l':0.07,'leg_r':0.07}),
+              (0.90,FLY_FAST,'smooth',{})]
+F_RELEASE_CATCH=flight_pose(65,([40,-3,93],[-39,-3,94]),([12,-2,18],[-12,0,23]),([0.15,0.4,-0.8],[-0.15,0.4,-0.8]),head=4)
+for side,sgn in [('l',1),('r',-1)]:
+    leg=sum(_pa.length(_pa.IDLE[b+'_'+side][1]) for b in ['calf','foot'])
+    arm=sum(_pa.length(_pa.IDLE[b+'_'+side][1]) for b in ['lowerarm','hand'])
+    flight_target(F_RELEASE_CATCH,side,[sgn*arm*0.55,arm*0.1,-arm*0.55],[sgn*8,leg*0.12,-leg*0.83])
+F_RELEASE_GATHER=flight_pose(18,([48,17,115],[-47,14,114]),([16,19,32],[-16,8,40]),([0.2,1,0],[-0.2,1,0]),head=0)
+F_RELEASE_STRIDE=with_(FLY_HOVER,pelvis_rot=[0,-9,0],spine_lo=[0,-3,0],spine_hi=[0,-2,0],
+                       hand_l=[48,8,108],hand_r=[-30,0,96],elbow_pole_l=[1,-0.25,0],elbow_pole_r=[-0.6,-0.7,0],
+                       foot_l=[13,18,39],foot_r=[-12,-18,18],foot_rot_l=[0,-28,0],foot_rot_r=[0,-46,0])
+# The stride is upright; do not inherit the horizontal hover's knee guides.
+for side,sign in [('l',1),('r',-1)]:
+    F_RELEASE_STRIDE['knee_pole_'+side]=_pa.qrot(_pa.qeuler(*F_RELEASE_STRIDE['pelvis_rot']),[sign*0.12,1,0])
+FLIGHT_RELEASE=[(0,FLY_FAST,'smooth',{}),(0.22,F_RELEASE_CATCH,'flow',{'leg_l':0.05,'leg_r':0.07}),
+                (0.48,F_RELEASE_GATHER,'flow',{'leg_l':0.04,'leg_r':0.06}),
+                (0.75,F_RELEASE_STRIDE,'smooth',{'leg_l':0,'leg_r':0})]
+
+# Stationary hover and the momentum catch are separate from the approved prone
+# flight family above. Build these after its action keys so Forward/Fast/banks,
+# Boost and Release retain exactly the same authored transforms.
+FLY_HOVER_IDLE=flight_pose(2,([42,6,104],[-42,6,104]),
+                          ([12,0,16],[-12,0,16]),([0.12,1,0],[-0.12,1,0]),head=0)
+FLY_STOP_BRACE=flight_pose(-10,([48,24,118],[-48,24,118]),
+                          ([14,22,32],[-14,22,32]),([0.12,1,0],[-0.12,1,0]),head=0)
+for base,hand_offset,foot_offset in [
+        (FLY_HOVER_IDLE,(0.32,0.04,-0.89),(0.08,0.02,-0.94)),
+        (FLY_STOP_BRACE,(0.55,0.35,-0.38),(0.10,0.28,-0.80))]:
+    for side,sign in [('l',1),('r',-1)]:
+        arm=sum(_pa.length(_pa.IDLE[b+'_'+side][1]) for b in ['lowerarm','hand'])
+        leg=sum(_pa.length(_pa.IDLE[b+'_'+side][1]) for b in ['calf','foot'])
+        flight_target(base,side,[sign*arm*hand_offset[0],arm*hand_offset[1],arm*hand_offset[2]],
+                      [sign*leg*foot_offset[0],leg*foot_offset[1],leg*foot_offset[2]])
+        base['knee_pole_'+side]=_pa.qrot(_pa.qeuler(*base['pelvis_rot']),[sign*0.12,1,0])
+FLIGHT_HOVER=hover_loop(FLY_HOVER_IDLE,amp=0.65,legs=(0.65,0.65))
+FLIGHT_BRAKE=hover_loop(FLY_STOP_BRACE,amp=0.35,legs=(0.5,0.5))
+FLIGHT_TAKEOFF=[(0,with_(FLY_HOVER_IDLE,pelvis_off=[0,0,3]),'smooth',{}),
+                (0.32,with_(FLY_HOVER_IDLE,pelvis_off=[0,0,7]),'flow',{}),
+                (0.65,FLY_HOVER_IDLE,'flow',{}),(1.0,FLY_HOVER_IDLE,'smooth',{})]

 # Both punches play 10% faster than their authored timing (key times and lags scale together).
 PUNCH_SPEED = 1.1
@@ -659,12 +745,51 @@ SPECS = {
     'RPG_Proto_FlyHover': (FLIGHT_HOVER, None, None, ()),
     'RPG_Proto_FlyForward': (FLIGHT_FORWARD, None, None, ()),
     'RPG_Proto_FlyFast': (FLIGHT_FAST, None, None, ()),
+    'RPG_Proto_FlyBrake': (FLIGHT_BRAKE, None, None, ()),
+    'RPG_Proto_FlyBankL': (FLIGHT_BANK_L,None,None,()),
+    'RPG_Proto_FlyBankR': (FLIGHT_BANK_R,None,None,()),
+    'RPG_Proto_FlyBankFastL': (FLIGHT_BANK_FAST_L,None,None,()),
+    'RPG_Proto_FlyBankFastR': (FLIGHT_BANK_FAST_R,None,None,()),
+    'RPG_Proto_FlyTakeoff': (FLIGHT_TAKEOFF,None,None,()),
+    'RPG_Proto_FlyAccelerate': (FLIGHT_ACCELERATE,None,None,()),
+    'RPG_Proto_FlyBoost': (FLIGHT_BOOST,None,None,()),
+    'RPG_Proto_FlyRelease': (FLIGHT_RELEASE,None,None,()),
 }
 CLIPS = [(name, spec[0], spec[1]) for name, spec in SPECS.items()]

 def bake_spec(name):
     keys, impact, springs, pins = SPECS[name]
-    clip = bake(name, keys, GROUPS, springs, pins); clip['impact'] = impact
+    if name.startswith('RPG_Proto_Fly') and name not in {'RPG_Proto_FlyTakeoff','RPG_Proto_FlyAccelerate','RPG_Proto_FlyBoost','RPG_Proto_FlyRelease'}:
+        # Sample using neighbours across the seam, so velocity is continuous too.
+        T=keys[-1][0]
+        periodic=(keys[:-1] + [(k[0]+T,*k[1:]) for k in keys]
+                  + [(k[0]+2*T,*k[1:]) for k in keys[1:]])
+        frames=[_pa.build_pose(sample(periodic,T+i/FPS,GROUPS)) for i in range(round(T*FPS)+1)]
+        clip=dict(name=name,fps=FPS,frames=len(frames),bones=_pa.BONES,poses=frames)
+    else:
+        clip = bake(name, keys, GROUPS, springs, pins)
+    if name.startswith('RPG_Proto_Fly'):
+        from flight_symmetry import level_pose, release_stride_pose, flatten_pose, action_curve, bank_pose as balanced_bank
+        # Remove the idle's pre-existing chest twist in every flight frame.
+        clip['poses']=[level_pose(frame) for frame in clip['poses']]
+        actions={'RPG_Proto_FlyTakeoff','RPG_Proto_FlyAccelerate','RPG_Proto_FlyBoost','RPG_Proto_FlyRelease'}
+        if name in actions:
+            # Interpolate the solved anatomical poses, rather than crossing an IK
+            # straight-leg singularity between nearly identical flat targets.
+            solved=[]
+            for i,key in enumerate(keys):
+                frame=_pa.build_pose(key[1])
+                # The final release key is a running stride, not a neutral loop.
+                balance=release_stride_pose if name=='RPG_Proto_FlyRelease' and i==len(keys)-1 else level_pose
+                solved.append((key[0],balance(frame)))
+            curve=action_curve(solved)
+            clip['poses']=[curve(i/FPS) for i in range(clip['frames'])]
+        if name not in {'RPG_Proto_FlyHover','RPG_Proto_FlyTakeoff','RPG_Proto_FlyBrake','RPG_Proto_FlyRelease'}:
+            clip['poses']=[flatten_pose(frame) for frame in clip['poses']]
+        if 'FlyBank' in name:
+            turn=-1 if name.endswith('L') else 1
+            clip['poses']=[balanced_bank(frame,turn) for frame in clip['poses']]
+    clip['impact'] = impact
     clip['keys'] = [k[0] for k in keys]
     return clip

```
### Appendix D — exact uncommitted text changes (binary materials listed in A)
```diff
diff --git a/Docs/ENERGY_SPELL_2026-10-24.md b/Docs/ENERGY_SPELL_2026-10-24.md
index 6a8f7d0..9a8eb1d 100644
--- a/Docs/ENERGY_SPELL_2026-10-24.md
+++ b/Docs/ENERGY_SPELL_2026-10-24.md
@@ -2,11 +2,11 @@

 Controls remain independent of melee: **LB = left spell; RB = right spell**. Hold to charge and release to fire. RT/LT punches and dual-trigger guard are retained. Both spell hands can charge together and release separately. Spells are equipped automatically; no menu or paid assets are required.

-Default orb radius is 5 cm (10 cm diameter), growing linearly to 10 cm at 1.5 seconds. Charge caps and holds without auto-firing. A single projectile leaves the palm after the short release extension. A brief energy burst expands to twice the incoming orb diameter; it is visual feedback only, with no radial damage. Travel is swept, including the initial palm-to-muzzle sweep, so nearby walls and fast projectiles cannot skip contact. The source actor is ignored. Character movement capsules are skipped while their skeletal collision remains eligible.
+Default orb radius is 15 cm (30 cm diameter), growing linearly to 30 cm at 1.5 seconds. Both sizes are 50% larger than the accepted 10/20 cm radius baseline. Charge caps and holds without auto-firing. A single projectile leaves the palm after the short release extension. A brief energy burst expands to twice the incoming orb diameter; it is visual feedback only, with no radial damage. Travel is swept, including the initial palm-to-muzzle sweep, so nearby walls and fast projectiles cannot skip contact. The source actor is ignored. Character movement capsules are skipped while their skeletal collision remains eligible.

-The original effect uses three authored materials, rotating energy filaments, a glow shell, sparks, a short trail. All files live in `/Game/RPG/Spells/Energy`. These are native mesh/material effects, with no external VFX pack dependency. Rebuild materials with `Tools/Spells/build_energy_materials.py` in Unreal's Python commandlet.
+The original effect uses four authored materials, rotating energy filaments, a pulsing glow shell/core, sparks and a short trail. Three pooled branching static-electricity clusters independently flicker at changing locations around the sphere only while a hand remains held at full charge. They clear immediately when the button is released, including during the release-pose delay, and never travel with the projectile or appear at impact. The sphere breathes in size and subtly changes shape while its emissive brightness pulses. All files live in `/Game/RPG/Spells/Energy`. These are native mesh/material effects, with no external VFX pack dependency. Rebuild materials with `Tools/Spells/build_energy_materials.py` in Unreal's Python commandlet.

-`URPGEnergySpellComponent` owns separate charge/release/recovery states, aim tracing and tunable settings. `URPGControllerInputComponent` adds it at runtime alongside the existing helpers. It subscribes to the existing spell events. Main tuning properties include FullChargeSeconds, BaseRadius, FullSizeMultiplier, Color, ProjectileSpeed, Range, TapDamage, ChargedDamage, TapImpulse and ChargedImpulse. Defaults: 22 m/s, 60 m range, 6–28 damage, 1200–5000 kg cm/s before the local body mass cap.
+`URPGEnergySpellComponent` owns separate charge/release/recovery states, aim tracing and tunable settings. `URPGControllerInputComponent` adds it at runtime alongside the existing helpers. It subscribes to the existing spell events. Main tuning properties include FullChargeSeconds, BaseRadius, FullSizeMultiplier, Color, ProjectileSpeed, Range, TapDamage, ChargedDamage, TapImpulse and ChargedImpulse. Defaults: 33 m/s, 60 m range, 6–28 damage, 1200–5000 kg cm/s before the local body mass cap.

 `URPGAnimInstance` applies a spell-only pose after the established flight/aim layers. The arms use two-bone IK, down/out elbow poles and preserved bone lengths. The torso shares yaw and pitch within limits. Charge shortens reach and adds a small tremor; release extends predominantly at the elbow, followed by recoil and a soft fade. Visible mouse cursor deprojection is used when available; normal locked-cursor play uses the actual camera ray at the reticle. The character turns smoothly toward the target, with each hand limited to a 12-degree forward half-cone. A released shot waits for body alignment and a raised arm before the elbow extension; an unreachable release cancels after two seconds. During hover, pitch adds a small whole-body lean and yaw turns add a brief bank, without translating the capsule. The spell pose corrects any retained GASP idle root yaw before solving the arms, so a sideways idle cannot make the far arm cross the chest. Casting is supported on the ground or during slow hover. Melee, guard, root-motion traversal, death, loss of possession and travel flight cancel a held spell. The pose has no effect at zero weight. Flight animation assets/settings are preserved.

@@ -14,7 +14,7 @@ The original effect uses three authored materials, rotating energy filaments, a

 Verification and captures are written under `Saved/SpellReview`; logs under `Saved/Logs/Spell-*`. Automated cases are `RPG.Spells.Energy` and `Visual.EnergySpell`. They create a temporary PIE floor/target/camera and never save the map. The visual test includes charge, tap, both hands, hover, punch preservation and localized impacts.

-Before editing, existing flight/animation/controller/dummy sources were backed up in `Saved/Spell_Before_20261024`. Existing flight work remains uncommitted; no reset or stash was performed.
+The accepted game before this size/reach/static experiment is committed and pushed to GitHub as `7974c0a1c508795367ca6a4acbc0847468f51a3f`, tagged `checkpoint/pre-spell-aura-2026-10-24`. Experimental spell changes remain separate in the working tree. No reset or stash was performed.

 The spell material directory is explicitly included for cooking in DefaultGame.ini. A full packaged build has not been run. The temporary moving point light from the first candidate triggered a UE5.8 GPU Scene light-data ensure and was removed; the accepted effect uses emissive materials.

diff --git a/Source/RPG_CODEX/RPGAnimInstance.cpp b/Source/RPG_CODEX/RPGAnimInstance.cpp
index b022f5a..e109a63 100644
--- a/Source/RPG_CODEX/RPGAnimInstance.cpp
+++ b/Source/RPG_CODEX/RPGAnimInstance.cpp
@@ -295,12 +295,12 @@ void FRPGAnimInstanceProxy::ApplySpellPose(FPoseContext& Output) const
         const FTransform RootCS = BoneComponentTransform(Pose,Root),MidCS = BoneComponentTransform(Pose,Mid),EndCS = BoneComponentTransform(Pose,End);
         const FVector R = RootCS.GetLocation(),M = MidCS.GetLocation(),E = EndCS.GetLocation();
         const float Length = FVector::Distance(R,M)+FVector::Distance(M,E);
-        const float Reach = FMath::Clamp(0.965f-0.065f*S.Charge+S.Extension*(0.015f+0.065f*S.Charge),0.89f,0.98f);
+        const float Reach = FMath::Clamp(0.978f-0.028f*S.Charge+S.Extension*(0.009f+0.028f*S.Charge),0.95f,0.987f);
         const FVector Out = S.RightCS*(H==0?-1.f:1.f),Dir = S.DirectionCS.GetSafeNormal();
         const float Tremor = (1.f-S.Extension)*S.Charge*S.Charge*0.55f;
         const FVector Shake = S.UpCS*FMath::Sin(S.Time*49.f+H*1.1f)*Tremor + Out*FMath::Sin(S.Time*63.f)*Tremor*0.5f;
-        const FVector Target = R+Dir*(Length*Reach)-S.UpCS*4.f+Out*2.f+Shake;
-        const FVector Pole = R-S.UpCS*Length*0.85f+Out*Length*0.45f+Dir*Length*0.2f;
+        const FVector Target = R+Dir*(Length*Reach)-S.UpCS*1.5f+Out+Shake;
+        const FVector Pole = R-S.UpCS*Length*0.85f+Out*Length*0.3f+Dir*Length*0.2f;
         FVector NewMid,NewEnd;
         AnimationCore::SolveTwoBoneIK(R,M,E,Pole,Target,NewMid,NewEnd,false,1.0,1.0);
         const FQuat Q1 = FQuat::FindBetweenNormals((M-R).GetSafeNormal(),(NewMid-R).GetSafeNormal());
diff --git a/Source/RPG_CODEX/RPGEnergyLegBalanceTest.cpp b/Source/RPG_CODEX/RPGEnergyLegBalanceTest.cpp
index eced224..e18b361 100644
--- a/Source/RPG_CODEX/RPGEnergyLegBalanceTest.cpp
+++ b/Source/RPG_CODEX/RPGEnergyLegBalanceTest.cpp
@@ -76,7 +76,13 @@ public:
         if(Local>=.7f && !bShot)
         {
             const bool Left=Case<2;const float Charge=Case%2?1.f:0.f;
-            const FVector Point=D->Mesh->GetBoneLocation(Left?TEXT("calf_l"):TEXT("calf_r"));
+            // Aim below the knee joint: a 60 cm charged sphere aimed at that joint also catches
+            // the hanging hand from the side. Keep the real enlarged sweep and first-contact behavior.
+            const FVector Knee=D->Mesh->GetBoneLocation(Left?TEXT("calf_l"):TEXT("calf_r"));
+            const FVector Ankle=D->Mesh->GetBoneLocation(Left?TEXT("foot_l"):TEXT("foot_r"));
+            FVector Point=FMath::Lerp(Knee,Ankle,0.32f);
+            const float FloorZ=Home.Z-D->Capsule->GetScaledCapsuleHalfHeight()-1.f;
+            Point.Z=FMath::Max(Point.Z,FloorZ+S->GetRadius(Charge)+2.f);
             const FVector Out=D->GetActorRightVector()*(Left?-1.f:1.f);
             FActorSpawnParameters Spawn;Spawn.Owner=P;
             auto* Orb=W->SpawnActor<ARPGEnergyOrb>(Point+Out*150.f,FRotator::ZeroRotator,Spawn);
diff --git a/Source/RPG_CODEX/RPGEnergyOrb.cpp b/Source/RPG_CODEX/RPGEnergyOrb.cpp
index d63d5ac..316091e 100644
--- a/Source/RPG_CODEX/RPGEnergyOrb.cpp
+++ b/Source/RPG_CODEX/RPGEnergyOrb.cpp
@@ -18,6 +18,7 @@ ARPGEnergyOrb::ARPGEnergyOrb()
     Root=CreateDefaultSubobject<USceneComponent>(TEXT("Root")); SetRootComponent(Root);
     static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
     static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
+    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
     auto Part=[&](const TCHAR* Name,UStaticMesh* Shape)
     {
         UStaticMeshComponent* C=CreateDefaultSubobject<UStaticMeshComponent>(Name);
@@ -33,6 +34,12 @@ ARPGEnergyOrb::ARPGEnergyOrb()
         Sparks.Add(Part(*FString::Printf(TEXT("Spark%d"),I),Sphere.Object));
         Trail.Add(Part(*FString::Printf(TEXT("Trail%d"),I),Sphere.Object));
     }
+    // Three short branching clusters, pooled once. They never become projectile/trail parts.
+    for(int32 I=0;I<27;++I)
+    {
+        UStaticMeshComponent* Arc=Part(*FString::Printf(TEXT("ChargeStatic%d"),I),Cylinder.Object);
+        Arc->SetVisibility(false); StaticArcs.Add(Arc);
+    }
     InitialLifeSpan=12.f;
 }

@@ -47,11 +54,14 @@ void ARPGEnergyOrb::BeginPlay()
     CoreMaterial=Material(TEXT("/Game/RPG/Spells/Energy/M_EnergyCore.M_EnergyCore"));
     ShellMaterial=Material(TEXT("/Game/RPG/Spells/Energy/M_EnergyShell.M_EnergyShell"));
     RingMaterial=Material(TEXT("/Game/RPG/Spells/Energy/M_EnergyRing.M_EnergyRing"));
+    StaticMaterial=Material(TEXT("/Game/RPG/Spells/Energy/M_EnergyStatic.M_EnergyStatic"));
     if(CoreMaterial) Core->SetMaterial(0,CoreMaterial);
     if(ShellMaterial) Shell->SetMaterial(0,ShellMaterial);
     for(UStaticMeshComponent* C:Filaments) if(RingMaterial) C->SetMaterial(0,RingMaterial);
     for(UStaticMeshComponent* C:Sparks) if(CoreMaterial) C->SetMaterial(0,CoreMaterial);
     for(UStaticMeshComponent* C:Trail) if(ShellMaterial) C->SetMaterial(0,ShellMaterial);
+    for(UStaticMeshComponent* C:StaticArcs) if(StaticMaterial) C->SetMaterial(0,StaticMaterial);
+    StaticSeed=int32(GetUniqueID());
     TrailPositions.Init(GetActorLocation(),Trail.Num()); UpdateVisual(0.f,0.f);
 }

@@ -68,6 +78,7 @@ void ARPGEnergyOrb::ChargeAt(URPGEnergySpellComponent* Source,bool bLeft)

 void ARPGEnergyOrb::Launch(const FVector& Direction,float Charge,float Radius,float Speed,float Range,float Damage,float Impulse,FLinearColor Color)
 {
+    StopChargeEffects();
     ChargeSource.Reset(); bLaunched=true; OrbRadius=FMath::Max(1.f,Radius); Tint=Color;
     Impact.Attacker=GetOwner(); Impact.Direction=Direction.GetSafeNormal();
     Impact.Charge=FMath::Clamp(Charge,0.f,1.f); Impact.Damage=FMath::Max(0.f,Damage); Impact.Impulse=FMath::Max(0.f,Impulse);
@@ -147,14 +158,18 @@ void ARPGEnergyOrb::Tick(float Delta)
 void ARPGEnergyOrb::UpdateVisual(float Delta,float Charge)
 {
     const float Fade=bImpacted?1.f-FMath::SmoothStep(0.08f,0.35f,ImpactAge):1.f;
+    const float Wave=FMath::Sin(Age*(7.f+Charge*3.f));
+    const float Brightness=bImpacted?1.f:1.12f+Wave*(0.22f+Charge*0.18f);
     for(UMaterialInstanceDynamic* M:{CoreMaterial.Get(),ShellMaterial.Get(),RingMaterial.Get()})
-        if(M) { M->SetVectorParameterValue(TEXT("Color"),Tint); M->SetScalarParameterValue(TEXT("Fade"),Fade); }
-    const float Pulse=1.f+0.025f*FMath::Sin(Age*(12.f+Charge*10.f));
+        if(M) { M->SetVectorParameterValue(TEXT("Color"),Tint); M->SetScalarParameterValue(TEXT("Fade"),Fade); M->SetScalarParameterValue(TEXT("Brightness"),Brightness); }
+    const float Pulse=1.f+(0.035f+Charge*0.025f)*Wave;
+    const float Shape=bImpacted?0.f:0.025f*FMath::Sin(Age*5.3f);
+    const FVector BreathingShape(1.f+Shape,1.f-Shape*0.65f,1.f-Shape*0.35f);
     const float CoreRadius=bImpacted?OrbRadius*FMath::Lerp(1.f,0.1f,FMath::Clamp(ImpactAge/0.25f,0.f,1.f)):OrbRadius*0.72f*Pulse;
-    Core->SetRelativeScale3D(FVector(CoreRadius/50.f));
+    Core->SetRelativeScale3D(BreathingShape*(CoreRadius/50.f));
     // The burst's maximum diameter is twice that of the incoming orb, without splash damage.
     const float BurstRadius=bImpacted?OrbRadius*FMath::Lerp(1.f,2.f,FMath::SmoothStep(0.f,0.22f,ImpactAge)):OrbRadius*Pulse;
-    Shell->SetRelativeScale3D(FVector(BurstRadius/50.f));
+    Shell->SetRelativeScale3D(BreathingShape*(BurstRadius/50.f));
     for(int32 I=0;I<Filaments.Num();++I)
     {
         Filaments[I]->SetRelativeScale3D(FVector(BurstRadius*2.f/100.f));
@@ -177,4 +192,58 @@ void ARPGEnergyOrb::UpdateVisual(float Delta,float Charge)
             Trail[I]->SetWorldScale3D(FVector(OrbRadius*(1.f-float(I+1)/13.f)*0.7f/50.f));
         }
     }
+    const FRPGEnergyHandState* Held=ChargeSource.IsValid()?(bLeftHand?&ChargeSource->Left:&ChargeSource->Right):nullptr;
+    UpdateStatic(Delta,Held && Held->bHeld && Held->Charge>=1.f && !bLaunched && !bImpacted);
+}
+
+void ARPGEnergyOrb::StopChargeEffects()
+{
+    for(UStaticMeshComponent* Arc:StaticArcs) Arc->SetVisibility(false);
+    StaticTime=0.f;
+    for(FStaticCluster& Cluster:StaticClusters) Cluster.Cycle=INDEX_NONE;
+}
+
+void ARPGEnergyOrb::UpdateStatic(float Delta,bool bFullyHeld)
+{
+    if(!bFullyHeld) { StopChargeEffects(); return; }
+    StaticTime+=Delta;
+    if(StaticMaterial) StaticMaterial->SetVectorParameterValue(TEXT("Color"),FMath::Lerp(Tint,FLinearColor(0.65f,0.9f,1.f),0.72f));
+    for(int32 I=0;I<3;++I)
+    {
+        FStaticCluster& Cluster=StaticClusters[I];
+        const float Period=0.43f+I*0.137f,Clock=StaticTime+I*0.19f;
+        const int32 Cycle=FMath::FloorToInt(Clock/Period);
+        const float Phase=FMath::Fmod(Clock,Period);
+        // A brief double flicker followed by a quiet gap, at independently changing surface locations.
+        const bool bVisible=Phase<0.135f && !(Phase>0.043f && Phase<0.066f);
+        if(bVisible && Cluster.Cycle!=Cycle)
+        {
+            Cluster.Cycle=Cycle; Cluster.Points.Reset(10);
+            FRandomStream Random(StaticSeed+I*7919+Cycle*104729);
+            const FVector Normal=Random.VRand();
+            const FVector Tangent=FVector::CrossProduct(Normal,FMath::Abs(Normal.Z)<0.9f?FVector::UpVector:FVector::RightVector).GetSafeNormal();
+            const FVector Across=FVector::CrossProduct(Normal,Tangent).GetSafeNormal();
+            for(int32 P=0;P<7;++P)
+            {
+                const FVector OnSphere=Normal+Tangent*(float(P)/6.f-0.5f)*1.05f+Across*Random.FRandRange(-0.12f,0.12f);
+                Cluster.Points.Add(OnSphere.GetSafeNormal()*Random.FRandRange(1.25f,1.44f));
+            }
+            for(int32 P=0;P<3;++P)
+                Cluster.Points.Add((Cluster.Points[3]+Across*(0.14f+P*0.13f)+Tangent*Random.FRandRange(-0.09f,0.09f)).GetSafeNormal()*Random.FRandRange(1.28f,1.49f));
+        }
+        for(int32 S=0;S<9;++S)
+        {
+            UStaticMeshComponent* Arc=StaticArcs[I*9+S];
+            Arc->SetVisibility(bVisible && StaticMaterial!=nullptr);
+            if(!bVisible || Cluster.Points.Num()!=10) continue;
+            const int32 A=S<6?S:S==6?3:S;
+            const int32 B=S+1;
+            const FVector Start=Cluster.Points[A]*OrbRadius,End=Cluster.Points[B]*OrbRadius;
+            const FVector Segment=End-Start;
+            Arc->SetRelativeLocation((Start+End)*0.5f);
+            Arc->SetRelativeRotation(FQuat::FindBetweenNormals(FVector::UpVector,Segment.GetSafeNormal()));
+            const float Width=FMath::Max(0.2f,OrbRadius*0.03f)*(S<6?1.f:0.7f);
+            Arc->SetRelativeScale3D(FVector(Width/100.f,Width/100.f,Segment.Size()/100.f));
+        }
+    }
 }
diff --git a/Source/RPG_CODEX/RPGEnergyOrb.h b/Source/RPG_CODEX/RPGEnergyOrb.h
index 95146e8..eef292a 100644
--- a/Source/RPG_CODEX/RPGEnergyOrb.h
+++ b/Source/RPG_CODEX/RPGEnergyOrb.h
@@ -19,6 +19,7 @@ public:
     virtual void BeginPlay() override;
     virtual void Tick(float Delta) override;
     void ChargeAt(URPGEnergySpellComponent* Source, bool bLeft);
+    void StopChargeEffects();
     void Launch(const FVector& Direction, float Charge, float Radius, float Speed, float Range, float Damage, float Impulse, FLinearColor Color);
     static bool Trace(UWorld* World, const FVector& Start, const FVector& End, float Radius, AActor* Ignore, FHitResult& Hit);
     static void ApplyImpact(const FRPGEnergyImpact& Impact);
@@ -33,9 +34,11 @@ private:
     UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Filaments;
     UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Sparks;
     UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Trail;
+    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> StaticArcs;
     UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CoreMaterial;
     UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ShellMaterial;
     UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> RingMaterial;
+    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> StaticMaterial;
     TWeakObjectPtr<URPGEnergySpellComponent> ChargeSource;
     bool bLeftHand = false;
     FVector Velocity = FVector::ZeroVector;
@@ -44,6 +47,15 @@ private:
     float ImpactAge = 0.f;
     FLinearColor Tint = FLinearColor(0.08f,0.5f,1.f);
     TArray<FVector> TrailPositions;
+    struct FStaticCluster
+    {
+        int32 Cycle = INDEX_NONE;
+        TArray<FVector> Points;
+    };
+    FStaticCluster StaticClusters[3];
+    int32 StaticSeed = 0;
+    float StaticTime = 0.f;
     void HitTarget(const FHitResult& Hit);
     void UpdateVisual(float Delta, float Charge);
+    void UpdateStatic(float Delta, bool bFullyHeld);
 };
diff --git a/Source/RPG_CODEX/RPGEnergySpellComponent.cpp b/Source/RPG_CODEX/RPGEnergySpellComponent.cpp
index 32d1ff5..6a303d2 100644
--- a/Source/RPG_CODEX/RPGEnergySpellComponent.cpp
+++ b/Source/RPG_CODEX/RPGEnergySpellComponent.cpp
@@ -93,6 +93,7 @@ void URPGEnergySpellComponent::Release(ERPGHand Which)
     FRPGEnergyHandState& Hand = Which == ERPGHand::Left ? Left : Right;
     if (!Hand.bHeld) return;
     Hand.bHeld = false;
+    if (Hand.Orb.IsValid()) Hand.Orb->StopChargeEffects();
     Hand.ReleasedCharge = Hand.Charge;
     Hand.ReleaseWait = 0.f;
     Hand.ReleaseSeconds = 0.f;
diff --git a/Source/RPG_CODEX/RPGEnergySpellComponent.h b/Source/RPG_CODEX/RPGEnergySpellComponent.h
index 6a20035..991e993 100644
--- a/Source/RPG_CODEX/RPGEnergySpellComponent.h
+++ b/Source/RPG_CODEX/RPGEnergySpellComponent.h
@@ -37,7 +37,7 @@ public:
     virtual void TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* Tick) override;

     UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Energy", meta=(ClampMin="0.1")) float FullChargeSeconds = 1.5f;
-    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Energy", meta=(ClampMin="1")) float BaseRadius = 10.f;
+    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Energy", meta=(ClampMin="1")) float BaseRadius = 15.f;
     UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Energy", meta=(ClampMin="1")) float FullSizeMultiplier = 2.f;
     UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Energy") FLinearColor Color = FLinearColor(0.08f,0.5f,1.f);
     UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Energy", meta=(ClampMin="100")) float ProjectileSpeed = 3300.f;
diff --git a/Source/RPG_CODEX/RPGEnergySpellTest.cpp b/Source/RPG_CODEX/RPGEnergySpellTest.cpp
index 6480bff..db96009 100644
--- a/Source/RPG_CODEX/RPGEnergySpellTest.cpp
+++ b/Source/RPG_CODEX/RPGEnergySpellTest.cpp
@@ -22,6 +22,7 @@
 #include "Engine/LocalPlayer.h"
 #include "Engine/StaticMeshActor.h"
 #include "Engine/World.h"
+#include "EngineUtils.h"
 #include "GameFramework/Character.h"
 #include "GameFramework/CharacterMovementComponent.h"
 #include "GameFramework/PlayerController.h"
@@ -93,10 +94,48 @@ public:
             Dir=FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_SPELL_REVIEW_DIR"));
             if(Dir.IsEmpty())Dir=FPaths::ProjectSavedDir()/TEXT("SpellReview/Default");
             IFileManager::Get().MakeDirectory(*Dir,true);
-            Test->TestEqual(TEXT("Tap orb radius"),Spells->GetRadius(0.f),10.f);
-            Test->TestEqual(TEXT("Full orb doubles diameter"),Spells->GetRadius(1.f),20.f);
+            Test->TestEqual(TEXT("Tap orb radius enlarged by fifty percent"),Spells->GetRadius(0.f),15.f);
+            Test->TestEqual(TEXT("Full orb doubles diameter"),Spells->GetRadius(1.f),30.f);
         }
         const float T=World->GetTimeSeconds()-Start,Dt=World->GetDeltaSeconds();
+        // Inspect the actual pooled render components, including the release delay and in-flight actor.
+        for(TActorIterator<ARPGEnergyOrb> It(World);It;++It)
+        {
+            const ARPGEnergyOrb* Orb=*It;
+            const FRPGEnergyHandState* Hand=Spells->Left.Orb.Get()==Orb?&Spells->Left:Spells->Right.Orb.Get()==Orb?&Spells->Right:nullptr;
+            const bool bFullHeld=Hand && Hand->bHeld && Hand->Charge>=1.f;
+            TInlineComponentArray<UStaticMeshComponent*> Parts(Orb);
+            for(const UStaticMeshComponent* Part:Parts)
+            {
+                if(Part->GetName().StartsWith(TEXT("ChargeStatic")) && Part->IsVisible())
+                {
+                    SawStaticBeforeFull |= !bFullHeld && !Orb->bLaunched;
+                    SawStaticOnProjectile |= Orb->bLaunched || Orb->bImpacted;
+                    SawGroundStatic |= bFullHeld && !State->bIsFlying;
+                    SawHoverStatic |= bFullHeld && State->bIsFlying;
+                    if(bFullHeld)
+                    {
+                        const FVector Unit=Part->GetRelativeLocation()/Orb->OrbRadius;
+                        if(LastStaticPoint.IsNearlyZero()) LastStaticPoint=Unit;
+                        else SawStaticRelocate |= FVector::Distance(Unit,LastStaticPoint)>0.5f;
+                    }
+                }
+                if(Part->GetName()==TEXT("Core") && bFullHeld && T>20.7f && T<23.8f)
+                {
+                    MinFullCoreScale=FMath::Min(MinFullCoreScale,float(Part->GetRelativeScale3D().X));
+                    MaxFullCoreScale=FMath::Max(MaxFullCoreScale,float(Part->GetRelativeScale3D().X));
+                }
+            }
+        }
+        if(T>2.04f && T<2.38f && Spells->Left.PoseWeight>0.99f)
+        {
+            const FVector Shoulder=Player->GetMesh()->GetBoneLocation(TEXT("upperarm_l"));
+            const FVector Elbow=Player->GetMesh()->GetBoneLocation(TEXT("lowerarm_l"));
+            const FVector Wrist=Player->GetMesh()->GetBoneLocation(TEXT("hand_l"));
+            const float Bend=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct((Elbow-Shoulder).GetSafeNormal(),(Wrist-Elbow).GetSafeNormal()),-1.,1.)));
+            if(T<2.17f) { FullElbowBend=FMath::Max(FullElbowBend,Bend); ++ElbowSamples; }
+            if(T>2.28f) ReleaseElbowBend=FMath::Min(ReleaseElbowBend,Bend);
+        }
         Camera->SetActorLocation(Home+FVector(-420,-650,160)+FVector(0,0,Player->GetActorLocation().Z-Home.Z));
         const FVector Target=Dummy->Mesh->GetBoneLocation(TEXT("spine_04"));
         Camera->SetActorRotation((Target-Camera->GetActorLocation()).Rotation());
@@ -223,6 +262,11 @@ public:
         Test->TestTrue(TEXT("Evaluated hover body follows up/down aim"),SawBodyUp && SawBodyDown);
         Test->TestTrue(TEXT("Aim-only hover holds world position"),MaxHoverDrift<0.5f);
         Test->TestTrue(TEXT("Left reaches full charge at 1.5 seconds"),SawFull);
+        Test->TestTrue(TEXT("Static clusters appear at full charge on ground and in hover"),SawGroundStatic && SawHoverStatic && SawStaticRelocate);
+        Test->TestFalse(TEXT("No static before full charge or during release delay"),SawStaticBeforeFull);
+        Test->TestFalse(TEXT("Static never follows the projectile or impact"),SawStaticOnProjectile);
+        Test->TestTrue(TEXT("Fully charged ball visibly pulses its shape"),MinFullCoreScale>0.f && MaxFullCoreScale/MinFullCoreScale>1.07f);
+        Test->TestTrue(TEXT("Straighter charge retains an elbow bend and extends on release"),ElbowSamples>0 && FullElbowBend>15.f && FullElbowBend<46.f && ReleaseElbowBend>5.f && ReleaseElbowBend<FullElbowBend-5.f);
         Test->TestTrue(TEXT("Both hands charge simultaneously"),SawBoth);
         Test->TestTrue(TEXT("Releasing left preserves right charge"),SawIndependent);
         Test->TestTrue(TEXT("Hover spell pose active"),SawHover);
@@ -235,6 +279,7 @@ public:
         FString Report=FString::Printf(TEXT("Energy integration completed. left=%d right=%d both=%d independent=%d hover=%d punch=%d tapImpulse=%.1f chargedImpulse=%.1f hitBone=%s maxSpellHandSpeed=%.1f at %.3f\n"),Spells->Left.ShotsFired,Spells->Right.ShotsFired,SawBoth,SawIndependent,SawHover,SawPunch,TapImpulse,Dummy->PhysicsReaction->LastReaction.ImpulseMagnitude,*TapBone.ToString(),MaxHandSpeed,MaxHandAt);
         FFileHelper::SaveStringToFile(Report,*(Dir/TEXT("result.txt")));Test->AddInfo(Report);
         Test->AddInfo(FString::Printf(TEXT("FORWARD AIM samples=%d bodyYaw=%.2f armYaw=%.2f hoverDrift=%.3f up=%d down=%d left=%d right=%d bank=%d"),ForwardSamples,MaxBodyYaw,MaxArmYaw,MaxHoverDrift,SawHoverUp,SawHoverDown,SawHoverLeft,SawHoverRight,SawHoverBank));
+        Test->AddInfo(FString::Printf(TEXT("STATIC AURA ground=%d hover=%d relocate=%d premature=%d launched=%d pulseRatio=%.3f elbowCharge=%.2f release=%.2f"),SawGroundStatic,SawHoverStatic,SawStaticRelocate,SawStaticBeforeFull,SawStaticOnProjectile,MaxFullCoreScale/FMath::Max(MinFullCoreScale,0.001f),FullElbowBend,ReleaseElbowBend));
         return true;
     }
 private:
@@ -253,6 +298,9 @@ private:
     int32 Waiting=0,Warmup=0,Shot=0,CancelShots=0;
     float Start=-1,NextShot=0,TapImpulse=0,MaxHandSpeed=0,MaxHandAt=0;
     float MaxBodyYaw=0,MaxArmYaw=0,MaxHoverDrift=0;int32 ForwardSamples=0;
+    bool SawGroundStatic=false,SawHoverStatic=false,SawStaticRelocate=false,SawStaticBeforeFull=false,SawStaticOnProjectile=false;
+    float MinFullCoreScale=100.f,MaxFullCoreScale=0.f,FullElbowBend=0.f,ReleaseElbowBend=180.f;int32 ElbowSamples=0;
+    FVector LastStaticPoint=FVector::ZeroVector;
     bool bAimHover=false,bAimCancelled=false,SawHoverUp=false,SawHoverDown=false,SawHoverLeft=false,SawHoverRight=false,SawHoverBank=false,SawBodyUp=false,SawBodyDown=false;
     bool SawFull=false,SawBoth=false,SawIndependent=false,SawHover=false,SawPunch=false,SawAfterPunch=false,bHover=false,bGround=false,bCancelled=false,bCancelStarted=false,bTraceChecks=false;
     bool bTapHit=false,bTapRead=false,bChargedHit=false,bChargedRead=false,LastHandValid=false;
diff --git a/Tools/Spells/build_energy_materials.py b/Tools/Spells/build_energy_materials.py
index 1326fc7..018602c 100644
--- a/Tools/Spells/build_energy_materials.py
+++ b/Tools/Spells/build_energy_materials.py
@@ -26,7 +26,7 @@ def custom(mat, code, inputs, output=unreal.CustomMaterialOutputType.CMOT_FLOAT3
         connect(source, value, name)
     return value

-for kind in ['Core', 'Shell', 'Ring']:
+for kind in ['Core', 'Shell', 'Ring', 'Static']:
     path = f'{ROOT}/M_Energy{kind}'
     mat = unreal.load_asset(path)
     if not mat:
@@ -35,23 +35,27 @@ for kind in ['Core', 'Shell', 'Ring']:
     mel.delete_all_material_expressions(mat)
     mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
     mat.set_editor_property('two_sided', True)
-    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE if kind == 'Core' else unreal.BlendMode.BLEND_ADDITIVE)
+    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE if kind in ['Core', 'Static'] else unreal.BlendMode.BLEND_ADDITIVE)
     uv = node(mat, unreal.MaterialExpressionTextureCoordinate)
     time = node(mat, unreal.MaterialExpressionTime)
     color = node(mat, unreal.MaterialExpressionVectorParameter, parameter_name='Color', default_value=unreal.LinearColor(0.08,0.5,1,1))
     fade = node(mat, unreal.MaterialExpressionScalarParameter, parameter_name='Fade', default_value=1.0)
+    brightness = node(mat, unreal.MaterialExpressionScalarParameter, parameter_name='Brightness', default_value=1.0)
     fresnel = node(mat, unreal.MaterialExpressionFresnel, exponent=2.0, base_reflect_fraction=0.02)
-    inputs = {'UV':uv, 'T':time, 'C':color, 'F':fresnel, 'Fade':fade}
+    inputs = {'UV':uv, 'T':time, 'C':color, 'F':fresnel, 'Fade':fade, 'Brightness':brightness}
     veins = 'float v=pow(saturate(0.5+0.5*sin((UV.x*9+sin(UV.y*23-T*3)*0.15+T*0.8)*6.283185)),18);'
     if kind == 'Core':
         code = veins + 'return (C*(1.7+F*4+v*4)+float3(0.65,0.88,1)*pow(1-F,4)*1.4)*Fade;'
     elif kind == 'Shell':
         code = veins + 'return C*(1.2+v*2.0)*Fade;'
-    else:
+    elif kind == 'Ring':
         code = 'return lerp(C,float3(0.6,0.9,1),0.25)*4*Fade;'
+    else:
+        code = 'return lerp(C,float3(0.78,0.94,1),0.4)*14*Fade;'
+    code = code.replace('*Fade;', '*Fade*Brightness;')
     emissive = custom(mat, code, inputs)
     mel.connect_material_property(emissive, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
-    if kind != 'Core':
+    if kind not in ['Core', 'Static']:
         if kind == 'Shell':
             opacity_code = veins + 'return saturate(pow(F,2.5)*0.6+v*0.13)*Fade;'
         else:
```
