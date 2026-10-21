// Editor automation test for the combat test dummy: Session Frontend > Automation > RPG.Combat.TestDummy, or
// UnrealEditor-Cmd RPG_CODEX.uproject -ExecCmds="Automation RunTests RPG.Combat.TestDummy;Quit" -unattended -nullrhi
// Plays the startup map, stands the player in front of the dummy and presses the real RT / LT / Y input actions
// (taps and holds), then checks what the dummy registered.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "RPG_CODEX.h"
#include "RPGCameraModeComponent.h"
#include "RPGCombatAimComponent.h"
#include "RPGCombatTestDummy.h"
#include "RPGCombatStateComponent.h"
#include "RPGHUD.h"
#include "RPGControllerInputComponent.h"
#include "RPGPrototypeAnimationComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraComponent.h"
#include "RPGAnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

namespace
{
	struct FDummyCase
	{
		FString Name;
		TArray<FString> Buttons;	// input actions held together
		float Hold = 0.f;			// seconds held (0 = no press)
		float Wait = 1.f;			// seconds after release before checking
		float Distance = 100.f;		// player to dummy, centre to centre
		int32 ExpectedHits = 1;
		FString ExpectedAttack;
		bool bSwitchPerspectiveMidHold = false;	// toggle first / third person halfway through the hold
		// Aim: the body faces this many degrees right of the dummy (+ = the dummy is on the body's left); the player keeps
		// the reticle on this bone of the dummy (or looks straight along the body).
		float BodyYawFromTarget = 0.f;
		bool bLookAtTarget = false;
		FName LookAtBone = NAME_None;
		int32 CompareHeight = 0;	// 2 = this hit sets the reference height, +1 / -1 = must land higher / lower than it
		float SwitchAfterRelease = -1.f;	// toggle the perspective this long after letting go (recovery), if >= 0
	};

	FDummyCase AimCase(const TCHAR* Name, const TCHAR* Button, const TCHAR* Attack, int32 Hits, float BodyYaw, bool bLookAt, const TCHAR* Bone, int32 Compare)
	{
		FDummyCase Case{Name, {Button}, 0.08f, 1.2f, 100.f, Hits, Attack};
		Case.BodyYawFromTarget = BodyYaw;
		Case.bLookAtTarget = bLookAt;
		Case.LookAtBone = Bone;
		Case.CompareHeight = Compare;
		return Case;
	}

