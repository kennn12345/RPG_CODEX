# Catch contact and stale brace recovery — 31 October 2026

Follow-up to user play review of `8a524fb`: walking into a catching/falling NPC could move it several feet, and repeated knockdowns could leave a standing Standard Bandit's arms held backward in a brace pose.

## Causes and fixes

`URPGHitOutcomeComponent::BeginFall` clears brace weight immediately on knockout; `TryKneeCatch` also clears it when switching to a knee-catch montage. The old Tick only published the overlay while a nonzero weight existed. If the weight had already become zero, the animation instance could keep the last weight of 1 forever. The component now publishes its authoritative pose every tick, including zeros. This is a few pose-field assignments, with no extra simulation. A real braced-fall → lethal follow-up → get-up test reproduced component weight 0 / animation weight 1 on three consecutive cycles before the fix.

`ARPGNpcCharacter::ReceiveBump` used the attack knockback minimum even though body-impact already supplied a mass-split collision velocity. A 117 cm/s transferred walking bump queued a 400 cm/s shove. Body bumps now keep their transferred speed without that attack minimum. Light bumps during an active knee catch give through the existing local reaction without queuing another feet-movement drive underneath root motion. Attack knockbacks retain their existing tuning.

`URPGPhysicsReactionComponent::SetSolidRagdoll` explicitly made ragdoll bones block Pawn capsules. The player's Character Movement has physics interaction enabled with PushForceFactor 750000, which could push individual simulated bones independently of the custom body-impact response. Downed ragdoll meshes now ignore Pawn capsules. Walking capsules can pass through a downed ragdoll without pushing it. Standing/catching movement capsules retain their ordinary blocking behavior. Ragdolls still block the world, PhysicsBody objects and each other, and still take aim/spell traces. Their prior collision responses are restored on recovery.

## Verification scope

New tests: `RPG.Npc.BraceRecovery` (three real repeated fall/knockout/recovery cycles), `RPG.Npc.CatchBump` (light momentum while a real knee-catch montage plays), and `RPG.Npc.RagdollWalkContact` (actual walking motion past a zero-launch ragdoll on a flat test floor). A test-only environment flag `RPG_TEST_RESTORE_PAWN_BLOCK=1` restores the old ragdoll collision response for comparison; never enable it for ordinary validation.

Focused fixed runs cleared component and animation brace weights to 0 in all three cycles and queued 0 cm/s of extra drive for the 117 cm/s knee-catch bump. In the walking comparison, restoring Pawn blocking produced 79.5 cm of pelvis travel and 301.4 cm/s peak horizontal speed; the fixed response produced 32.6 cm and 87.9 cm/s. This is a controlled reproduction of excess travel, not a claim to reproduce every reported launch. The walking regression requires travel within two feet (60.96 cm).

Final regression results and source hashes are recorded in the Codex workspace's `outputs/Catch_Recovery_Fixes.md` and adjacent evidence folder. The earlier Power/ground-grip and contact/brace-motor fixes remain in place; input mappings, charge behavior, punch animations and fantasy Strength limits were not changed.

The Editor Development build and all three focused checks passed. The full 28-test run passed 27; PunchImpact's 105 cm chin-aim case missed once. A separate final run passed both PunchImpact (all five cases) and RagdollWalkContact with its tightened two-foot bound. The miss remains an intermittent targeting issue, not a resolved finding. The last source change before that final build tightened only the walking test's distance bound; production behavior was unchanged from the full regression. Launch calibration passed at 83–108% calculated distance (largest measured 138.5 ft), and GroundGrip measured 139 cm total / 24 cm after grip. Checked PhysicsControl, dropped-impulse and fully-simulated-mesh movement warning counts were zero.
