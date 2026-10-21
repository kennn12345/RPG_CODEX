# RPG_CODEX — Project Status Report

*Written 2026-10-20 by read-only inspection of the files on disk: source, config, asset names and binary string references, logs, build output, test results and checkpoint notes. Nothing was changed, committed or rebuilt. The build check reported "Target is up to date" and compiled nothing; see §5.*

**How to read the evidence in this report**
- I wrote this in a fresh session. It is **not** based on memory of earlier conversations, and some earlier work was clearly done by a different AI agent (see §7).
- **"Tested in game"** means the editor logs (`Saved/Logs/RPG_CODEX*.log`) show a *manual* Play-In-Editor session, launched with an empty command line, in which that action fired. The logs record that something happened. They don't record whether the user was happy with how it looked.
- Only logs from **2026-10-20 03:38 local onward** (plus one from 10-17) survive. Manual testing before that left no evidence here.
- `.uasset`/`.umap` files are binary. I **could not open Blueprints, the AnimBP graph or the level**. I could only scan them for referenced names. Anything I say about their insides is marked as inferred.

---

## 1. PROJECT BASICS

| Item | Value |
|---|---|
| Project folder | `C:\UNREAL PROJECTS\RPG_CODEX` |
| .uproject | `C:\UNREAL PROJECTS\RPG_CODEX\RPG_CODEX.uproject` |
| EngineAssociation | `"5.8"`. The editor log reports **5.8.3-58210709 (UE5 Release-5.8)**, installed at `C:\Program Files\Epic Games\UE_5.8` |
| Project name | RPG_CODEX. Note: `DefaultEngine.ini` still says `ProjectName=Third Person BP Game Template` |
| C++ module | `RPG_CODEX` (Runtime, Default loading phase), the only module |
| Targets | `RPG_CODEX` (Game) and `RPG_CODEXEditor` (Editor), both `BuildSettingsVersion.Latest` |
| Origin | UE Third Person **Blueprint** template, converted to C++ on 2026-10-17. `RPG_CODEX.uproject.backup-before-cpp` is the pre-conversion file. |
| Toolchain | VS 2026 (v18) MSVC 14.51.36260. UBT warns it is "newer than latest preferred version 14.50.35717". |

### Plugins
Listed in `RPG_CODEX.uproject` (there are **no project `.uplugin` files**; the project has no `Plugins/` folder):

| Plugin | Enabled | Notes |
|---|---|---|
| ModelingToolsEditorMode | Yes (Editor only) | From the template |
| GameplayStateTree | Yes | **Not used.** No StateTree assets or code exist. |
| PhysicsControl | Yes | Used by `URPGPhysicsReactionComponent` |

The other plugins you asked about:
- **GameplayAbilities (GAS): NOT enabled.** It isn't in the .uproject or `Build.cs`, and nothing references it (see §3).
- **EnhancedInput:** not listed in the .uproject because it is engine-default enabled in UE5. It is a dependency in `RPG_CODEX.Build.cs` and the log shows it loaded.
- **PythonScriptPlugin:** not listed in the .uproject. The editor log shows it loads from the engine's Experimental folder and says `Python enabled via CVar 'Engine.Python.IsEnabledByDefault'` (Python 3.11.8). The `Tools/ProtoAnim/ue_import.py` animation importer depends on it.

`Build.cs` dependencies: Public = Core, CoreUObject, Engine, InputCore, EnhancedInput, PhysicsCore, PhysicsControl, AnimationCore. Editor-only private = UnrealEd, AnimGraph, AnimGraphRuntime, AssetTools.

---

## 2. PROJECT STRUCTURE

### 2.1 C++ (`Source/RPG_CODEX/`, one module, about 400 KB of source)

**Runtime gameplay classes**

