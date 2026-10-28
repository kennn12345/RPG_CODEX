# Filmed fight stance — IMG_8635

Implemented October 28, 2026, at the user's request ("simple fight stance"). The game now plays this stance instead of
the hand-edited `RPG_FightStance`, which is kept unchanged in the project.

## Result

- Third person: `/Game/RPG/Animations/FightStance/RPG_FightStance_Capture8635`.
- First person: `/Game/RPG/Animations/FightStance/RPG_FP_FightStance_Capture8635`.
- 269 frames at 60 Hz (4.47 s loop). Square guard as recorded: fists at the cheeks, elbows down, upright.
  Identity root, fixed pelvis/facing, planted feet, exact loop seam. Fingers use the rig's closed fists
  (body tracking does not solve fingers).
- Footprint from the median of four held directions (front/side/back/opposite, headings 97/5/-83/-169 degrees):
  31.1 cm sideways, 5.9 cm stagger. Turns are not played back.
- No hand, elbow or foot offsets (unlike the later 8619 tweaks). The recorded left elbow was ~18 degrees off its hinge;
  `repair_chain` re-seats both elbows on the rig hinge with each hand left exactly where it was recorded.
- `RPGPrototypeAnimationComponent.cpp` default `CombatStance` / `FirstPersonCombatStance` point at the new clips.

## Capture and rebake

Original: `C:/Users/cedar/Downloads/IMG_8635.mov` (1920x1080 HEVC, stored sideways, 53.6 s), preserved unchanged.
SHA256: `7E9F61AB83A414B1C4965A7288426595BB32035B08CC3EEB325A6440A25F306B`.

1. Frames 0.5–52.0 s at 15 Hz, rotated upright (773 frames), into the isolated capture project
   `C:/Users/cedar/Documents/Codex/2026-10-26/ok-x20/work/Mocap8635` (`prepare8635.py` next to it).
2. MetaHuman Animator Markerless Motion Capture 1.0.0 (full editor, `editor_solve.py`): 773/773 valid body frames,
   no pipeline error, ~7.7 min. The processed-frame counter reads zero, as with prior takes.
3. `Tools/Mocap/retarget_capture8635.py` with session-only `"-EnablePlugins=MetaHumanBodyTracker,MetaHuman"` (quote it
   in PowerShell) and `RPG_MOCAP_BODY_JSON`. The handled `bValidTime` ensure (non-zero exit) also occurred for 8619;
   all 773 output frames are finite and normalized. Work assets moved to `Saved/Mocap8635/RetargetWorkAssets`.
   Checked-in target data: `Tools/ProtoAnim/captures/IMG8635_uefn_retarget.json`.
4. `RPG_SKEL=uefn python capture_stance8635.py` (loop window 4–9 s), then `check_stance8635.py`: both clips PASS
   structure and full anatomy checks.
5. Import with `ue_import.py`, `RPG_IMPORT_FOLDER=/Game/RPG/Animations/FightStance`,
   `RPG_CLIP_FILE=<capture_stance8635_uefn.json>`, `RPG_IMPORT_REPORT=Saved/Mocap8635/import-report.json`.

## Verification

Evidence in `Saved/Mocap8635/`: `Retarget.log`, `Import.log`, `Build.log` (succeeded), `Gameplay.log`.
`RPG.Combat.StationaryStanceLocomotion`, `RPG.Combat.StationaryStanceLocomotionFirstPerson` and
`Visual.StationaryStanceLocomotion` all PASS; rendered frames show the square guard at rest and the upper-body guard
while running. `compare.png` puts the video next to the Blender render; `Blender/RPG_FightStance_Capture8635.blend`
holds the clip on the player mesh for review.
