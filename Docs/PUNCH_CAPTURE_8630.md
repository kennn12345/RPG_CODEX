# Captured punches — IMG_8630

Implemented October 27, 2026 in RPG_CODEX, using the user's phone recording.

## Result and controls

RT releases one right punch; LT releases one left punch. Each hand independently cycles variants 1, 2, 3 within its selected tier. A tap selects Light, a partial charge selects Heavy, and a full charge selects Power using the existing input thresholds. Holding a trigger never automatically releases or repeats punches. Releasing it executes one strike and returns to the approved IMG_8619 stance. Both triggers together retain the unarmed guard and cancel the pending punch without advancing its variant.

There are 18 third-person clips and 18 matched first-person clips in `/Game/RPG/Animations/CapturedPunches`:

`RPG_Punch_{L|R}_{Light|Heavy|Power}_V{1|2|3}` and `RPG_FP_Punch_{L|R}_{Light|Heavy|Power}_V{1|2|3}`.

The three variants retain each hand's attack family and timing, with small hand-arc and hip variations. Heavy punches increase body drive; fully charged Power punches have the greatest hip/head movement and forward extension in every variant. At rest the full body plays; while moving, the existing upper-body layer preserves locomotion. Root translation and rotation remain identity, so actor movement and facing stay under gameplay control. The first-person shadow uses the corresponding body clip.

## Capture and fitting

Source: `C:/Users/cedar/Downloads/IMG_8630.mov`, unchanged.

SHA256: `1B206A40D63D102DFDC7E8A315CFCC3D80E20E885D6950F95C3FD172EF439503`.

The upright reference is sampled at 24 Hz: ordinary punches at 28.8–31.5 seconds, charged left at 63.6–65.7, and charged right at 66.0–68.0. All 164 solved frames have body/raw data. The MetaHuman Markerless solve ran in the isolated chat work project `work/Mocap8630`; its summary reports no start error, although the plugin's processed-frame counter remains zero as on the earlier captures. Frame-to-source timing is checked in as `Tools/Mocap/capture8630_take.json`.

`Tools/Mocap/retarget_capture8630.py` transfers the body motion to the UEFN mannequin. Unreal's frame-rate conversion constraint required a temporary 30 Hz sequence; each pose is evaluated at its temporary frame time and the exported array restores its native 24 Hz timing. The checked-in target transforms are `Tools/ProtoAnim/captures/IMG8630_uefn_retarget.json`. Temporary retarget/plugin assets were moved out of Content into `Saved/Mocap8630/RetargetWorkAssets`. The main project does not acquire a capture-plugin dependency.

`Tools/ProtoAnim/capture_punches8630.py` samples the intervening solved frames, fits their deltas onto the approved stance, repairs rig hinges and foot contacts, and bakes at 60 Hz. Finger tracking is unavailable in this body solve, so the approved closed fists are retained. Physical bone lengths remain fixed; shoulder protraction supplies additional reach without arm stretching. First-person hands are fitted for the camera outside the strike, and the striking wrist/knuckle positions agree with the body clip within 0.1 cm throughout the authored hit window.

| Tier | Clip duration | Impact time | Pose rows |
| --- | ---: | ---: | ---: |
| Light | 0.95 s | 0.30 s | 58 |
| Heavy | 2.20 s | 1.50 s | 133 |
| Power | 2.40 s | 1.50 s | 145 |

All variants share the initial load through 0.12 s. Charged clips share their coil/hold through 1.20 s, with hold beginning at 0.60 s. Heavy/Power release into the strike from 1.20 s; a tap resumes its single Light clip. The held coil can continue indefinitely. Combat stance timeout refreshes on actual release, including after a hold longer than the normal eight-second timeout.

Representative maximum body metrics for variant 1, in centimeters (forward is hand Y in mannequin mesh space):

| Hand / tier | Hip displacement | Head displacement | Forward hand position |
| --- | ---: | ---: | ---: |
| Left Light | 3.569 | 5.982 | 58.808 |
| Left Heavy | 4.391 | 10.135 | 68.287 |
| Left Power | 10.733 | 15.204 | 74.910 |
| Right Light | 3.373 | 8.547 | 64.190 |
| Right Heavy | 5.883 | 9.804 | 66.786 |
| Right Power | 12.666 | 10.997 | 72.971 |

## Integration

`RPGPrototypeAnimationComponent` selects captured punches on SK_UEFN_Mannequin, maintains separate hand/tier counters, and maps all 18 presentation pairs. Existing other-skeleton fallback remains available. Gameplay strike windows, hit radii, damage rules and physics reaction settings are unchanged. `Config/DefaultGame.ini` always cooks the captured-punch folder.

The importer now sets the destination sample rate before editing frame count, avoiding a half-30-Hz-frame ensure when importing a 0.95-second, 60-Hz clip. The import report preserves previous entries and adds these 36 clips (51 total). Only the two approved captured get-ups remain; fighting stance assets, maps and the main .uproject are unchanged.

## Validation and evidence

- UE 5.8 RPG_CODEXEditor Development build succeeded (`Saved/Mocap8630/Build-D.log`). New test-file discovery used `-NoUBTMakefiles`.
- `RPG_SKEL=uefn python Tools/ProtoAnim/check_punches8630.py` passes all 36 clips: anatomy, floor clearance, normalized finite transforms, root identity, fixed bone lengths, exact stance seams, shared load/hold frames, subtle distinct variants, presentation strike agreement and Light < Heavy < Power progression (`Structure-J.log`, `metrics.json`).
- Engine import of all 36 final clips succeeded without errors (`Import-C.log`).
- `RPG.Combat.CapturedPunches` and `CapturedPunchesFirstPerson` pass all 18 cases each, using real Enhanced Input actions. Assertions cover one release/strike window per press, independent variant order, actual clip selection, no firing while held, long full-charge hold, recovery/stance return and guard cancellation (`Gameplay-C.log`, first/third-person result CSVs).
- `RPG.Combat.CapturedStance`, `CapturedStanceFirstPerson`, and `FirstPersonSteadiness` pass in the same final run.
- `Visual.CapturedPunches` passes six rendered examples, one per hand/tier (`Visual-B.log`, `GameplayVisual/captures.csv`). The accompanying MP4 uses the capture timestamps to resample normal-speed playback, and labels variant 1 of 3.
- The existing focused close-range right-Light-punch contact test passes with rendering enabled in both views (`Contact-J.log`): one hit and one impulse each. Maximum fist penetration was 1.3 cm in third person and 0.5 cm in first person; no elbow penetration; contact correction returns to zero. The fixture exists only in editor memory and no map was saved. This is a focused contact check, not a claim that the full legacy combat/kick/Power suite was rerun.

Headless contact runs were inconsistent: third person missed in fast `-nullrhi` runs, while per-frame instrumented headless runs and the rendered run passed with the same final assets. The rendered result is the gameplay evidence; the headless discrepancy is retained in Saved logs rather than treated as a clean pass. Broad aim calibration is outside this animation change.

## Rebake

Use `RPG_SKEL=uefn` to run `capture_punches8630.py`, then `check_punches8630.py`. Import through `ue_import.py` with `RPG_CLIP_FILE` pointing to the generated `capture_punches8630_uefn.json` and `RPG_IMPORT_FOLDER=/Game/RPG/Animations/CapturedPunches`. Preserve the prior report entries when importing a subset. Re-solving the source video is not required to rebake from the checked-in target transforms.
