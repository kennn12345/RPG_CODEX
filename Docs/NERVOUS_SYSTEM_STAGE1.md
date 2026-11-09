# Physical response / nervous system: Stage 1 integration audit

Date: 2026-11-02. Project: C:/UNREAL PROJECTS/RPG_CODEX.
Inspected branch: gasp-integration, HEAD df0ec76.
Scope: current C++ source, project configuration and selected existing handoffs. No runtime, animation, map, physics asset or Blueprint changes. No new build or in-game verification performed.

## Agreed behavior

Shared player/NPC body condition and immediate physical responses. NPC intent is influenced by personality, training, relationships and current distress. Player intent remains controlled by input. Severe limb injuries should allow sustained attempts to crawl, fight, escape and use reachable surroundings. Magical flight does not require functional limbs; pain can modestly disrupt concentration, with stronger disruption from disorientation. Willpower influences persistence and resistance to disruption, not anatomical capability. Dismemberment visuals can follow the injury/capability foundation.

Workflow: event -> body assessment -> feasible responses -> personality/current-state choice -> physical execution -> reassessment. Immediate protective reflexes can preempt deliberate actions.

## Existing systems and reuse decisions

| Existing system | Observed responsibility | Decision |
|---|---|---|
| RPGBodyMass.h/.cpp | Exact FName lookup for 23 mannequin physics-body bones; normalized segment mass shares applied to actual mesh bodies | Keep as mass authority; add persistent condition in a separate component |
| RPGPowerSystem / RPGPowerTypes | Calculates damage, hit zone, structural severity, reaction and displacement | Keep calculation; consume its existing StructuralDamage channel once per real applied hit |
| RPGHitOutcomeComponent | Shared balance meter, directional brace overlay, torso bend, knee catches, braced fall setup; player hit execution | Extend with capability checks and response coordination |
| RPGReactivePhysicsComponent | Local upper-body Physical Animation reactions; legs/pelvis remain on animation; explicit handoff to legacy physics | Reuse local reaction executor; do not introduce competing muscle motors |
| RPGPhysicsReactionComponent | PhysicsControl ownership, powered reactions, full ragdoll, flight-follow behavior, ground grip, settling and pinned return fade | Keep low-level physics owner; add a recovery decision boundary before automatic return |
| RPGBodyImpactComponent | Mass-based character collisions, down state, player mesh/capsule following and shared pose-matched get-up | Keep; gate recovery by capabilities and separate magical flight disruption from ground knockdown |
| RPGNpcCharacter | NPC damage application, reaction selection, shoves, ragdoll and get-up handoffs | Adapt routing gradually rather than replacing the character |
| RPGNpcAIController | Navigation-based wandering, waits while IsBusy | Extend with persistent intent and injured action selection; wandering alone cannot provide survival behavior |
| RPGMovementAbilityComponent | Player movement abilities, MOVE_Flying, acceleration and fast flight | Keep flight mechanics; later accept transient concentration/control effects |
| RPGFlightAnimationComponent / RPGDirectionalHoverComponent | Flight presentation, hover and release to physics | Keep, with explicit airborne response policy |
| RPGAnimInstance proxy | Applies flight, aim, brace, spell and strike-contact layers after the animation graph | Reuse presentation pipeline; resolve limb ownership before publishing overlays |

Current default/editor map: /Game/RPG/Maps/L_DummyField (Config/DefaultEngine.ini).

## How body identification actually works

RPGBodyMass::Fraction matches physics-body bone names such as upperarm_l, lowerarm_l, hand_l, thigh_l, calf_l and foot_l. Apply reads each body InstanceBoneIndex through Mesh->GetBoneName and applies its mass share. This is a concrete existing segment map, not a persistent injury model.

Other systems independently group bones: Power GetHitZone uses name substrings; reactive physics resolves physical bodies and discovers descendants of spine_02; PhysicsControl maintains bone/limb maps. The new condition model should provide a shared validated region mapping while preserving these current executors during migration. Unknown bones must not silently become a disabling torso injury.

