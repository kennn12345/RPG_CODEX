# GASP integration checkpoint

## Status

Stage 1 is **ready for user review, awaiting approval**. Branch: `gasp-integration`, based on `2a17eec` (handoff documentation; parent `9b9d114` is the approved hover backup). Stage 2 has not started. Do not merge main or switch the RPG player yet.

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
3. Saved review-map reload now passes (see results below).
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
## Focused control and saved-scene reload results

- Sequential contact runs in `Saved/GASPIntegration/ContactCheck_11384_30471/` each completed both contact suites. The control log confirms ChaosMover and MoverExamples were not mounted. The moving-target TP punch failed with approximately 2.5 cm penetration in both runs. Disabling these two plugins did not resolve it; the specific ChaosMover hypothesis above is unsupported by this control. `Comparison.json` records the result.
- `EngineConfigControl.log` / `EngineConfigControlReport/`: same contact discrepancy using `-DefEngineIni=Saved/GASPIntegration/BaselineEngine.ini`, generated from commit `2a17eec`, with the integration plugins still enabled.
- `OriginalHeadlessControl.log` / `OriginalHeadlessControlReport/`: same discrepancy using that baseline engine configuration and disabling all 20 explicitly added plugin declarations for this launch. RigLogic and HairStrands still mount, as they also did in the original baseline/editor-control logs. Added sample gameplay tags remain enabled in this control, so it is not an exact original-configuration baseline. The difference between earlier rendered editor controls and these headless controls remains unexplained; do not claim a resolved cause or a regression-free integration.
- These extra launches used native UnrealEditor.exe shortcuts opened in Explorer. This succeeded where sandboxed process launches failed. They did not overwrite project configurations. Scratch shortcuts are in this chat's `work/` folder.
- `ReviewReloadResult.json` reports **passed** after a fresh rendered editor launch, explicit loading of the saved review map, and PIE: controller PC_Sandbox_C, pawn SandboxCharacter_CMC_C, CharacterMovementComponent, AnimInstance SandboxCharacter_CMC_ABP_C. Movement reached 500 cm/s and upward jump velocity was observed. The review script ended PIE and closed Unreal without saving assets. `ReviewReload.log` confirms normal exit.
- Reload warning review: no Error or ensure entries. Initial pose-search warnings said searches were skipped while asynchronous index work was pending or failed; they stopped after the early startup frames. Sample console-variable lookup performance warnings and an editor-layout warning were also present. Movement/possession/jump validation alone does not establish complete visual locomotion quality.

Stage 1 remains in progress. Stage 2 is not authorized yet. Native GitHub credential access from the sandbox is still blocked; a user-launched branch-backup helper is the next fallback. Original RPG assets/source remain unchanged.
## Backup verified and matched-timing comparison

The user's native backup helper successfully uploaded **2,863 LFS objects (4.6 GB)** and pushed `gasp-integration`. `BranchBackup.log` records GitHub's advertised branch SHA `642a11670e17a032c209e0d2eabe75359a2eff62`, matching the local HEAD and remote-tracking ref at verification. Earlier credential-access blockers describe the automated attempt; the native manual fallback succeeded. Main was not merged or modified.

A final sequential TP-only contact comparison used **-Deterministic -FPS=60** in both launches and the exact filter `^RPG.Combat.StrikeContact$`. The original-settings launch additionally used baseline DefaultEngine.ini, an empty project DefaultGameplayTags.ini override (none existed originally), and disabled all explicitly added plugin declarations. This corrects the added-tag caveat of the preceding headless control. Files: `FixedOriginal.log`, `FixedOriginalReport/`, `FixedIntegration.log`, `FixedIntegrationReport/`, `FixedTimingComparison.json`.

All 30 TP cases ran in each launch. **No case changed from PASS under original settings to FAIL under integration settings.** One case (right kick, close range) changed from FAIL to PASS; other numerical physics results varied. The moving-target right punch failed in both with 3.8 cm fist penetration, matching hit/impulse counts and near-identical contact metrics. Thus the earlier suspected new failure is not isolated to GASP under matched timing. This does not prove an exact causal explanation for earlier editor results, eliminate existing contact defects, establish FP equivalence at fixed timing, or justify claiming every combat behavior is regression-free.

Stage 1 import, standalone CMC validation, saved-scene reload and remote backup are now ready for user review. User approval is still required before stage 2. The next stage must use Manny retargeting, preserve URPGAnimInstance and its proxy, include DefaultSlot AND UpperBody, preserve C++ helper components and Xbox inputs, and keep combat/flight assets unchanged. Retain the existing test defects and timing sensitivity as baseline limitations rather than weakening tests or changing combat source just to pass them.
## Plan revised (2026-10-21, user decision)

The stage-2 safeguards above about Manny retargeting and keeping the combat/flight clips playing are **superseded**. Follow the revised Stages 2-5 in `Docs/GASP_HANDOFF.md`: the player is rebuilt on GASP's CMC character (UEFN skeleton), and the old combat/flight clips are not converted; they will be remade later.