| File(s) | Class / type | What it does |
|---|---|---|
| `RPG_CODEX.h/.cpp` | module + `LogRPGInput` | Primary game module. Declares the `LogRPGInput` log category that all gameplay logging uses. |
| `RPGPlayerTypes.h` | enums + structs | Shared types. `ERPGHandItemType`, `ERPGCombatMovementState`, `ERPGHand`, `ERPGKickLeg`, `ERPGInteractionType`, `ERPGPlayerAction` (about 50 actions), `ERPGAttackPhase`, `ERPGStrikeLimb`, plus the structs `FRPGPlayerActionInfo` and `FRPGMeleeHitInfo` (the full hit record meant for a future Power System). |
| `RPGControllerInputComponent.h/.cpp` | `URPGControllerInputComponent` | **The entry point.** It is the only RPG component added to `BP_ThirdPersonCharacter`. At BeginPlay it creates every other RPG component, builds **17 Input Actions and 2 Mapping Contexts in code** (`IMC_RPGController` and a camera-adjust context; these are not assets), binds the **Xbox gamepad** buttons, works out tap / heavy / power from hold time, and sets the HUD to `ARPGHUD`. |
| `RPGCombatStateComponent.h/.cpp` | `URPGCombatStateComponent` | Central state bag: equipment types, blocking, sprint/fly/crouch/air flags, charge %, attack phase, hit window, strike limb, aim. `ReportAction()` broadcasts `OnPlayerAction`. Draws the debug text and state panel (console var `rpg.Debug`, default 0). |
| `RPGMovementAbilityComponent.h/.cpp` | `URPGMovementAbilityComponent` | Placeholder sprint (speed change), crouch toggle, flight (`MOVE_Flying` + hover), dodge / air evade (a velocity "shove" with cooldown, no animation) and charged jump. |
| `RPGCameraModeComponent.h/.cpp` | `URPGCameraModeComponent` | First/third-person toggle (D-Pad Right tap). Hold D-Pad Right + Right Stick changes the camera distance. First person hides `neck_01` and below, follows part of the head motion with smoothing, switches clips to the FP versions, and spawns a **shadow-only second skeletal mesh** so the player still casts a full-body shadow. Third-person combat adds a shoulder offset. |
| `RPGCombatAimComponent.h/.cpp` | `URPGCombatAimComponent` | Look-directed attacks. Traces from the camera through the reticle, clamps torso yaw/pitch, and turns the body when the aim goes past the torso limit. Writes the aim to the combat state and the anim instance. |
| `RPGHUD.h/.cpp` | `ARPGHUD` | Canvas reticle: a centre dot, plus a ring while fighting that fades when out of reach. Console var `rpg.Reticle`. This is the **only UI** in the project. |
| `RPGInteractable.h` | `IRPGInteractable` | Interface: `CanInteract`, `Interact`, `GetInteractionType` (Bed enables Sleep). |
| `RPGInteractionComponent.h/.cpp` | `URPGInteractionComponent` | Sphere overlap (200 cm, facing check) to find the best `IRPGInteractable`. Pressing A interacts before it jumps. |
| `RPGMeleeTarget.h` | `IRPGMeleeTarget` | Interface: `ReceiveMeleeHit`, `ReceiveContactPush`. |
| `RPGMeleeHitComponent.h/.cpp` | `URPGMeleeHitComponent` | Unarmed hit detection. While the hit window is open, it sweeps spheres on the striking fist/foot between frames against Pawn/PhysicsBody objects, allows one hit per target per strike, and fills in `FRPGMeleeHitInfo`. It does no damage. Console var `rpg.MeleeDebug`. |
| `RPGStrikeContactComponent.h/.cpp` | `URPGStrikeContactComponent` | Gathers the target surfaces each limb is touching (latches, planes), separates the capsule a little, and sends contact pushes. It feeds the anim proxy's contact IK. **This is the system that was being worked on last (§5).** |
| `RPGAnimInstance.h/.cpp` | `URPGAnimInstance` + `FRPGAnimInstanceProxy` | Parent class of `ABP_Unarmed`. After the Blueprint graph has evaluated, it applies spine aim (yaw/pitch), first-person guard steadying, and **strike contact**: two-bone IK that stops a fist or foot at the target's surface. It also mirrors montages onto the FP shadow body. |
| `RPGPrototypeAnimationComponent.h/.cpp` | `URPGPrototypeAnimationComponent` | The largest file (about 59 KB). Listens to `OnPlayerAction` and plays the prototype punch, kick, guard and stance montages, either full body or on the `UpperBody` slot when moving. Handles the charge-hold logic (switching to the Charged clip and its breathing loop), attack phases, hit-window timing, the movement lock during kicks, the combat-stance timeout, FP clip swapping and FP shadow mirroring. Also has editor-only static helpers: `PreparePrototypeAnimations`, `SetupUpperBodySlot`, `SetupAimAnimInstance`, `SetClipSampleRate`. |
| `RPGPhysicsReactionComponent.h/.cpp` | `URPGPhysicsReactionComponent` | Physical hit reactions through PhysicsControl, with states ANIMATED → POWERED (motors pull toward the animation, the struck limb weakens) or LIMP (full ragdoll, entered when charge ≥ 0.99), then back to ANIMATED. Impulse values are marked **TEMPORARY** placeholders for the Power System. |
| `RPGCombatTestDummy.h/.cpp` | `ARPGCombatTestDummy` | Punching-bag actor: capsule, skeletal mesh with `PA_Mannequin`, and a PhysicsReaction component. It wanders around its spawn point with a locomotion blend space, gets shoved by hits and contact pushes, and shows a floating hit counter. It has no AI and no health. |