	class FRPGDummyTestCommand : public IAutomationLatentCommand
	{
	public:
		FRPGDummyTestCommand(FAutomationTestBase* InTest, bool bInFirstPerson) : Test(InTest), bFirstPerson(bInFirstPerson)
		{
			// Kicks alternate legs: right, left, then the charged pair the same way.
			Cases = {
				{TEXT("Right punch (RT tap)"), {TEXT("IA_RightAttack")}, 0.08f, 1.0f, 100.f, 1, TEXT("Right Punch")},
				{TEXT("Left punch (LT tap)"), {TEXT("IA_Defense")}, 0.08f, 1.0f, 100.f, 1, TEXT("Left Punch")},
				{TEXT("Right kick (Y tap)"), {TEXT("IA_Kick")}, 0.08f, 1.2f, 100.f, 1, TEXT("Right Kick")},
				{TEXT("Left kick (Y tap)"), {TEXT("IA_Kick")}, 0.08f, 1.2f, 100.f, 1, TEXT("Left Kick")},
				{TEXT("Charged right punch (RT hold)"), {TEXT("IA_RightAttack")}, 1.3f, 1.4f, 100.f, 1, TEXT("Charged Right Punch")},
				{TEXT("Charged left punch (LT hold)"), {TEXT("IA_Defense")}, 1.3f, 1.4f, 100.f, 1, TEXT("Charged Left Punch")},
				{TEXT("Charged right kick (Y hold)"), {TEXT("IA_Kick")}, 1.3f, 1.4f, 100.f, 1, TEXT("Charged Right Kick")},
				{TEXT("Charged left kick (Y hold)"), {TEXT("IA_Kick")}, 1.3f, 1.4f, 100.f, 1, TEXT("Charged Left Kick")},
				{TEXT("Idle, standing against the dummy"), {}, 0.f, 3.0f, 80.f, 0, TEXT("")},
				{TEXT("Guard (RT+LT) against the dummy"), {TEXT("IA_RightAttack"), TEXT("IA_Defense")}, 1.5f, 1.0f, 80.f, 0, TEXT("")},
				{TEXT("Right punch out of reach"), {TEXT("IA_RightAttack")}, 0.08f, 1.0f, 250.f, 0, TEXT("")},
				{TEXT("Charged right punch, perspective switched during the hold"), {TEXT("IA_RightAttack")}, 1.3f, 1.4f, 100.f, 1, TEXT("Charged Right Punch"), true},
				{TEXT("Charged right kick, perspective switched back during the hold"), {TEXT("IA_Kick")}, 1.3f, 1.4f, 100.f, 1, TEXT("Charged Right Kick"), true},
				{TEXT("Guard, perspective switched while guarding"), {TEXT("IA_RightAttack"), TEXT("IA_Defense")}, 1.6f, 0.8f, 80.f, 0, TEXT(""), true},
				{TEXT("Right punch, perspective switched during the recovery"), {TEXT("IA_RightAttack")}, 0.08f, 1.0f, 100.f, 1, TEXT("Right Punch"), false, 0.f, false, NAME_None, 0, 0.35f},
				{TEXT("Left punch, perspective switched during the recovery"), {TEXT("IA_Defense")}, 0.08f, 1.0f, 100.f, 1, TEXT("Left Punch"), false, 0.f, false, NAME_None, 0, 0.35f},
				{TEXT("Left kick, perspective switched back during the recovery"), {TEXT("IA_Kick")}, 0.08f, 1.2f, 100.f, 1, TEXT("Left Kick"), false, 0.f, false, NAME_None, 0, 0.45f},
				// Look-directed aim.
				AimCase(TEXT("Reticle on the chest: right punch (reference height)"), TEXT("IA_RightAttack"), TEXT("Right Punch"), 1, 0.f, true, TEXT("spine_05"), 2),
				AimCase(TEXT("Reticle on the head: right punch lands on the head"), TEXT("IA_RightAttack"), TEXT("Right Punch"), 1, 0.f, true, TEXT("head"), 1),
				AimCase(TEXT("Reticle on the head: left punch lands on the head"), TEXT("IA_Defense"), TEXT("Left Punch"), 1, 0.f, true, TEXT("head"), 1),
				AimCase(TEXT("Reticle on the lower chest: right punch lands lower"), TEXT("IA_RightAttack"), TEXT("Right Punch"), 1, 0.f, true, TEXT("spine_03"), -1),
				{TEXT("Reticle on the head: charged right punch"), {TEXT("IA_RightAttack")}, 1.3f, 1.4f, 100.f, 1, TEXT("Charged Right Punch"), false, 0.f, true, TEXT("head"), 1},
				AimCase(TEXT("Dummy 30 deg left, reticle on it: right punch"), TEXT("IA_RightAttack"), TEXT("Right Punch"), 1, 30.f, true, TEXT("spine_05"), 0),
				AimCase(TEXT("Dummy 30 deg left, looking straight ahead: right punch misses"), TEXT("IA_RightAttack"), TEXT(""), 0, 30.f, false, TEXT(""), 0),
				AimCase(TEXT("Dummy 30 deg right, reticle on it: left punch"), TEXT("IA_Defense"), TEXT("Left Punch"), 1, -30.f, true, TEXT("spine_05"), 0),
				AimCase(TEXT("Dummy 65 deg left, reticle on it: body turns, right punch"), TEXT("IA_RightAttack"), TEXT("Right Punch"), 1, 65.f, true, TEXT("spine_05"), 0),
				AimCase(TEXT("Dummy 30 deg left, reticle on it: kick"), TEXT("IA_Kick"), TEXT("Right Kick"), 1, 30.f, true, TEXT("spine_05"), 0),
			};
		}

