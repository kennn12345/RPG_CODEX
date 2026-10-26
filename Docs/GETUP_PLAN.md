# Our own dynamic get-ups (RDR2-style) — plan

Agreed 2026-10-26. The user does not like GASP's get-up clips. Goal: like Red Dead Redemption 2 (Euphoria): an active
ragdoll while falling / down, then **our own** get-up animations — the body pushing itself off the ground back to the
standing idle — matched to how the body actually lies, blended from the real ragdoll pose, hands and feet pinned to the
real ground.

## Order
1. **Our own get-up animations (do first).** Authored with the generator (`Tools/ProtoAnim`, `RPG_SKEL=uefn`), in place
   (no root motion), ending exactly in the neutral standing idle (GASP `M_Neutral_Stand_Idle_Loop`, the UEFN dump's
   `idle_local`). Four starts, the common ways a body lies:
   - **Face down:** prone (pelvis pitched ~-88, pelvis ~12 cm up, hands flat by the shoulders, legs back) → push-up
     (chest rises, back arches) → hands and knees (torso level, knees under the hips, shins on the ground) → one foot
     forward (kneel, hand pushes on that knee) → rise → stand.
   - **On its back:** sit up (crunch, hands behind / beside the hips) → roll to one side onto a hand and knee → kneel
     on one knee → rise → stand.
   - **Left / right side:** prop on the lower elbow / hand → hip over → hands and knees or straight to a kneel → stand.
   Timing ~2–3 s, slower variants for hurt (the play rate already slows when hurt).
2. **Use them instead of GASP's:** `URPGBodyImpactComponent::StandUp` already pose-matches any clips (features: head,
   chest, hands, elbows, knees, feet relative to the hips + hip height; candidate start frames over the first 0.6 s) and
   places the capsule from the matched frame. Replace the `GetUps` table entries with our clips (folder
   `/Game/RPG/Animations/SK_UEFN_Mannequin/`, e.g. `RPG_GetUp_FaceDown`), keep `Run` intent if we author a get-up into a
   run later.
3. **Ground contact IK at runtime:** while a get-up plays, pin the hands / knees / feet that the clip has on the ground
   to the actual ground under them (line traces), so slopes and uneven ground fit — new pass in `URPGAnimInstance`'s
   proxy (like `ApplyFlow`), driven by curves or by "near the ground in the clip" detection.