Important severing caveat: Apply normalizes available shares back to TotalKg. Reapplying the original total after removing bodies could redistribute a missing limb's mass into the remaining body. Later severing needs retained/detached mass accounting and consistent gameplay mass for collision calculations; do not simply rerun Apply with the original total.

## Current execution paths

NPC melee -> ReceivePowerHit -> MakeMeleeInput + HitOutcome::PrepareInput -> Power::Calculate -> GAS health application -> React -> local hit / shove / knee catch / full ragdoll. Energy uses ReceiveEnergyHit with spell damage and the same React selection.

Player helpers are created by RPGControllerInputComponent::EnsureHelperComponents. They include stats, movement abilities, flight animation, hover, body impact and hit outcome. They do not explicitly create the NPC local-reactive component. Share new condition/coordinator code through both initialization paths; do not assume player and NPC component sets are identical.

NPC constructor explicitly creates stats, PhysicsReaction, HitReaction, ReactivePhysics, BodyImpact, HitOutcome and CharacterContact. It does not explicitly create flight abilities/presentation. NPC Blueprint components were not inspected, so flying NPC initialization remains to be verified.

PhysicsReaction::EnterFullRagdoll -> OnLimpStarted -> NPC OnRagdoll -> stop AI movement, stop active reaction, disable CharacterMovement. Physics settling/time currently leads to ReturnToAnimated -> OnLimpEnding -> NPC HandleLimpEnding or BodyImpact's player handler -> shared StandUp.

## Integration boundaries that need changes

1. StructuralDamage is already calculated (PowerSystem.cpp) and described as not yet applied (PowerTypes.h). Searches found calculation, logging and a calculation test, but no persistent injury consumer. Add injury recording at the actual damage application boundary, not in Power::Calculate: previews must remain side-effect free. NPC/player and melee/energy routes need exactly-once application.
2. Recovery currently assumes standing. NPC HandleLimpEnding calls StandUp, clears bDown and later restores KO health. BodyImpact handles the player's corresponding return. StandUp sets MOVE_Walking. Add a decision before return begins: remain down, reposition, crawl, supported recovery, or eligible get-up. Capability rejection must also prevent the generic return fade; filtering the clip alone is insufficient.
3. OnLimpEnding is a notification, not a veto/selection contract. Introduce a narrow recovery request/decision API at the physics boundary; keep existing return behavior as the healthy default. Do not rely on delegate listener ordering to suppress standing.
4. Current production get-up pool is exactly FaceDown_Capture8614 and FaceUp_Capture8611, both standing recoveries. Preserve them. Older GETUP_HANDOFF and REACTIVE_PHYSICS_PHASE1 notes describe retired larger pools; current source and MOCAP_ONLY_GETUPS supersede those counts. Crawling and asymmetric recovery need additional execution support, not relabeling a standing animation.
5. BodyImpact::KnockDown calls StopFlight and disables movement. FlightAnimation suppresses flight while limp/down. Separate loss of magical flight control from a physical hit or limb disability. Keep genuine hard-crash handling, but do not route every airborne injury into ground knockdown.
6. AI currently waits while busy/down. Injured characters need meaningful downed intent/actions while ordinary navigation remains suspended. Walking path availability does not prove crawl clearance or environmental hand support.
7. Animation evaluation applies brace before spell pose, filmed fist aim and final strike contact. Evaluation order alone does not make bracing authoritative. Add per-region grants so lower-priority writers yield; keep final contact correction for the limbs actually granted attack control.
8. NPC React currently treats any ActiveReaction as getting up for escalation. Track response kind explicitly before adding wound-reaching/crawling montages, or those actions may be mistaken for get-ups.
9. Health/KO currently allows automatic full-health recovery in NPC HandleLimpEnding and player hit-outcome logic. Persistent injury must remain independent of that reset. Revisit the intended KO policy separately without silently removing injuries on stand-up.

## Proposed additions (design names, not implemented)

