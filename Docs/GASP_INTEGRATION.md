# GASP integration checkpoint

## Status

Stage 1 is **in progress, not complete**. Branch: `gasp-integration`, based on `2a17eec` (handoff documentation; parent `9b9d114` is the approved hover backup). Stage 2 has not started. Do not merge main or switch the RPG player yet.

The user approved the isolated CMC review scene, and the log confirms its map save at 2026-10-21 18:33:09 America/Chicago. The original 711 Content files still match their pre-migration SHA-256 manifest. No existing C++ source, RPG input bindings, combat/flight clips, player AnimBP, player Blueprint, or test dummy has changed.

## Imported and configured

- Used the source Unreal editor's Asset Actions > Migrate on `/Game/Blueprints/SandboxCharacter_CMC` and `SandboxCharacter_CMC_ABP` into RPG_CODEX/Content: 2,861 dependency packages.
- The closure includes UEFN animations, pose-search databases, choosers, audio, retarget characters and shared assets. Shared references also pulled in Mover assets. The review pawn is explicitly CMC, not Mover.
- Added the sample plugin declarations while preserving all RPG plugins, sample data-driven console variables, and traversal collision definitions. Existing collision definitions were not replaced.
- Added the sample `Config/DefaultGameplayTags.ini` (no target file existed). Missing tags initially caused invalid-tag warnings and a Smart Object StateTree ensure. After restarting with tags present, the CMC character/AnimBP, sample controller and sample game mode compiled without those tag/StateTree errors recurring.
- New isolated assets: `/Game/RPG/GASPIntegration/BP_GASP_CMC_ReviewGameMode` and `L_GASP_CMC_Review`. The review game mode inherits GM_Sandbox; a placed CMC character auto-possesses Player 0 to avoid GM_Sandbox's Mover default.
- Runtime identity verified: controller `PC_Sandbox_C`, pawn `SandboxCharacter_CMC_C`, movement component `CharacterMovementComponent`.
- Real in-game screenshots captured idle, movement, stopping and jumping. The scene used movement input and jump, not posed animation previews. The user approved saving it.

## Validation evidence

All evidence below is in `Saved/GASPIntegration/` and was preserved before subsequent runs.

| Run | Result | Evidence |
|---|---|---|
| Before migration, full RPG suite in editor | 7 pass / 4 fail | `BaselineEditor.log`, `BaselineSummary.txt` |
| After migration, full RPG suite in original RPG map | 8 pass / 3 fail | `PostMigrationEditor.log`, `PostMigrationSummary.txt` |
| Repeated both contact suites without code changes | Both suites fail | `ContactRepeatEditor.log` |
| Both contact suites with original plugin/engine/tag configuration | Both suites fail | `OriginalSettingsControl.log` |

Baseline failures: DummyWander, StrikeContact, StrikeContactFirstPerson, TestDummyFirstPerson. Post-migration failures: both contact suites and TestDummyFirstPerson. The latter is the documented one-frame shadow discrepancy. All four Power tests, ordinary third-person combat, first-person steadiness and physics reactions passed after migration.

The 60 contact-case results were compared individually; suite totals alone are insufficient:

| Contact case | Baseline | Integration | Integration repeat | Original-settings control |
|---|---|---|---|---|
| TP right punch, dummy walking toward player | PASS | FAIL | FAIL | PASS |
| TP repeated right punch, close range | PASS | FAIL | FAIL | FAIL |
| FP charged right punch, normal range | PASS | FAIL | PASS | PASS |

**Potential regression remains:** the moving-target TP punch consistently differed in these runs. Its cause is not established. Do not claim preservation based only on unchanged assets/source or the suite totals. The repeated close punch also fails under original settings, and the charged FP punch varies between integration runs. Detailed comparison: `ContactCaseComparison.json`.

