# Stage 2 — GASP player checkpoint

Stage 1 approved; stage 2 implementation saved on `gasp-integration`. Stage 2 verification is complete; awaiting user approval. Stage 3 has not started. Keep main unchanged and wait for user approval at the stage boundary.

## Player and controls

- New `/Game/RPG/Player/BP_RPGPlayer` is a duplicate of `SandboxCharacter_CMC`. It keeps `SKM_UEFN_Mannequin`, `SK_UEFN_Mannequin`, and `SandboxCharacter_CMC_ABP` unchanged.
- Existing `BP_ThirdPersonGameMode` now spawns this new player. The original player Blueprint remains intact for fallback.
- Added native RPG controller input, the GASP input bridge, and a flight-animation component with its three old Manny clips unset on the new player only. Existing gameplay helper components attach through the original RPG input component.
- The bridge clones the sample mapping context in memory. It retains sample movement/look, keyboard jump/traversal and walk; RPG owns the gamepad buttons. Left Shift and C also map to RPG sprint/crouch. Sample debug, aim, strafe and conflicting button mappings are removed from the clone.
- The bridge suppresses `IMC_Default` and `IMC_MouseLook` on the new player, because the existing controller adds these template contexts with different input actions. The original `IMC_Sandbox` is replaced by the filtered clone. Source input assets and the old player are unchanged.
- RPG sprint, crouch, aim and defensive/first-person strafe intent feed GASP's existing input-state struct before the sample calculates movement. Sample locomotion remains responsible for gait and animation.
- Disabled `DDCVar.NewGameplayCameraSystem.Enable` in project defaults so the existing RPG spring-arm/camera component controls the new player. This also changes the default camera choice in the isolated sample review scene.

## Verification

- C++ build succeeded after closing Unreal. Build log: `Saved/GASPIntegration/Stage2/BuildFinal.log`. Engine/toolchain warnings and a cache-result-store warning remain; no adapter compile errors.
- Saved assets reloaded in a fresh editor. PIE possessed `BP_RPGPlayer_C` using the UEFN mesh/skeleton and the original sample AnimBP.
- Automated action injection verified movement, sprint state, crouch and jump. Final run observed 500 cm/s peak horizontal movement. This is a functional check, not a measurement of maximum sprint speed.
- Input/state bridge readiness passed. The three unfiltered/template mapping contexts were absent. Required movement/look and RPG actions had active key mappings. Hardware Xbox controller feel and appearance remain for the user's direct review in Unreal.
- `Tools/GASP/run_stage2_checks.py` runs the same saved-player checks without taking screenshots, writes `Saved/GASPIntegration/Stage2/ReloadFunctionalResult.json`, and closes Unreal. Launch through Unreal's `-ExecutePythonScript` option; it imports the callback module so its state persists across frames.
- Original asset SHA-256 manifest: 710 of 711 files remain identical; the sole intended change is `BP_ThirdPersonGameMode`. Existing C++ gameplay/combat source and all old animation clips remain unchanged.

## Known limitations and next stage

The old Manny attack/guard/kick/flight clips are deliberately not retargeted. The new player may remain in its GASP pose during those actions. Existing attack hit windows and hit detection depend on compatible attack clips; missing animation can therefore prevent hits, impulses and dummy reactions. This is expected under the revised handoff, and is not evidence that all gameplay is already verified.

The first post-change headless suite ran all 11 tests: 5 passed, 6 failed, compared with 7 passed / 4 failed before stage 2. New failing suites were `RPG.Combat.PhysicsReaction` and `RPG.Combat.TestDummy`; both report missing attacks/hits with skeleton-mismatch clip warnings. DummyWander and the two strike-contact and first-person dummy suites also fail. FirstPersonSteadiness and all four Power tests passed. The final headless run after rebuilding the input-context fix confirmed the same 5 pass / 6 fail result across all 11 tests (`Saved/GASPIntegration/Stage2/FinalReport/index.json`, `Final.log`).

Stage 3 must separately verify flight takeoff/movement/landing, gameplay attack/block/charge states, commands/stats/Power System and first-person/shadow behavior. Document clip-dependent gaps; do not change combat source merely to make old-animation tests pass. Stage 4 handles DefaultSlot/UpperBody and the RPG animation-parent behavior. No old clips were converted, rebaked or deleted in this stage.

User instruction recorded 2026-10-22: continue making/testing without repeated previews. The user will inspect the result directly in Unreal. Ask for approval at stage checkpoints.
