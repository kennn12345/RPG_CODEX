# GASP walking test dummy

Completed 2026-10-22 on `gasp-integration`, at the user's request before stage 3. The user authorized replacing the dummy's assets completely with GASP-compatible assets. Stage 3 remains on hold.

The active `BP_CombatTestDummy` and its placed instance in `Lvl_ThirdPerson` now use `SKM_UEFN_Mannequin`, `SK_UEFN_Mannequin` and `PA_UEFN_Mannequin`. A new `BS_GASP_Dummy_IdleWalk` blends the GASP relaxed standing idle at 0 cm/s into its forward walking loop at 160 cm/s. The source loop already has root locking enabled. No source GASP clip or old RPG clip was edited, converted or deleted.

The original dummy's wandering, collision, stats, damage and reaction logic remain unchanged. Only the four native constructor asset defaults changed, so newly spawned native dummies also receive GASP assets. Existing calibration-arena code still deliberately disables wandering on its stationary targets. The old Manny asset preset is archived as `/Game/RPG/Combat/Legacy/BP_CombatTestDummy_Manny`.

`RPGGASPAssetLibrary::RebuildBlendSpace` is a small editor helper for validating samples and rebuilding blend-space interpolation data after scripted asset creation. It does not change combat behavior. The dummy uses its existing single-node idle/walk system; the player's motion-matching AnimBP expects a character movement component and is not applied to this actor.

Verification:

- Editor closed for both C++ builds; both succeeded.
- Saved player level reloaded with exactly one active test dummy using the UEFN mesh, skeleton and physics asset.
- 22-second in-game check passed: travelled 290.3 cm, reached 160 cm/s, paused, and stayed within its 350 cm wandering radius (observed maximum 290.3 cm). Local foot animation spans were 9.5 / 83.1 / 19.8 cm, confirming animated walking rather than a sliding static pose.
- Stats initialized; the hand maps to ArmRight. PhysicsControl initialized 23 bodies and 22 motors across arms, legs, head and spine.
- Existing `RPG.Power.DummyLaunch` passed: health 100 to approximately 40, launch/ragdoll observed, got up near its landing position and returned to animation. This direct hit test does not rely on the player's still-missing compatible combat clips.
- Evidence: `Saved/GASPIntegration/Dummy/WalkingResult.json`, `Walking.log`, `PowerReport/index.json`, `Power.log`, `BuildAssets.log`, `BuildDummy.log`.
- Full RPG combat suite was not repeated for this asset swap. The stage-2 missing-player-animation limitations remain; no combat code was changed to conceal them.

Open RPG_CODEX and press Play to see the walking dummy. No repeated visual previews were produced.
