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

## State before starting
Branch `gasp-integration`, commit `85affa2`. GASP's 12 get-ups are in use (pose-matched) until ours replace them.