		virtual bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World) return Timeout(TEXT("no play world"));
			const float Now = World->GetTimeSeconds();
			ACharacter* Player = UGameplayStatics::GetPlayerCharacter(World, 0);
			APlayerController* Controller = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
			if (!Dummy.IsValid())
				for (TActorIterator<ARPGCombatTestDummy> It(World); It; ++It) Dummy = *It;
			if (!Player || !Controller || !Dummy.IsValid() || !Player->FindComponentByClass<URPGControllerInputComponent>())
				return Timeout(TEXT("player, controller, input component or dummy not found"));
			if (Stage == EStage::Boot)
			{
				// No renderer in a headless run: keep both skeletons evaluating so bones (and hit bodies) move.
				Player->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				Dummy->Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				Dummy->StopWanderingAtHome();   // the cases place the player relative to a dummy standing where it was placed
				if (bFirstPerson)
					if (URPGCameraModeComponent* CameraMode = Player->FindComponentByClass<URPGCameraModeComponent>())
						if (!CameraMode->bIsFirstPerson) CameraMode->TogglePerspective();
				StartCase(World, Player, Controller, Now);
				return false;
			}
			const FDummyCase& Case = Cases[CaseIndex];
			UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Controller->GetLocalPlayer());
			UObject* InputOwner = Player->FindComponentByClass<URPGControllerInputComponent>();
			switch (Stage)
			{
			case EStage::Settle:
				Observe(Player);
				if (Now - StageStart < 0.8f) return false;
				HitsBefore = Dummy->HitCount;
				if (const URPGCombatStateComponent* State = Player->FindComponentByClass<URPGCombatStateComponent>()) StrikeIdBefore = State->StrikeId;
				// Where the aimed part is when the attack is thrown (a full-charge hit then knocks the dummy down).
				AimedBoneZ = Case.LookAtBone != NAME_None ? Dummy->Mesh->GetBoneLocation(Case.LookAtBone).Z : 0.f;
				if (Case.Hold > 0.f && Input)
					for (const FString& Button : Case.Buttons)
						if (UInputAction* Action = FindObject<UInputAction>(InputOwner, *Button))
							Input->StartContinuousInputInjectionForAction(Action, FInputActionValue(1.f), {}, {});
				Stage = EStage::Hold; StageStart = Now;
				return false;
			case EStage::Hold:
				Observe(Player);
				if (Case.bSwitchPerspectiveMidHold && !bSwitched && Now - StageStart >= Case.Hold * 0.5f)
				{
					if (URPGCameraModeComponent* CameraMode = Player->FindComponentByClass<URPGCameraModeComponent>()) CameraMode->TogglePerspective();
					bSwitched = true;
				}
				if (Now - StageStart < Case.Hold) return false;
				if (Input)
					for (const FString& Button : Case.Buttons)
						if (UInputAction* Action = FindObject<UInputAction>(InputOwner, *Button))
							Input->StopContinuousInputInjectionForAction(Action);
				Stage = EStage::Wait; StageStart = Now;
				return false;
			case EStage::Wait:
			{
				Observe(Player);
				if (Case.SwitchAfterRelease >= 0.f && !bSwitched && Now - StageStart >= Case.SwitchAfterRelease)
				{
					if (URPGCameraModeComponent* CameraMode = Player->FindComponentByClass<URPGCameraModeComponent>()) CameraMode->TogglePerspective();
					bSwitched = true;
				}
				if (Now - StageStart < Case.Wait) return false;
				const int32 Hits = Dummy->HitCount - HitsBefore;
				const FRPGMeleeHitInfo& Last = Dummy->LastHit;
				const URPGCameraModeComponent* CameraMode = Player->FindComponentByClass<URPGCameraModeComponent>();
				const bool bViewNow = CameraMode && CameraMode->bIsFirstPerson;
				// In first person no hand / forearm may come near the camera (near clip plane 5 cm, plus the limb's thickness).
				const bool bCameraClear = !bViewNow || (NearestLimbToCamera >= 7.f && NearestBodyToCamera >= 7.f);
				// Aim height checks against the reference (level) hit, and the body never snaps round.
				bool bHeightOk = true;
				FString HeightNote;
				if (Hits > 0 && Case.CompareHeight == 2) ReferenceHeight = Last.ImpactLocation.Z;
				if (Hits > 0 && (Case.CompareHeight == 1 || Case.CompareHeight == -1))
				{
					const float Rise = Last.ImpactLocation.Z - ReferenceHeight;
					bHeightOk = Case.CompareHeight * Rise >= (Case.CompareHeight < 0 ? 5.f : 3.f);
					HeightNote = FString::Printf(TEXT(" | impact %+.1f cm vs the chest punch"), Rise);
				}
				// The punch lands near the height of the part under the reticle.
				// (kicks turn toward the aim but keep their height; low targets are at the edge of the down-tilt range, where a
				// descending fist meets the chest surface first, so they are only required to land clearly lower)
				if (Hits > 0 && Case.LookAtBone != NAME_None && !Last.bKick && Case.CompareHeight != -1)
				{
					const float Off = Last.ImpactLocation.Z - AimedBoneZ;
					bHeightOk &= FMath::Abs(Off) <= 18.f;
					HeightNote += FString::Printf(TEXT(" | %+.1f cm from the aimed %s"), Off, *Case.LookAtBone.ToString());
				}
				// First person: the camera is a steady aiming reference (it never dips into the body).
				// One strike per press: a perspective switch never restarts the attack or opens a second hit window.
				if (const URPGCombatStateComponent* State = Player->FindComponentByClass<URPGCombatStateComponent>())
				{
					const int32 Strikes = State->StrikeId - StrikeIdBefore;
					bHeightOk &= Strikes <= 1 && (Case.ExpectedHits == 0 || Strikes == 1);
					HeightNote += FString::Printf(TEXT(" | hit windows %d"), Strikes);
				}
				if (bViewNow && !Case.bSwitchPerspectiveMidHold && Case.SwitchAfterRelease < 0.f)
				{
					bHeightOk &= CameraTravel <= 16.f && CameraDrop <= 8.f;
					HeightNote += FString::Printf(TEXT(" | camera moved %.1f cm, dropped %.1f cm"), CameraTravel, CameraDrop);
				}
				const bool bNoSnap = MaxTurnRate <= 420.f;
				// Without a switch, every clip seen must be the current perspective's version. The mocap clips
				// (RPG_Mocap_*) have no first-person version yet, so they play in both views.
				bool bRightClips = true;
				if (!Case.bSwitchPerspectiveMidHold && Case.SwitchAfterRelease < 0.f)
					for (const FString& Clip : ClipsSeen)
						bRightClips &= Clip.StartsWith(bViewNow ? TEXT("RPG_FP_") : TEXT("RPG_Proto_")) || Clip.StartsWith(TEXT("RPG_Mocap_"));
				const bool bPass = bHeightOk && bNoSnap && bRightClips && bCameraClear && Hits == Case.ExpectedHits && (Case.ExpectedHits == 0 || Last.AttackName == Case.ExpectedAttack);
				const FString Line = FString::Printf(TEXT("RPGTEST %s  [%s] %s: hits %d (expected %d)%s%s | body turned %.0f deg, fastest %.0f deg/s | clips: %s%s"), bPass ? TEXT("PASS") : TEXT("FAIL"),
					bViewNow ? TEXT("first person") : TEXT("third person"), *Case.Name, Hits, Case.ExpectedHits,
					Hits > 0 ? *FString::Printf(TEXT(", last '%s' on [%s], aim %+.0f / %+.0f deg, %+.3fs from the impact frame, release charge %.0f%%, limb speed %.0f cm/s, distance %.0f cm"),
						*Last.AttackName, *Last.HitBone.ToString(), Last.AimYaw, Last.AimPitch, Last.TimeFromImpact, Last.ChargePercent * 100.f, Last.LimbSpeed, Last.Distance) : TEXT(""),
					*HeightNote, FMath::Abs(FRotator::NormalizeAxis(Player->GetActorRotation().Yaw - StartYaw)), MaxTurnRate,
					*FString::Join(ClipsSeen, TEXT(", ")),
					bViewNow ? *FString::Printf(TEXT(" | nearest to the camera: hand/forearm %.1f cm, chest/shoulders %.1f cm"), NearestLimbToCamera, NearestBodyToCamera) : TEXT(""));
				UE_LOG(LogRPGInput, Display, TEXT("%s"), *Line);
				if (bPass) Test->AddInfo(Line); else Test->AddError(Line);
				if (++CaseIndex >= Cases.Num())
				{
					CheckShadow(Player, bViewNow);
					// The reticle HUD is on the player, and on-screen debug text is off by default (rpg.Debug 0).
					const AHUD* Hud = Controller->GetHUD();
					const bool bHudOk = Hud && Hud->IsA<ARPGHUD>() && !URPGCombatStateComponent::IsDebugDisplayEnabled();
					const FString HudLine = FString::Printf(TEXT("RPGTEST %s  Reticle HUD: %s, on-screen debug text %s"), bHudOk ? TEXT("PASS") : TEXT("FAIL"),
						Hud ? *Hud->GetClass()->GetName() : TEXT("none"), URPGCombatStateComponent::IsDebugDisplayEnabled() ? TEXT("ON") : TEXT("off"));
					UE_LOG(LogRPGInput, Display, TEXT("%s"), *HudLine);
					if (bHudOk) Test->AddInfo(HudLine); else Test->AddError(HudLine);
					return true;
				}
				StartCase(World, Player, Controller, Now);
				return false;
			}
			default:
				return true;
			}
		}

	private:
		enum class EStage { Boot, Settle, Hold, Wait };

		void StartCase(UWorld* World, ACharacter* Player, APlayerController* Controller, float Now)
		{
			// In front of the dummy, facing it, feet on the same floor.
			const FDummyCase& Case = Cases[CaseIndex];
			// Stand the dummy back up: a full-charge hit leaves it ragdolled longer than a case lasts.
			Dummy->ResetDummy();
			const FVector Facing = Dummy->GetActorForwardVector().GetSafeNormal2D();
			const float FloorZ = Dummy->GetActorLocation().Z - Dummy->Capsule->GetScaledCapsuleHalfHeight();
			FVector Location = Dummy->GetActorLocation() + Facing * Case.Distance;
			Location.Z = FloorZ + Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.f;
			const FRotator ToTarget = (-Facing).Rotation();
			const FRotator Body(0.f, ToTarget.Yaw + Case.BodyYawFromTarget, 0.f);
			Player->SetActorLocationAndRotation(Location, Body, false, nullptr, ETeleportType::ResetPhysics);
			Controller->SetControlRotation(FRotator(0.f, Case.bLookAtTarget ? ToTarget.Yaw : Body.Yaw, 0.f));
			AimController = Controller;
			bHaveCameraStart = false;
			CameraTravel = 0.f;
			CameraDrop = 0.f;
			StartYaw = LastYaw = Body.Yaw;
			MaxTurnRate = 0.f;
			NearestBodyToCamera = 1e9f;
			Stage = EStage::Settle; StageStart = Now;
			ClipsSeen.Reset();
			NearestLimbToCamera = 1e9f;
			bSwitched = false;
		}

		/** Which clips play (first-person RPG_FP_* or third-person RPG_Proto_*) and how near the hands come to the camera. */
		void Observe(ACharacter* Player)
		{
			// Keep the reticle on the chosen part of the dummy, as a player would (from wherever the camera is now).
			const FDummyCase& Case = Cases[CaseIndex];
			APlayerController* Controller = AimController.Get();
			if (Case.bLookAtTarget && Case.LookAtBone != NAME_None && Controller && Controller->PlayerCameraManager && Dummy.IsValid())
			{
				const FVector Target = Dummy->Mesh->GetBoneLocation(Case.LookAtBone);
				Controller->SetControlRotation((Target - Controller->PlayerCameraManager->GetCameraLocation()).Rotation());
			}
			// every montage up (the guard plays over the combat stance, so there can be two at once)
			UAnimInstance* Instance = Player->GetMesh()->GetAnimInstance();
			TArray<TPair<const UAnimSequenceBase*, float>> MainClips;
			PlayingClips(Instance, MainClips);
			for (const auto& Clip : MainClips) ClipsSeen.AddUnique(Clip.Key->GetName());
			// First person: the shadow body plays the third-person version of the visible clip, at the same position.
			if (const URPGCameraModeComponent* CameraMode = Player->FindComponentByClass<URPGCameraModeComponent>(); CameraMode && CameraMode->bIsFirstPerson)
				if (const USkeletalMeshComponent* Shadow = FindShadow(Player); Shadow && Shadow->IsVisible())
				{
					// each first-person clip up on the visible body has its third-person version up on the shadow, same time
					TArray<TPair<const UAnimSequenceBase*, float>> ShadowClips;
					PlayingClips(Shadow->GetAnimInstance(), ShadowClips);
					bool bChecked = false, bMismatch = false;
					for (const auto& Main : MainClips)
					{
						if (!Main.Key->GetName().StartsWith(TEXT("RPG_FP_"))) continue;
						bChecked = true;
						const FString Want = Main.Key->GetName().Replace(TEXT("RPG_FP_"), TEXT("RPG_Proto_"));
						const TPair<const UAnimSequenceBase*, float>* Match = ShadowClips.FindByPredicate([&](const auto& C) { return C.Key->GetName() == Want; });
						const float Gap = Match ? FMath::Abs(Main.Value - Match->Value) : 0.f;
						if (!Match || Gap > 0.02f) bMismatch = true;
						else ShadowLargestGap = FMath::Max(ShadowLargestGap, Gap);
					}
					if (bChecked) ++ShadowFramesChecked;
					if (bMismatch)
					{
						++ShadowMismatches;
						FString What;
						for (const auto& C : MainClips) What += FString::Printf(TEXT(" %s@%.3f"), *C.Key->GetName(), C.Value);
						What += TEXT(" | shadow");
						for (const auto& C : ShadowClips) What += FString::Printf(TEXT(" %s@%.3f"), *C.Key->GetName(), C.Value);
						UE_LOG(LogRPGInput, Display, TEXT("RPGSHADOW mismatch [%s]:%s"), *Case.Name, *What);
					}
				}
			// How fast the body turns (it must never snap).
			// (not in the first moments after the test teleports the player: the camera is still where it was)
			if (const UWorld* World = Player->GetWorld(); World && World->GetDeltaSeconds() > 0.f)
			{
				const float Yaw = Player->GetActorRotation().Yaw;
				const bool bJustTeleported = Stage == EStage::Settle && World->GetTimeSeconds() - StageStart < 0.1f;
				if (!bJustTeleported)
					MaxTurnRate = FMath::Max(MaxTurnRate, FMath::Abs(FRotator::NormalizeAxis(Yaw - LastYaw)) / World->GetDeltaSeconds());
				LastYaw = Yaw;
			}
			const UCameraComponent* Camera = Player->FindComponentByClass<UCameraComponent>();
			if (!Camera) return;
			const FVector Eye = Camera->GetComponentLocation();
			// How far the (first-person) camera moves relative to the body during the case, and how far it drops.
			const FVector EyeLocal = Player->GetActorTransform().InverseTransformPosition(Eye);
			if (!bHaveCameraStart) { CameraStart = EyeLocal; bHaveCameraStart = true; }
			CameraTravel = FMath::Max(CameraTravel, FVector::Dist(EyeLocal, CameraStart));
			CameraDrop = FMath::Max(CameraDrop, CameraStart.Z - EyeLocal.Z);
			const USkeletalMeshComponent* Mesh = Player->GetMesh();
			for (const TCHAR* Side : {TEXT("l"), TEXT("r")})
			{
				const FVector Hand = Mesh->GetBoneLocation(*FString::Printf(TEXT("hand_%s"), Side));
				const FVector Knuckles = Mesh->GetBoneLocation(*FString::Printf(TEXT("middle_01_%s"), Side));
				const FVector Elbow = Mesh->GetBoneLocation(*FString::Printf(TEXT("lowerarm_%s"), Side));
				for (float A = 0.f; A <= 1.f; A += 0.25f)
				{
					NearestLimbToCamera = FMath::Min(NearestLimbToCamera, FVector::Dist(Eye, FMath::Lerp(Elbow, Hand, A)) - 5.f);
					NearestLimbToCamera = FMath::Min(NearestLimbToCamera, FVector::Dist(Eye, FMath::Lerp(Hand, Knuckles, A)) - 5.f);
				}
				// Shoulder and the top of the upper arm.
				const FVector Shoulder = Mesh->GetBoneLocation(*FString::Printf(TEXT("upperarm_%s"), Side));
				NearestBodyToCamera = FMath::Min(NearestBodyToCamera, FVector::Dist(Eye, Shoulder) - 7.5f);
				NearestBodyToCamera = FMath::Min(NearestBodyToCamera, FVector::Dist(Eye, FMath::Lerp(Shoulder, Elbow, 0.4f)) - 6.5f);
			}
			// Upper chest at the base of the (hidden) neck, and the chest.
			NearestBodyToCamera = FMath::Min(NearestBodyToCamera, FVector::Dist(Eye, Mesh->GetBoneLocation(TEXT("neck_01"))) - 7.f);
			NearestBodyToCamera = FMath::Min(NearestBodyToCamera, FVector::Dist(Eye, FMath::Lerp(Mesh->GetBoneLocation(TEXT("spine_05")), Mesh->GetBoneLocation(TEXT("neck_01")), 0.3f)) - 12.f);
		}

		static USkeletalMeshComponent* FindShadow(ACharacter* Player)
		{
			TArray<USkeletalMeshComponent*> Meshes;
			Player->GetComponents(Meshes);
			for (USkeletalMeshComponent* Mesh : Meshes)
				if (Mesh->GetFName() == TEXT("FirstPersonShadowBody")) return Mesh;
			return nullptr;
		}

		/** The clip and position of every montage playing (not blending out) on this instance. */
		static void PlayingClips(const UAnimInstance* Instance, TArray<TPair<const UAnimSequenceBase*, float>>& Out)
		{
			if (!Instance) return;
			for (const FAnimMontageInstance* Playing : Instance->MontageInstances)
				if (Playing && Playing->Montage && !Playing->IsStopped() && Playing->Montage->SlotAnimTracks.Num()
					&& Playing->Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num())
					if (const UAnimSequenceBase* Clip = Playing->Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference())
						Out.Emplace(Clip, Playing->GetPosition());
		}

		/** First person: the visible body is headless and casts no shadow; the shadow-only body casts the full one. */
		void CheckShadow(ACharacter* Player, bool bViewNow)
		{
			USkeletalMeshComponent* Mesh = Player->GetMesh();
			USkeletalMeshComponent* Shadow = FindShadow(Player);
			bool bOk;
			FString Detail;
			if (bViewNow)
			{
				const URPGAnimInstance* ShadowAnim = Shadow ? Cast<URPGAnimInstance>(Shadow->GetAnimInstance()) : nullptr;
				bOk = Shadow && Shadow->IsVisible() && !Shadow->bRenderInMainPass && Shadow->CastShadow && !Mesh->CastShadow
					&& Mesh->IsBoneHiddenByName(TEXT("neck_01")) && !Shadow->IsBoneHiddenByName(TEXT("neck_01"))
					&& Shadow->GetCollisionEnabled() == ECollisionEnabled::NoCollision && ShadowAnim && ShadowAnim->MirrorFrom.IsValid()
					&& ShadowMismatches == 0 && ShadowFramesChecked > 0;
				Detail = FString::Printf(TEXT("visible body: head hidden %d, casts shadow %d | shadow body: exists %d, in view %d, casts shadow %d, head hidden %d, collision %d, mirrored %d | ")
					TEXT("shadow played the third-person clip at the visible body's position in %d / %d checked frames (largest gap %.3f s)"),
					Mesh->IsBoneHiddenByName(TEXT("neck_01")), Mesh->CastShadow, Shadow != nullptr, Shadow && Shadow->bRenderInMainPass, Shadow && Shadow->CastShadow,
					Shadow && Shadow->IsBoneHiddenByName(TEXT("neck_01")), Shadow ? int32(Shadow->GetCollisionEnabled()) : -1, ShadowAnim && ShadowAnim->MirrorFrom.IsValid(),
					ShadowFramesChecked - ShadowMismatches, ShadowFramesChecked, ShadowLargestGap);
			}
			else
			{
				bOk = Mesh->CastShadow && !Mesh->IsBoneHiddenByName(TEXT("neck_01")) && (!Shadow || !Shadow->IsVisible());
				Detail = FString::Printf(TEXT("body casts shadow %d, head hidden %d, shadow body active %d"), Mesh->CastShadow, Mesh->IsBoneHiddenByName(TEXT("neck_01")), Shadow && Shadow->IsVisible());
			}
			const FString Line = FString::Printf(TEXT("RPGTEST %s  [%s] Shadow setup: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), bViewNow ? TEXT("first person") : TEXT("third person"), *Detail);
			UE_LOG(LogRPGInput, Display, TEXT("%s"), *Line);
			if (bOk) Test->AddInfo(Line); else Test->AddError(Line);
		}

		bool Timeout(const TCHAR* Why)
		{
			if (++WaitFrames < 600) return false;
			Test->AddError(FString::Printf(TEXT("RPGTEST could not start: %s"), Why));
			return true;
		}

		FAutomationTestBase* Test;
		bool bFirstPerson = false;
		bool bSwitched = false;
		TArray<FString> ClipsSeen;
		float NearestLimbToCamera = 1e9f;
		float NearestBodyToCamera = 1e9f;
		int32 ShadowFramesChecked = 0;
		int32 ShadowMismatches = 0;
		float ShadowLargestGap = 0.f;
		float ReferenceHeight = 0.f;
		float AimedBoneZ = 0.f;
		float StartYaw = 0.f;
		TWeakObjectPtr<APlayerController> AimController;
		FVector CameraStart = FVector::ZeroVector;
		bool bHaveCameraStart = false;
		float CameraTravel = 0.f;
		float CameraDrop = 0.f;
		float LastYaw = 0.f;
		float MaxTurnRate = 0.f;
		TArray<FDummyCase> Cases;
		TWeakObjectPtr<ARPGCombatTestDummy> Dummy;
		EStage Stage = EStage::Boot;
		int32 CaseIndex = 0;
		int32 HitsBefore = 0;
		int32 StrikeIdBefore = 0;
		int32 WaitFrames = 0;
		float StageStart = 0.f;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGCombatTestDummyTest, "RPG.Combat.TestDummy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRPGCombatTestDummyTest::RunTest(const FString& Parameters)
{
	// Plays the level open in the editor (the startup map, Lvl_ThirdPerson, has the dummy).
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRPGDummyTestCommand(this, false));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

// The same suite in first person (the first-person clips must land exactly the same hits).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGCombatTestDummyFirstPersonTest, "RPG.Combat.TestDummyFirstPerson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRPGCombatTestDummyFirstPersonTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRPGDummyTestCommand(this, true));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