- Body condition component: rig-validated segment identity, present/functional state, capability queries, injury records, wound locations and optional embedded-object references. Parent/child severing must invalidate downstream capabilities. Mass remains in RPGBodyMass.
- Physical response coordinator: request type, urgency, required regions/capabilities, interruption policy, execution status and ownership grants. Calls existing reaction/movement executors. It should not become another physics controller.
- Personality profile: courage, aggression, self-preservation, composure, willpower and targeted loyalty. Training and current pain/fear/fatigue/concentration remain distinct. NPC intent selection uses feasible actions and commitment/hysteresis. Player intent is not selected by NPC personality logic.
- Recovery action descriptions: required limbs/support, valid start conditions, completion/failure conditions and fallback. Existing two captured get-ups receive requirements based on their actual motion before disabled-limb use is allowed.

Keep willpower distinct from existing GAS MagicControl: MagicControl can represent magical skill; willpower represents persistence under distress. Exact formula and whether any new value belongs in GAS are later implementation decisions.

## Staged implementation and acceptance

Stage 1 (this audit): identify current owners, reuse points and conflicts. Source inspection complete; runtime baseline not executed.
Stage 2: add body-condition foundation and debug injury/reset controls to both player and NPC. Healthy default leaves behavior unchanged. Test segment mapping, downstream capability loss, persistent injury despite health reset, and exactly-once real-hit application; Power previews must not mutate state.
Stage 3: add explicit response kind and region grants, then localized injury reactions/wound-reaching. Test movement overlap, repeated hits, cancellation and brace preemption without stale overlays or double impulses.
Stage 4: gate ragdoll return/get-up and choose viable recovery. Keep healthy captured get-ups. Test disabled support limbs and no fallback into standing when recovery is rejected.
Stage 5: sustained ground movement and cover goal. Add actual crawl execution/collision clearance and failure adaptation; then environmental support interactions. Do not simulate crawling by sliding a prone ragdoll with walking navigation.
Stage 6: connect Survivor, Aggressor and Protector presets to working choices, with targeted loyalty and commitment. Same body injury and scene should produce distinct decisions without changing physical feasibility.
Stage 7: magical flight injury integration and NPC flight initialization. Ordinary limb injury preserves flight; distress effects recover over time; severe disorientation requests a separately defined stabilization/landing response.
Stage 8: severing visuals, physics constraints, detached/retained mass and persistent capability updates. Interrupt actions requiring the lost limb.
Stage 9: player feel, visual variation and performance tuning.

## First playable slice and test area

A moving NPC receives a deliberately injected disabling leg injury, reacts, loses support where appropriate, falls/braces, rejects an unsuitable standing recovery, then crawls toward a reachable cover target. Repeat with each personality after the physical slice works. Flight counterpart applies the injury while hovering/moving and verifies flight remains available.

Start with disposable test fixtures: flat floor, cover marker/block, nearby wall and reproducible character spawn. Later add drops, slopes, stairs, tight spaces and elevated landing platforms. A wall is only an obstacle until validated support detection/reach/animation exists. No permanent map edits made during this audit.

Debug readout should show injury/segment capabilities, intent, active response kind, per-region owner, recovery rejection reason, flight concentration disruption and last failure. Include deterministic personality/scenario selection, slow motion, reset and event history.

## Verification boundary / next step

Existing relevant tests include RPG.Npc.ReactivePhysics, ReactiveStandalone, Collision, GetUpVariety, GetUpRepeat, BraceRecovery, CatchBump, RagdollWalkContact, RecoveryOverlap, RecoveryFlightImpact, ReactionStress, RPG.Player.FlightCrashFirstPerson and RPG.Flight.Transitions. Run targeted current baseline checks before runtime changes, then relevant regression checks after each implementation stage. Existing recorded passes in older docs are historical, not fresh results.

Blueprint animation graphs, placed test-area geometry and live behavior have not been inspected here. No engine shutdown/build/test was needed for this documentation-only stage. Existing dirty files were preserved: DefaultEditor.ini, fight stance asset, Blender directory, GETUP_HANDOFF.md and three Blender scripts. No commits made.