4. **Active ragdoll (later):** while falling / down, PhysicsControl with goals (arms brace toward the ground in the
   direction of the fall using GASP's `M_ragdoll_reach_*` / `BS_Ragdoll_Flail` as targets, protect head, settle);
   strong trunk control, no jitter (lessons: weak motors flop, strong motors on light bodies jitter, body parts must
   not collide with each other during powered control; collisions on only when fully limp).

## Generator notes
- Axes: +Y forward, +X character-left, +Z up (Manny authoring coordinates; protoanim maps hand / foot targets to UEFN by
  shoulder / hip and limb length). `flight_pose()` in poses.py shows how a whole-body tilt is authored.
- Build a NEUTRAL standing pose (no guard: `FLY_FREE` channels; hands hanging at the idle's hand positions; feet at the
  idle's feet) as the last key of every get-up.
- Bake: `RPG_SKEL=uefn python poses.py GetUp`; import `RPG_SKEL=uefn RPG_IMPORT_ONLY=RPG_GetUp` via `ue_import.py`;
  film with `Visual.ClipShots` (`RPG_SHOT_CLIPS=/Game/RPG/Animations/SK_UEFN_Mannequin/RPG_GetUp_FaceDown`).
- In-game check: `RPG.Npc.Collision` (RPG_SHOT_IMPACT=1 films a crash and both get-ups); `RPG.Npc.Smoke`.

## Status (2026-10-26)
- **Steps 1 and 2 done.** `Tools/ProtoAnim/getups.py` authors `RPG_GetUp_FaceDown`, `RPG_GetUp_FaceUp`, `RPG_GetUp_SideL`,
  `RPG_GetUp_SideR` (~3.3 s; targets written straight in UEFN component space, root on the floor; the last 0.35 s blends
  per bone into the idle's exact pose). Face down: push-up -> hands and knees -> kneel upright -> left foot forward ->
  rise. Sides: lower arm out ahead along the floor -> prop on it and the upper hand -> roll onto hands and knees -> the
  face-down finish. Face up: roll over onto the left side (left arm out ahead, right arm and leg over the top) -> as the
  left side. Every clip ends kneeling on the right knee, left foot forward (the idle stands left foot forward).
- **Anatomy check:** `RPG_SKEL=uefn python getupcheck.py` measures every frame: joint ranges (elbows, knees, hips,
  shoulders, wrists, ankles, spine, neck), hinges bending backwards or sideways, and body parts through each other
  (capsules round the bones). All four pass (only <1 deg / ~1 cm at the edges). Run it after any change to the keys.
- New generator channels (protoanim.py): `square` (takes the idle's hip / chest turn out, so a lying body isn't rolled
  onto one side), `pelvis_lie` (roll about the long axis, for lying on a side; the head and feet roll with it, so
  `head_rot` / `foot_rot` stay relative to the lying body), `hand_flat_*` / `hand_dir_*` (palm flat on the floor).
- `StandUp` uses only our four (GASP's 12 are out). Wanting to move with no moving get-up falls back to any.
- Bake: `RPG_SKEL=uefn python getups.py` (prints each key's bone heights and anything below the floor; stick sheets in
  `previews/`). Import: `RPG_SKEL=uefn RPG_IMPORT_ONLY=RPG_GetUp` via `ue_import.py` (merge `ue_import_report_uefn.json`
  afterwards: a partial import overwrites it). Film: `Visual.ClipShots` now films on the first NPC when the level has no
  combat dummy.
- Checked: RPG.Npc.Collision and RPG.Npc.Smoke pass; filmed on the real mesh (no floor penetration in the authored keys).
- Next: step 3 (ground-contact IK while a get-up plays), then a get-up into a run, then step 4 (active ragdoll).

## Smoothness and realism pass (overnight 2026-10-26)
- **Smoothness check:** `python getupsmooth.py` flags per frame: stop-and-go (a part halting between faster stretches),
  jolts, and flips (a bone turning far faster than its neighbours: IK poles or hand directions passing through
  degenerate). Fixed: the lower arm folding shut as it pulls out from under a side-lying body (elbow spun at 11 m/s),
  hand directions blended through zero, the torso tilt dipping and coming back at hands-and-knees, the full stop at the
  kneel, the hips' weight shift going out and back. Planted hands / feet holding still are correct and stay reported.
- **Kneel / rise:** the hands rest on the front of the thighs (the right arm no longer goes behind the back); kneeling
  the chest goes out over the front knee and the hips come forward as the back knee lifts, so the centre of mass is over
  the front foot when the rise starts (it sat midway between the feet: the rise floated); eyes on the floor until up.
- **Overlap:** springs on the head and chest (trail the hips a little and settle).
- **Start variants** (closer matches, and variety): FaceDownArmsSide, FaceDownArmsUp, FaceDownFrog, FaceUpArmsOut,
  FaceUpArmsUp, SideLCurled, SideRCurled - the same get-ups from other ways a body lands (`variant()` in getups.py).
- **In game** (`RPG.Npc.Collision` with `RPG_GETUP_TRACE=1` -> `Saved/GetUpTrace.csv`; `python tracesmooth.py`,
  `python tracecarriers.py`):
  - the limp ragdoll kept simulating through the fade into the get-up, so its parts jittered on the ground and popped
    out of each other; now each body is pinned kinematic where it lies for the fade (`ReturnToAnimated`). (Static
    bodies drop out of the physics blend: the pose snapped to the animation.)
  - the clips were loaded and measured on the first get-up (a 21 ms frame); now at BeginPlay.
  - a start whose limb points far from the ragdoll's whips round through the fade (half way the shortest turn flips
    sides, frame to frame): the match now also compares limb segment directions, skips starts any segment would turn
    more than 110 deg to (unless none is closer), scores turn as well as distance, and fades a big turn in more slowly
    (0.5 s up to 1 s).
  - landings no start is close to are written to `Saved/GetUpMisses.csv` (the body parts in the matching frame) to
    author new starts from.
- Pipeline used overnight: `getups.py` -> `getupcheck.py` -> `getupsmooth.py` -> import -> build -> film
  (`Visual.ClipShots`, sheets in `Saved/GetUpSheets/`) -> trace -> `RPG.Npc`.