**Automation tests** (editor-only, `WITH_DEV_AUTOMATION_TESTS`; they play the startup map and inject the real input actions):

| File | Test name(s) | What it checks |
|---|---|---|
| `RPGCombatDummyTest.cpp` | `RPG.Combat.TestDummy`, `…TestDummyFirstPerson` | Tap/hold RT/LT/Y at the dummy; the expected hits, attack names, charge, aim, stance and shadow |
| `RPGDummyWanderTest.cpp` | `RPG.Combat.DummyWander` | The dummy walks, stays on the floor, then is hit once |
| `RPGPhysicsReactionTest.cpp` | `RPG.Combat.PhysicsReaction` | hit → POWERED/LIMP → impulse → back to ANIMATED |
| `RPGStrikeContactTest.cpp` | `RPG.Combat.StrikeContact`, `…StrikeContactFirstPerson` | Per-frame limb penetration, jitter, release, segment length, joint fold. Env vars `RPG_CONTACT_ORDINARY` etc. select subsets. |
| `RPGFirstPersonSteadinessTest.cpp` | `RPG.Combat.FirstPersonSteadiness` | FP camera shake and fist jitter |
| `RPGFirstPersonGuardShots.cpp` | `Visual.FPGuardShots` | Needs rendering. Takes FP guard screenshots and logs `FPGUARDTRACK` |

### 2.2 Content (`Content/`, about 814 MB, 609 files)

Top two levels:
```
Content/
├─ Characters/Mannequins/        (template: Anims, Meshes, Materials, Rigs, Textures)
├─ Collections/                  (empty)
├─ Developers/CodexSandboxOffline/, Developers/cedar/   (both empty)
├─ FreeAnimationsPack/           (Animations, Demo, Map) — marketplace/Fab pack
├─ Input/                        (Actions, Touch) — template input
├─ LevelPrototyping/             (Interactable, Materials, Meshes, Textures) — template
├─ RPG/                          (Animations, Combat) — project's own assets
├─ ThirdPerson/                  (Blueprints, Lvl_ThirdPerson.umap)
├─ __ExternalActors__/           (ThirdPerson, VehicleTemplate)
└─ __ExternalObjects__/          (ThirdPerson, VehicleTemplate)
```

**Maps:**
- `/Game/ThirdPerson/Lvl_ThirdPerson`: the startup and default map, World Partition with about 25 external actors. The PIE log confirms it contains one `CombatTestDummy`. An earlier script report places the dummy at about (350, 0, 302) on a raised platform facing the player start. I can't see anything else in the level.
- `/Game/FreeAnimationsPack/Map/Overview`: the pack's demo map.
- `__ExternalActors__/VehicleTemplate/Maps/VehicleExampleMap/…`: **orphaned** external actors (about 30 files) for a map that does not exist in Content.

