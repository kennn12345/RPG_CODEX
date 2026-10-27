# Player punch reference — Claude continuation (2026-10-27)

The user resumed this work with Claude: "make these punches look as close to the reference as possible, punches only
(no dodging/weaving), stance may change to suit; full permission; keep going if usage runs out." Reference:
`C:/Users/cedar/Downloads/ScreenRecording_10-27-2026 05-26-11_1.mov`, frames 72-128. Codex's full context is in
`Docs/CLAUDE_PUNCH_HANDOFF.md` and `Docs/PLAYER_REFERENCE_MATCH_2026-10-27.md` (read those first).

## Installed state
- `6ad951f` "Punch reference G" is committed and installed: E's structural fixes (level shoulders, elbow hinge plane at
  full extension, bounded bank) + sideways stance levelling + smooth key timing + charged contact unwind .3 s.
  Tests at G: authoring 39/39, contact 24/24 (a rerun 23/24, see below), controls 4/4, stance/locomotion 2/2,
  visual 2/2 (fixed 60 Hz).
- Working tree after G (version **I**, being tested): bent elbows seeded from the pose's own elbow (fitted) instead of
  E's fixed outside/down seed when >=10 cm off the shoulder-wrist line; right-shoulder cap grows with the right elbow's
  height (4 cm, up to 12 cm while the elbow is cocked up - the filmed cross load), re-applied after the arm solve;
  `check_reference1027.py` shoulder check uses the same allowance.

## What was found
- `Tools/ProtoAnim/overlay_reference1027.py` draws the authored skeleton over the filmed frames through the fitted
  camera and reports distance to the hand-marked joints (pelvis aligned only; diagnostic). Means: AG 23.7 px, E 39.5,
  G 39.9, I 37.3.
- E lost AG's match mostly by capping the right shoulder: from behind/above, a raised shoulder and a further-away
  shoulder look alike on screen, and the marks put the shoulder at the visible trapezius of a bulky mesh - the user's
  "right shoulder extremely high" outranks them. Torso turn (searched per key) did not explain it.
- Extra forward lean (along the chest's facing or the camera's line of sight) moved the head off the filmed head:
  rejected. The "hunched" look of the reference is largely its mesh.
- The guard leaned 13.6 deg sideways (mocap 3.6): levelled to 4.
- Every key used to ease to rest: the fist stopped at in-between keys (stop-and-go). Now one C1 curve, resting only
  where the filmed motion turns round. The jab's wind-up is filmed (f74-76 the arm swings back), and the remaining
  burst (~100 cm in 4 frames, peak 41 cm/frame) is the film's timing.
- Test flags matter: run gameplay tests with `/Game/RPG/Maps/L_DummyField -UseFixedTimeStep -FPS=60 -windowed
  -RenderOffscreen` (30 fps doubles per-frame steps and fails the locomotion pop check). Contact uses the unsaved
  fixture (`Saved/CombatLocomotion/contact_fixture.py`) with RPG_CONTACT_ALL_PUNCH_TIERS=1, RPG_CONTACT_STANDARD_PUNCHES=1.
- Contact is somewhat nondeterministic at close range when the dummy goes limp (its falling arm meets ours): a rerun
  of the same build failed close power-right by 3.2 vs 3 cm forearm. Run contact twice before tuning against a case.
- Build output is written to `ReferencePunchCalibration.inl` by the generator; the generator now writes
  `reference1027_uefn.json` via a temp file (direct writes failed intermittently with Errno 22).

## Version I (committed 2ddaf0a, installed)
All checks pass: authoring 39/39, contact 24/24, controls 4/4, stance/locomotion 2/2, visual 2/2 (fixed 60 Hz).
Review: `Saved/PunchReview/Punches-I-comparison.png` (film / AG / I through the calibrated camera, same moments) and
`Saved/PunchReview/Punches-I.mp4` (gameplay, both views, all hands and tiers).

## Next
- Wait for the user's in-game feedback on I before further changes.
- Remaining visible gaps: the cross load (f88) right elbow sits ~19 cm low (shoulder held down by the cap; allowing
  more brings back the hiked look the user rejected); the film's bulkier mesh and closer camera; charged tiers are
  adaptations (not filmed); the jab's filmed wind-up burst is fast (peak 41 cm/frame).