The control used the original `2a17eec` plugin list and DefaultEngine.ini, with added sample tags temporarily removed from configuration; imported files remained on disk but were not used by the original RPG player. All three integration configuration files were restored afterward. `CONTROL_SETTINGS_ACTIVE.txt` explicitly says inactive; snapshots are in `ControlSettings/`.

Headless attempts using UnrealEditor-Cmd (direct, Start-Process, and a native batch wrapper) exited without creating a log/report. **Headless verification is NOT satisfied.** Do not treat exit code 0 as a passed test run. A user-facing double-click helper was prepared in this chat's outputs directory. It runs the original startup map's RPG suite and writes `Saved/GASPIntegration/PostMigrationHeadless.log` and the accompanying report folder.

## Remaining before completing stage 1

1. A headless log/report has now been obtained (see resume results below); retain the concurrent-run caveat and complete the isolated follow-up.
2. Investigate the potential moving-target contact regression, including whether plugin/settings effects or test nondeterminism cause it. Keep RPG combat source/assets unchanged unless a necessary fix is established.
3. Reopen the saved review map and confirm CMC possession survives a reload.
4. Finish warning review and obtain stage 1 completion approval before stage 2.
5. Commit/push a validated stage checkpoint; the current backup may be a WIP checkpoint and must be described that way.

For stage 2 retain Manny through retargeting, URPGAnimInstance, DefaultSlot and the existing UpperBody layer. No C++ build has been needed or attempted. Close Unreal before future C++ builds.

When an approval is required, minimize Unreal to reveal the chat and clearly state the request. The user explicitly requested this because watching Unreal hid approval messages.

## Git tooling note

The sandbox's Git shell failed to start the standard LFS filter/hooks. A temporary Windows-native batch launcher successfully ran `git-lfs filter-process`; the repository's LFS configuration was not removed. All binary additions were staged as LFS pointers. The existing hooks were inspected and are standard LFS-only wrappers; if their shell fails, perform the LFS upload natively before pushing the branch ref. Never push raw Unreal binaries as a workaround.

## Resume check

The local WIP asset checkpoint is `4f2b444`. On resume, all 711 original content hashes matched again and there were no C++ changes. Native GitHub push was attempted, but Git Credential Manager returned access denied before credentials were retrieved. No remote upload is verified. The user has been asked to run the existing headless test helper manually because automated launches produced no log. Do not open another target editor while that helper is running.

## Manual headless results

The user started the helper, which launched two concurrent runs. Both completed all 11 RPG tests: **7 passed / 4 failed**, with no unrun tests in the exported report. Both logged the test-exit request. Failures: DummyWander, StrikeContact, StrikeContactFirstPerson, TestDummyFirstPerson. All four Power tests, ordinary TP combat, FP steadiness, and physics reactions passed.

Evidence: `Saved/GASPIntegration/PostMigrationHeadless.log`, `PostMigrationHeadless_2.log`, `PostMigrationHeadless/index.json`, and `HeadlessRunSummary.json`. The first log has a shared cached-asset-registry rename error; the second has no such error. Because both ran concurrently against the same project and report directory, do not call this an isolated clean validation. The moving-target TP punch fails in both with approximately 2.5 cm fist penetration.

A focused follow-up helper, `Run-GASP-Contact-Check.cmd` in this chat's outputs, was prepared and the user was asked to launch it once. It refuses to launch while an Unreal editor/test process is running and runs two contact-suite launches sequentially with separate logs/reports. The second requests `-DisablePlugins=ChaosMover,MoverExamples` for that launch only; no project settings/assets are saved. Verify the logs actually exclude these modules before interpreting it as a valid control. No result is available yet.

Reason for this control: local UE source shows UChaosMoverSubsystem registers a solver simulation callback in every PIE/game world at BeginPlay, even without a registered Mover backend. This is only a hypothesis for the changed combat behavior, not an established cause. Do not change combat source or permanently disable dependencies without proving the effect and checking the CMC review scene.