**Blueprints** (names found by scanning the binaries; I couldn't read the graphs):
- `ThirdPerson/Blueprints/BP_ThirdPersonCharacter`: references `RPGControllerInputComponent` (the RPG hook), `ABP_Unarmed`, `SKM_Manny_Simple`, and the template `IA_Move/IA_Look/IA_MouseLook/IA_Jump`.
- `ThirdPerson/Blueprints/BP_ThirdPersonGameMode`: default pawn is `BP_ThirdPersonCharacter`, controller is `BP_ThirdPersonPlayerController`.
- `ThirdPerson/Blueprints/BP_ThirdPersonPlayerController`: adds `IMC_Default` and `IMC_MouseLook`. It also references touch UI widgets from the template.
- `RPG/Combat/BP_CombatTestDummy`: a Blueprint subclass of `ARPGCombatTestDummy`.
- Template interactables in `LevelPrototyping/Interactable/` (`BP_DoorFrame`, `BP_JumpPad`, `BP_WobbleTarget`). None of them implement `IRPGInteractable`; no asset in Content references that interface at all.

**Input:**
- Template assets: `Input/Actions/IA_Jump`, `IA_Look`, `IA_MouseLook`, `IA_Move`, and `Input/IMC_Default`, `Input/IMC_MouseLook`.
- **All RPG gamepad actions are created in C++ at runtime**, so they have no `.uasset`: IA_RightAttack, IA_Defense, IA_RightSpell, IA_LeftSpell, IA_Kick, IA_Dodge, IA_JumpInteractFlight, IA_QuickConsumable, IA_Sprint, IA_CrouchStealth, IA_QuickMenu, IA_DrawSheathe, IA_CameraButton, IA_WaitSleep, IA_Journal, IA_MainMenu, IA_CameraAdjust, in the contexts IMC_RPGController (priority 1) and the camera-adjust context (priority 10).
- **There are no keyboard/mouse bindings for any RPG action.** Keyboard and mouse only drive the template's move, look and jump.

**Animation Blueprints:**
- `Characters/Mannequins/Anims/Unarmed/ABP_Unarmed`: the player's AnimBP. Its parent class has been changed to `URPGAnimInstance`, and it has an added `UpperBody` slot layered from `spine_01`. A `RPG_BeforeUpperBody` name appears inside it, probably a saved/renamed node or state. The pre-change copy is in `Saved/Backups/BeforeUpperBodySlot_20261019_102303/`.
- `FreeAnimationsPack/Demo/.../ABP_Manny`, `ABP_Quinn` and the PostProcess AnimBPs come from the pack and are not used by the player as far as I can tell.

**Project animation assets** (`RPG/Animations/Prototype/`):
- `RPG_Proto_*`: third-person procedural clips. Punch_R/L, Punch_R/L_Charged, FrontKick_R/L, FrontKick_R/L_Charged, CombatStance, UnarmedGuard.
- `RPG_FP_*`: first-person versions of the same 10 clips.
- `RPG_FlightStart`, `RPG_FlyingAttack`, `RPG_ComboHands`, `RPG_RightLegKick`: copies of FreeAnimationsPack clips (`AS_FlyingUp`, `AS_AttackInLevitation2`, `AS_ComboHands`, `AS_RightLegKick2`), copied by `PreparePrototypeAnimations()`.

These are generated by the Python tools in `Tools/ProtoAnim/` (`poses.py` → `clips.json` → `ue_import.py`). See `Tools/ProtoAnim/README.md`.

**Data Assets / Data Tables / Widget Blueprints: none exist.** All tuning lives in C++ `UPROPERTY` defaults.

**Tools outside Content:** `Tools/ProtoAnim/` holds about 20 Python scripts: the generator, importer, and diagnostics (`analyze`, `smoothness`, `twist`, `clipcheck`, `stancecheck`, `fpsym`, `motioncheck`, `verify_saved_clips`), plus preview PNGs, including some `*_OLD.png` files.

---

## 3. GAMEPLAY ABILITY SYSTEM (GAS)

**GAS does not exist in this project.**

| GAS piece | Status | Location |
|---|---|---|
| GameplayAbilities plugin | Not enabled | — |
| AbilitySystemComponent | None | — |
| AttributeSets / attributes | None | — |
| GameplayAbilities | None | — |
| GameplayEffects | None | — |
| Gameplay Tags | None (no tags in Config, none in code) | — |

I checked by searching `Source/`, `Config/`, the .uproject and the Content binaries for `AbilitySystem`, `GameplayAbilit`, `AttributeSet`, `GameplayEffect` and `GameplayTag`. There were zero hits.

What the project uses instead: its own component architecture. Input decides *which* action happened (`ERPGPlayerAction`). `URPGCombatStateComponent::OnPlayerAction` broadcasts it. Listeners decide *how it looks* (`URPGPrototypeAnimationComponent`) and *whether it hit* (`URPGMeleeHitComponent` → `FRPGMeleeHitInfo` → `IRPGMeleeTarget`). There is **no health, damage, stamina or any other attribute anywhere.**

---

## 4. SYSTEM STATUS

Labels: **WORKING** = fired in a manual in-game PIE session (log evidence), with the caveats noted. **COMPILES BUT UNTESTED** = code exists and builds, but there is no manual-play evidence. **SCAFFOLDED ONLY** = only an action name, enum or debug print exists. **NOT STARTED** = no code.

| System | Status | Evidence / notes | Key files |
|---|---|---|---|
| **Input / controller mapping (Enhanced Input)** | **WORKING** for the buttons used in play (RT, LT, RT+LT, Y, RB, LB, D-Pad Right tap/hold, D-Pad Up) | Manual PIE logs show these turned into the correct actions. Gamepad only. Untested in retained logs: B, X, A (beyond 1 Jump), stick clicks, D-Pad Left/Down, View, Menu. | `RPGControllerInputComponent.*`, `RPGPlayerTypes.h` |
| **Combat: unarmed punches, tap / heavy / power charge** | **WORKING mechanically.** Visual contact quality is **NOT accepted**. | Dozens of manual hits, including Heavy and Power attacks, Charged Right Punch, and POWERED/LIMP reactions. Close-range limb penetration and jitter is the open problem (§5). `TestDummy` tests passed in the last full-suite run (on an experimental version that has since been reverted). | `RPGPrototypeAnimationComponent.*`, `RPGControllerInputComponent.*`, `RPGMeleeHitComponent.*` |
| **Combat: kicks (alternating legs, charge)** | **WORKING mechanically.** Visuals have **known issues**. | Manual Kick, Heavy Kick and Power Kick hits are in the logs. The knee contact-IK deformation is explicitly unresolved, and kicks were not re-tested after the last fix. | same files + `RPGStrikeContactComponent.*`, `RPGAnimInstance.cpp` |
| **Combat: unarmed guard (RT+LT)** | **WORKING (visual only)** | "Unarmed Guard" fired in several manual sessions and plays the guard clip. **It blocks nothing**, because no damage exists. | `RPGPrototypeAnimationComponent.*` |
| Combat: weapon block / shield block / shield bash / bow aim / dual-guard with weapons | **SCAFFOLDED ONLY** | The logic branches on `ERPGHandItemType`, but **no weapons exist**. The player is always unarmed. They only report actions. | `RPGControllerInputComponent.cpp`, `RPGCombatStateComponent.*` |
| Combat: context attacks (Blitz/sprint, Aerial, Flying) | **SCAFFOLDED ONLY** | The action names resolve (one "Aerial Attack" in the logs), but only FlyingAttack has a clip mapped, and that was never tested. | `RPGControllerInputComponent.cpp:~310-330` |
| Combat: dodge / air evade (B) | **COMPILES BUT UNTESTED** | A placeholder velocity shove with cooldown and no animation. No log evidence. | `RPGMovementAbilityComponent.*` |
| Melee hit detection | **WORKING** | Every manual HIT line shows the bone, limb, impact point and charge. | `RPGMeleeHitComponent.*` |
| Look-directed attack aim + reticle | **WORKING** (exercised) | Hits show varying aim yaw/pitch, and the reticle HUD is active. Visual quality isn't recorded. | `RPGCombatAimComponent.*`, `RPGHUD.*`, `RPGAnimInstance.cpp` |
| Strike contact (fist/foot stops at the target's surface) | **WORKING, BUGGY.** It runs in every strike; its quality is the open problem. | Only one case (close right punch) is verified after the latest change. See §5 and §7. | `RPGStrikeContactComponent.*`, `RPGAnimInstance.cpp` |
| **Power System** | **NOT STARTED** | Only hooks exist: `FRPGMeleeHitInfo`, `OnMeleeHit`, `AttackPhase`, `ChargePercent`, and the impulse values marked "TEMPORARY". No damage, stagger, stamina or power stats. | — |
| **Physics reactions / knockback / ragdoll** (on the dummy) | **WORKING** (POWERED and LIMP) | Manual logs show many `PHYSICS REACTION … state POWERED` lines and dozens of `state LIMP` lines. The dummy also gets a shove/slide. No get-up animation; it fades back. The `PhysicsReaction` automation test **failed** (motor return) on the reverted experimental version. Its result on the current code is unknown. | `RPGPhysicsReactionComponent.*`, `RPGCombatTestDummy.*` |
| Physics reactions on the player | **NOT STARTED** | The player has zero simulated bodies. | — |
| **Animation setup** | **WORKING** for the unarmed set | Montages play in TP and FP, the UpperBody slot works while moving, and there are charge holds, a combat stance, and an FP shadow body. The procedural clips come from the Python pipeline. Template locomotion is unchanged. | `RPGPrototypeAnimationComponent.*`, `RPGAnimInstance.*`, `ABP_Unarmed`, `Tools/ProtoAnim/` |
| Camera: FP/TP toggle, distance adjust | **WORKING** | Many manual "Camera Perspective" and "Camera Adjust Mode On" events. `FirstPersonSteadiness` test passed. | `RPGCameraModeComponent.*` |
| **Flight** | **COMPILES BUT UNTESTED** | A while airborne → `MOVE_Flying` hover; A again cancels. No flight speed boost ("not built yet"). No "Flight Started" in any retained log. | `RPGMovementAbilityComponent.*`, `RPGControllerInputComponent.cpp` |
| Sprint / crouch / charged jump | **COMPILES BUT UNTESTED** | Placeholders, no log evidence (one plain Jump only). | `RPGMovementAbilityComponent.*` |
| **Magic / spells** (RB/LB) | **SCAFFOLDED ONLY** | The buttons report "Right Spell"/"Left Spell" (seen in the logs). Nothing else happens. | `RPGControllerInputComponent.cpp` |
| **NPCs / AI** | **NOT STARTED** | The only other character is the test dummy: random wander, no AI controller, BT or StateTree. | `RPGCombatTestDummy.*` |
| **Interaction (A button)** | **COMPILES BUT UNTESTED** | The interface and finder exist, but **no actor in Content implements `IRPGInteractable`**, so nothing can be interacted with. | `RPGInteractable.h`, `RPGInteractionComponent.*` |
| UI | **SCAFFOLDED ONLY** | Only the reticle and debug text. Quick Menu, Journal and Main Menu just print/log. No widgets. | `RPGHUD.*` |
| Inventory / equipment | **SCAFFOLDED ONLY** | Just the `ERPGHandItemType` enums and a `bWeaponsDrawn` toggle. | `RPGCombatStateComponent.*` |
| Consumables (X), Wait/Sleep (D-Pad Down) | **SCAFFOLDED ONLY** | They report actions only. | — |
| Save / load | **NOT STARTED** | — | — |
| Automation test suite | Exists, results mixed | See §5. | `*Test.cpp`, `RPGFirstPersonGuardShots.cpp` |

---

## 5. CURRENT STATE

### 5.1 Does it compile?
**Yes**, but read the details:

- The build I ran just now, `Build.bat RPG_CODEXEditor Win64 Development -Project=…RPG_CODEX.uproject`, returned:
  ```
  Target is up to date
  Using Unreal Build Accelerator local executor to run 0 action(s)
  Result: Succeeded
  Total execution time: 1.67 seconds
  ```
  It compiled **nothing**. On its own, that proves little: earlier notes say a stale-timestamp build once fooled them.
- Two further checks:
  - I hashed **all 40 source files** and the editor DLL against `Saved/Backups/AuthoredElbowVerified_20261020_1823/VERIFICATION.json`. **0 mismatches, and the DLL hash matches.**
  - That DLL was produced by a real compile and link (`Saved/Logs/AuthoredElbowBuild.stdout.txt`: `[1/4] Compile [x64] RPGAnimInstance.cpp … Link UnrealEditor-RPG_CODEX.dll … Result: Succeeded`). The full 15-action recompile just before it (`VerifiedRestoredBuild.stdout.txt`) also succeeded.
  - So the current source compiles, and the DLL on disk was built from it.
- I deliberately did **not** run a clean rebuild, so as not to risk leaving the project without a DLL.
- **Warnings** in the last full compile come from engine headers only, not project code:
  - `Character.h(798,31): warning C4996: 'APawn::GetMovementBase': GetMovementBase is deprecated. Use GetMovementBaseObject or GetMovementBaseInterfaceData instead.` (×10)
  - `Chaos/Sphere.h(74,17)`, `Convex.h(212,17)`, `Plane.h(42,16)`, `Box.h(104,17)`: `warning C4996: 'Chaos::FImplicitObject::GetRadius': Please use GetRadiusf instead.` (×2 each)
  - `Chaos/Convex.h(207,17): warning C4996: 'Chaos::FImplicitObject::GetMargin': Please use GetMarginf instead.` (×2)
  - `Warning: Visual Studio compiler 14.51.36260 is not a preferred version`
  - After a successful build, UBT sometimes logs `ERROR opening file C:\Users\cedar\AppData\Local\UnrealBuildTool\Trace.uba for write … (Access is denied.)`. This is harmless; the DLL still links.

### 5.2 Known bugs / things that broke recently
1. **Strike-contact limb deformation and penetration.** This is the main open bug. Fists and feet sink into the dummy or "crumple" at close range, and the correction jitters.
   - Before the latest change, the verified-restored code passed **7 of 16** "ordinary" contact cases at 120 Hz (`Saved/SimplifiedContactPass.md`, though that run's binary was later found to be stale).
   - The failures included: TP right punch close, fist 15.8 cm inside `spine_05`; FP right kick close, foot 12.6 cm inside `pelvis` with an 8.4 cm one-frame snap; and many jitter/reversal failures.
2. **Latest fix (2026-10-20 ~18:20):** `RPGAnimInstance.cpp` now **disables the surface-driven pole steering for arms**, so elbows keep the authored pole. Knees keep the steering.
   - It fixed a measured elbow bend reversal of up to 179.5°.
   - It was verified on **one case only** (close right punch, TP + FP): `StrikeContact` and `StrikeContactFirstPerson` PASS, filtered to that case (`Saved/Logs/AuthoredElbowClosePunch.log`, `Saved/ClosePunchDiagnosis.md`).
   - **Not re-tested:** left punch, other ranges, kicks, charged strikes, rapid strikes. **Knee deformation on kicks is unresolved.**
3. **Last full automation run** (`CombatContactFinalSuite`, 17:37, on an **experimental version that was later reverted**):
   - FAIL: `DummyWander`, `PhysicsReaction`, `StrikeContact`, `StrikeContactFirstPerson`.
   - PASS: `FirstPersonSteadiness`, `TestDummy`, `TestDummyFirstPerson`.
   - **No full-suite run exists for the current code.**
4. **Crashes in `Saved/Crashes/`:**
   - 10-19 ~13:09: twice, `Assertion failed: FMath::IsNearlyZero(SampleFrameTime.GetSubFrame()) [AnimCompressionTypes.cpp:993]`. Caused by clip lengths that weren't whole 30 fps frames. The README says the importer now pads lengths to avoid this.
   - 10-20 16:24: a DerivedDataBackends.cpp:813 fatal.
   - 10-20 16:31: a ShaderCompilerThreadRunnable.cpp:707 fatal.
   - Neither of the 10-20 fatals has been explained. Both happened during the heavy automated-test period.
5. The last manual session (10-20, 18:35–18:36 local) ran cleanly with no errors from project code, and the editor exited normally. That session was close-range punches and kicks in TP/FP. Its log is `Saved/Logs/RPG_CODEX.log`.

### 5.3 What was being worked on most recently
- **Player strike contact**, meaning making fists and feet meet the dummy's body believably without passing through it or folding the arm or leg.
- The sequence on 10-20, according to the notes in `Saved/`:
  1. A broad "coordinated contact pipeline" rewrite (16:23–17:43). It **regressed** most cases and was paused.
  2. A time-boxed simplified-contact attempt (17:49–18:08). It was **rejected**, and the original runtime was restored.
  3. A verified rebuild of the restored code (18:10).
  4. A focused diagnosis of the close right punch, which found the elbow pole reversal (18:12–18:21).
  5. The single retained fix: arms keep the authored elbow pole (18:20–18:24).
  6. The user then played it manually at 18:35.
- **Where it was left:** `Saved/CONTACT_CURRENT.md` says the current version is `AuthoredElbowVerified_20261020_1823` and "ready-to-test". The suggested next step is to check the other attacks, especially kicks (knee steering is unchanged), with one isolated change at a time. **I found no record of the user's verdict on the 18:35 playtest.**

---

## 6. VERSION CONTROL / BACKUP

- **This is NOT a Git repository.** `git rev-parse` reports `fatal: not a git repository (or any of the parent directories)`. The parent folder `C:\UNREAL PROJECTS\` is not one either. There is no branch, no commit history and no git status.
- `Saved/SourceControl/UncontrolledChangelists.json` exists, but that is just Unreal's local bookkeeping. No source control provider is configured.
- **The only backups are manual folder copies, all inside the project folder.** Nothing is off-machine. If the disk or folder is lost, everything is lost.

| Backup | Contents |
|---|---|
| `Saved/Backups/AuthoredElbowVerified_20261020_1823/` (468 MB) | **Current version.** Source, Binaries, Config, Tools, .uproject, plus `REPORT.md`, `RESTORE.txt`, `VERIFICATION.json` (hashes) and the diff of the last change |
| `Saved/Backups/VerifiedRestored_20261020_181043/` | Source and Binaries just before the elbow fix (verified build) |
| `Saved/Backups/RestoredFoundation_20261020_1807/` | **Stale binary, do not use the DLL** (see `REBUILD_REQUIRED.txt`) |
| `Saved/Backups/BoundedContact_20261020_1805/` | The **rejected** simplified-contact candidate (`REJECTED.txt`) |
| `Saved/Backups/CombatContactProgress_20261020_1742/` | The paused experimental pipeline (regressed) |
| `Saved/Backups/ContactWorkBeforeCoordinatedPipeline_20261020_171917/` | Intermediate experimental state |
| `Saved/Backups/BeforeContactDeformationFix_20261020_162350/` (923 MB) | **The only backup that includes Content.** The state before the 10-20 contact work |
| `Saved/Backups/BeforeUpperBodySlot_20261019_102303/` | `ABP_Unarmed.uasset` before the UpperBody slot |
| `Saved/Backups/BeforeRootOrientationFix_20261018_011428/` | The 4 FreeAnimationsPack-derived RPG clips before the root fix |
| `Saved/Backups/BeforePrototypeAnimations_20261017_140452/` | `RPGControllerInputComponent.cpp` and `Build.cs` from 10-17 |
| `Tools/_backup_player_mesh_2026-10-20/BP_ThirdPersonCharacter.uasset` | The character BP before some player-mesh change on 10-20 |
| `RPG_CODEX.uproject.backup-before-cpp` | The .uproject before C++ conversion |

**Strong recommendation for the next session:** `git init` with a UE `.gitignore` (excluding Binaries, Intermediate, Saved, DerivedDataCache), plus Git LFS for `.uasset` and `.umap`, and push it somewhere off-machine.

---

## 7. HONEST ASSESSMENT

### Attempted but didn't work, or abandoned
- **The "coordinated contact pipeline"** (10-20 afternoon) was a joint-space multi-sample contact solver with feasible-reach clipping and recovery blending. It made most contact cases worse: 18 pass→fail changes on the original 38-case matrix. It was paused and its source was replaced; it survives only in the backups.
- **The simplified bounded-contact candidate** passed 7/16 cases with serious penetrations and was **rejected**.
- **A 45° cap on elbow pole steering** still produced a sharp elbow step, so it was rejected in favour of turning arm steering off.
- **The early clip import** crashed the editor (the sub-frame clip length assertion) and was fixed by padding lengths in the importer.
- The **flight speed boost** is listed as "not built yet". The FreeAnimationsPack flight and "ComboHands"/"RightLegKick" clips were imported but are barely used: only FlightStart and FlyingAttack are wired, and they are untested.

### Messy, duplicated or conflicting things
- **Tests contain diagnostic instrumentation.** `RPGStrikeContactTest.cpp` has optional selectors and capture code, and `RPGAnimInstance.cpp` has a default-off `rpg.ContactPoleTrace` CVar with a long `UE_LOG`. It's harmless but clutters the code.
- **`Saved/` is full of working notes and artifacts:** about 15 `.md`, `.json` and `.txt` reports, `PoleStudyInstrumentedOriginal.cpp` (a source file living in `Saved/`), a 400 KB orphan `.tmp` asset, about 39 automation result folders and hundreds of MB of backups. Several notes contradict one another over time; for example, one says "ready", then a later one says the binary was stale. **Trust only `CONTACT_CURRENT.md` and `ClosePunchDiagnosis.md` as the latest.**
- **Two input systems side by side.** The template's `IMC_Default` (keyboard/mouse/gamepad move, look, jump, added by the Blueprint controller) and the C++ `IMC_RPGController` (gamepad, priority 1) both bind the gamepad A button. The C++ one wins by priority. RPG combat has **no keyboard/mouse support**.
- **The dummy has two reaction paths:** the physics reaction, and an older "flinch" tilt used when reactions are off.
- **Leftover template and pack content:**
  - `FreeAnimationsPack/Demo` duplicates the Manny/Quinn meshes, materials and rigs.
  - There are orphaned `VehicleTemplate` external actors.
  - There are empty `Developers/` folders.
  - `DefaultEngine.ini` still names the project "Third Person BP Game Template".
  - `bUseSplitscreen=True`.
- **Hard-coded values:** the animation timings (impact, hold and charge-switch times) are hand-copied from `Tools/ProtoAnim/poses.py` into `UPROPERTY` defaults. The FP eye constants must "match fpanim.py". Nothing checks that they stay in sync.
- An empty **`New folder/`** sits in the project root.
- The `GameplayStateTree` plugin is enabled but unused.

### More than one AI agent has worked on this
- The 10-20 afternoon notes refer to "Original Claude state" and "What Claude had completed" in the third person.
- There is a `Developers/CodexSandboxOffline` folder, and the project is named RPG_CODEX.
- The contact work from 16:23 onward was most likely done by **a different agent**, not Claude. Its write-ups are thorough and self-critical, but the next planner should know that the design history is split across agents.

### Things I'm unsure about
- **What the user thought of any in-game test.** The logs only prove the actions ran.
- **What's inside the Blueprints and the level.** For example, whether `BP_ThirdPersonCharacter` overrides any RPG defaults, what the 10-20 "player mesh" change in `Tools/_backup_player_mesh_*` was, and what else is placed in `Lvl_ThirdPerson`.
- **Whether movement abilities (sprint, crouch, dodge, flight) were ever played.** That would have been before 10-20 03:38, and those logs have rotated away.
- **What the current code scores on the full automation suite.** It hasn't been run since the experimental version was reverted.
- The cause of the 10-20 DerivedDataCache and ShaderCompiler fatal crashes.
- The sibling project `C:\UNREAL PROJECTS\GameAnimationSample` exists (Epic's Game Animation Sample). I don't know whether it is meant to feed this project, for example motion matching.
