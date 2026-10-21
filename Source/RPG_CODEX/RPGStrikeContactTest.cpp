// Editor automation test for strike contact: Session Frontend > Automation > RPG.Combat.StrikeContact (third person) and
// RPG.Combat.StrikeContactFirstPerson, or
//   UnrealEditor-Cmd RPG_CODEX.uproject -ExecCmds="Automation RunTests RPG.Combat.StrikeContact;Quit" -unattended -nullrhi
// Presses the real RT / LT / Y input actions at the dummy from close and normal range (and at a dummy walking toward /
// away from the player) and measures, every frame, how far the attacking limb (fist, wrist / forearm, elbow; foot, shin,
// knee) is inside the dummy's physics bodies, plus the hits, impulses, joint angles and the contact correction.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "RPG_CODEX.h"
#include "RPGCameraModeComponent.h"
#include "RPGCombatTestDummy.h"
#include "RPGControllerInputComponent.h"
#include "RPGPhysicsReactionComponent.h"
#include "RPGStrikeContactComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Settings/LevelEditorPlaySettings.h"

namespace
{
	void RequestContactReviewPIE()
	{
		ULevelEditorPlaySettings* Play = NewObject<ULevelEditorPlaySettings>();
		Play->NewWindowWidth = 1600; Play->NewWindowHeight = 900;
		Play->NewWindowPosition = FIntPoint(0, 0); Play->CenterNewWindow = false;
		Play->LastExecutedPlayModeType = PlayMode_InEditorFloating;
		FRequestPlaySessionParams Session;
		Session.WorldType = EPlaySessionWorldType::PlayInEditor;
		Session.EditorPlaySettings = Play;
		GEditor->RequestPlaySession(Session);
	}
	struct FContactCase
	{
		FString Name;
		FString Button;
		float Hold = 0.08f;
		float Distance = 100.f;		// player to dummy, centre to centre (the capsules touch at about 70)
		float TargetSpeed = 0.f;	// dummy walks along the player->dummy line: + = away from the player, - = toward
		FString ExpectedAttack;
		float Wait = 1.3f;			// seconds after release before scoring
		bool bMayMiss = false;		// the target may walk out of reach before the strike lands (then: no hit, nothing held)
		bool bContinue = false;      // same recovering target without resetting it; player stays at the requested range
	};

	/** A point on the attacking limb, how thick the limb is there (cm), and which group it reports under. */
	struct FLimbSample { const TCHAR* A; const TCHAR* B; float T; float Radius; int32 Group; };
	enum EGroup { GEnd, GMid, GJoint, GCount };
	const TCHAR* GroupName(bool bFoot, int32 G)
	{
		static const TCHAR* Arm[] = {TEXT("fist"), TEXT("wrist/forearm"), TEXT("elbow")};
		static const TCHAR* Leg[] = {TEXT("foot"), TEXT("shin"), TEXT("knee")};
		return bFoot ? Leg[G] : Arm[G];
	}

	class FRPGStrikeContactTestCommand : public IAutomationLatentCommand
	{
	public:
		FRPGStrikeContactTestCommand(FAutomationTestBase* InTest, bool bInFirstPerson) : Test(InTest), bFirstPerson(bInFirstPerson)
		{
			// Kicks alternate legs on every press (right, left, right...), so they are listed in that order.
			// Charged: a 0.7 s hold releases part-charged (POWERED); 1.3 s is full charge (the dummy goes LIMP).
			Cases = {
				{TEXT("Right punch, normal range"), TEXT("IA_RightAttack"), 0.08f, 100.f, 0.f, TEXT("Right Punch")},
				{TEXT("Right punch, close range"), TEXT("IA_RightAttack"), 0.08f, 72.f, 0.f, TEXT("Right Punch")},
				{TEXT("Left punch, normal range"), TEXT("IA_Defense"), 0.08f, 100.f, 0.f, TEXT("Left Punch")},
				{TEXT("Left punch, close range"), TEXT("IA_Defense"), 0.08f, 72.f, 0.f, TEXT("Left Punch")},
				{TEXT("Right kick, normal range"), TEXT("IA_Kick"), 0.08f, 100.f, 0.f, TEXT("Right Kick")},
				{TEXT("Left kick, normal range"), TEXT("IA_Kick"), 0.08f, 100.f, 0.f, TEXT("Left Kick")},
				{TEXT("Right kick, close range"), TEXT("IA_Kick"), 0.08f, 72.f, 0.f, TEXT("Right Kick")},
				{TEXT("Left kick, close range"), TEXT("IA_Kick"), 0.08f, 72.f, 0.f, TEXT("Left Kick")},
				{TEXT("Charged right punch (part charge), close range"), TEXT("IA_RightAttack"), 0.7f, 72.f, 0.f, TEXT("Charged Right Punch"), 1.6f},
				{TEXT("Charged left punch (part charge), normal range"), TEXT("IA_Defense"), 0.7f, 100.f, 0.f, TEXT("Charged Left Punch"), 1.6f},
				{TEXT("Charged right punch (full charge), normal range"), TEXT("IA_RightAttack"), 1.3f, 100.f, 0.f, TEXT("Charged Right Punch"), 3.5f},
				{TEXT("Charged right kick (part charge), close range"), TEXT("IA_Kick"), 0.7f, 72.f, 0.f, TEXT("Charged Right Kick"), 1.6f},
				{TEXT("Charged left kick (part charge), normal range"), TEXT("IA_Kick"), 0.7f, 100.f, 0.f, TEXT("Charged Left Kick"), 1.6f},
				{TEXT("Right punch, dummy walking toward the player"), TEXT("IA_RightAttack"), 0.08f, 105.f, -120.f, TEXT("Right Punch")},
				{TEXT("Left punch, dummy walking away"), TEXT("IA_Defense"), 0.08f, 95.f, 120.f, TEXT("Left Punch")},
				{TEXT("Right kick, dummy walking toward the player"), TEXT("IA_Kick"), 0.08f, 110.f, -120.f, TEXT("Right Kick")},
				{TEXT("Left kick, dummy walking away (out of reach)"), TEXT("IA_Kick"), 0.08f, 95.f, 120.f, TEXT("Left Kick"), 1.3f, true},
				{TEXT("Right kick, dummy walking away (still in reach)"), TEXT("IA_Kick"), 0.08f, 75.f, 120.f, TEXT("Right Kick")},
				{TEXT("Right punch, dummy walking away (still in reach)"), TEXT("IA_RightAttack"), 0.08f, 78.f, 120.f, TEXT("Right Punch")},
				{TEXT("Charged left punch, close range"), TEXT("IA_Defense"), 0.7f, 72.f, 0.f, TEXT("Charged Left Punch"), 1.6f},
				{TEXT("Charged left kick, close range"), TEXT("IA_Kick"), 0.7f, 72.f, 0.f, TEXT("Charged Left Kick"), 1.6f},
				{TEXT("Charged right kick, normal range"), TEXT("IA_Kick"), 0.7f, 100.f, 0.f, TEXT("Charged Right Kick"), 1.6f},
				{TEXT("Repeated right punch, close range"), TEXT("IA_RightAttack"), 0.08f, 72.f, 0.f, TEXT("Right Punch"), 0.8f},
				{TEXT("Repeated left punch, close range"), TEXT("IA_Defense"), 0.08f, 72.f, 0.f, TEXT("Left Punch"), 1.3f, false, true},
				{TEXT("Repeated left kick, normal range"), TEXT("IA_Kick"), 0.08f, 100.f, 0.f, TEXT("Left Kick"), 0.8f},
				{TEXT("Repeated right kick, normal range"), TEXT("IA_Kick"), 0.08f, 100.f, 0.f, TEXT("Right Kick"), 1.3f, false, true},
				{TEXT("Repeated right punch, normal range"), TEXT("IA_RightAttack"), 0.08f, 100.f, 0.f, TEXT("Right Punch"), 0.8f},
				{TEXT("Repeated left punch, normal range"), TEXT("IA_Defense"), 0.08f, 100.f, 0.f, TEXT("Left Punch"), 1.3f, false, true},
				{TEXT("Repeated left kick, close range"), TEXT("IA_Kick"), 0.08f, 72.f, 0.f, TEXT("Left Kick"), 0.8f},
				{TEXT("Repeated right kick, close range"), TEXT("IA_Kick"), 0.08f, 72.f, 0.f, TEXT("Right Kick"), 1.3f, false, true},
			};
			// Optional focused run of the existing ordinary close/normal cases; defaults retain the full suite.
			if (!FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_CONTACT_ORDINARY")).IsEmpty()) Cases.SetNum(8);
			if (!FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_CONTACT_CLOSE_PUNCH")).IsEmpty()) Cases = {Cases[1]};
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
			if (!Player || !Controller || !Dummy.IsValid() || !Dummy->PhysicsReaction || !Player->FindComponentByClass<URPGControllerInputComponent>())
				return Timeout(TEXT("player, controller, input component or dummy not found"));
			if (Stage == EStage::Boot)
			{
				Player->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				Dummy->Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				Dummy->StopWanderingAtHome();
				Dummy->bUsePowerSystem = false;   // contact cases expect the fixed charge -> state rule, not Power System rolls
				Home = Dummy->GetActorLocation();
				if (URPGCameraModeComponent* CameraMode = Player->FindComponentByClass<URPGCameraModeComponent>(); CameraMode && CameraMode->bIsFirstPerson != bFirstPerson)
					CameraMode->TogglePerspective();
				if (!bFirstPerson && !FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_CONTACT_SHOTS")).IsEmpty())
				{
					ReviewCamera = World->SpawnActor<ACameraActor>();
					Controller->SetViewTarget(ReviewCamera.Get());
				}
				StartCase(Player, Controller, Now);
				return false;
			}
			const FContactCase& Case = Cases[CaseIndex];
			UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Controller->GetLocalPlayer());
			UInputAction* Action = FindObject<UInputAction>(Player->FindComponentByClass<URPGControllerInputComponent>(), *Case.Button);
			// The dummy walks along the line between them (from the press on), stopping once hit, like a wandering dummy.
			if (Stage != EStage::Settle && Case.TargetSpeed != 0.f && Dummy->HitCount == HitsBefore)
			{
				const FVector Away = (Dummy->GetActorLocation() - Player->GetActorLocation()).GetSafeNormal2D();
				Dummy->SetActorLocation(Dummy->GetActorLocation() + Away * Case.TargetSpeed * World->GetDeltaSeconds());
			}
			switch (Stage)
			{
			case EStage::Settle:
				if (Now - StageStart < (Case.bContinue ? 0.1f : 0.8f)) return false;
				HitsBefore = Dummy->HitCount;
				ImpulsesBefore = Dummy->PhysicsReaction->ImpulsesApplied;
				if (Action && Input) Input->StartContinuousInputInjectionForAction(Action, FInputActionValue(1.f), {}, {});
				Stage = EStage::Hold; StageStart = PressTime = Now;
				return false;
			case EStage::Hold:
				Observe(Player);
				if (Now - StageStart < Case.Hold) return false;
				if (Action && Input) Input->StopContinuousInputInjectionForAction(Action);
				Stage = EStage::Watch; StageStart = Now;
				return false;
			case EStage::Watch:
				Observe(Player);
				if (Now - StageStart < Case.Wait) return false;
				Finish(Player);
				if (++CaseIndex >= Cases.Num())
				{
					if (ReviewCamera.IsValid()) { Controller->SetViewTarget(Player); ReviewCamera->Destroy(); }
					if (bResisting) { Dummy->PhysicsReaction->bReactionsEnabled = true; Dummy->SetActorTickEnabled(true); }
					return true;
				}
				StartCase(Player, Controller, Now);
				return false;
			default:
				return true;
			}
		}

	private:
		enum class EStage { Boot, Settle, Hold, Watch };

		void StartCase(ACharacter* Player, APlayerController* Controller, float Now)
		{
			const FContactCase& Case = Cases[CaseIndex];
			Dummy->PhysicsReaction->bReactionsEnabled = !bResisting;
			// Test-only fixed target: no physical reaction, flinch, walking or capsule shove.
			Dummy->SetActorTickEnabled(!bResisting);
			if (!Case.bContinue)
			{
				Dummy->ResetDummy();
				Dummy->SetActorLocation(Home);
			}
			const FVector Facing = Dummy->GetActorForwardVector().GetSafeNormal2D();
			const float FloorZ = Dummy->GetActorLocation().Z - Dummy->Capsule->GetScaledCapsuleHalfHeight();
			FVector Location = Dummy->GetActorLocation() + Facing * Case.Distance;
			Location.Z = FloorZ + Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.f;
			const FRotator ToTarget = (-Facing).Rotation();
			Player->SetActorLocationAndRotation(Location, FRotator(0.f, ToTarget.Yaw, 0.f), false, nullptr, ETeleportType::ResetPhysics);
			Controller->SetControlRotation(FRotator(0.f, ToTarget.Yaw, 0.f));
			if (ReviewCamera.IsValid())
			{
				const FVector Focus = (Player->GetActorLocation() + Dummy->GetActorLocation()) * 0.5f + FVector(0.f, 0.f, 15.f);
				const FVector ViewFacing = (Dummy->GetActorLocation() - Player->GetActorLocation()).GetSafeNormal2D();
				const ERPGStrikeLimb Limb = ExpectedLimb();
				const float Side = Limb == ERPGStrikeLimb::RightHand || Limb == ERPGStrikeLimb::RightFoot ? -1.f : 1.f;
				const FVector ViewLocation = Focus + FVector::CrossProduct(ViewFacing, FVector::UpVector) * (225.f * Side) - ViewFacing * 35.f + FVector(0.f, 0.f, 35.f);
				ReviewCamera->SetActorLocationAndRotation(ViewLocation, (Focus - ViewLocation).Rotation());
			}
			for (int32 G = 0; G < GCount; ++G) { MaxDepth[G] = 0.f; DepthBone[G] = NAME_None; }
			MinJointAngle = 180.f;
			SeenState = ERPGPhysicsReactionState::Animated;
			MaxCorrection = 0.f;
			MaxRelease = 0.f;
			LastCorrection = 0.f;
			LastStep = 0.f;
			Reversals = 0;
			CorrectionAtEnd = 0.f;
			FramesInContact = 0;
			MaxFoldDeficit = 0.f;
			bHaveLast = false;
			MaxLengthError = 0.f;
			ShotMask = 0; LastDenseFrame = INDEX_NONE;
			for (int32 L = 0; L < RPGStrikeContact::NumLimbs; ++L)
			{
				const RPGStrikeContact::FLimbDef& Def = RPGStrikeContact::Limb(L);
				const FTransform Root = Player->GetMesh()->GetBoneTransform(Player->GetMesh()->GetBoneIndex(Def.Root));
				const FTransform Mid = Player->GetMesh()->GetBoneTransform(Player->GetMesh()->GetBoneIndex(Def.Mid));
				const FTransform End = Player->GetMesh()->GetBoneTransform(Player->GetMesh()->GetBoneIndex(Def.End));
				Lengths[L][0] = FVector::Dist(Root.GetLocation(), Mid.GetLocation());
				Lengths[L][1] = FVector::Dist(Mid.GetLocation(), End.GetLocation());
			}
			if (const URPGStrikeContactComponent* Contact = Player->FindComponentByClass<URPGStrikeContactComponent>()) PushbackBefore = Contact->PushbackDistance;
			Stage = EStage::Settle; StageStart = Now;
		}

		ERPGStrikeLimb ExpectedLimb() const
		{
			const FString& A = Cases[CaseIndex].ExpectedAttack;
			const bool bLeft = A.Contains(TEXT("Left"));
			if (A.Contains(TEXT("Kick"))) return bLeft ? ERPGStrikeLimb::LeftFoot : ERPGStrikeLimb::RightFoot;
			return bLeft ? ERPGStrikeLimb::LeftHand : ERPGStrikeLimb::RightHand;
		}

		/** Every frame: how deep the attacking limb is inside the dummy's bodies, its mid-joint angle, the correction. */
		void Observe(ACharacter* Player)
		{
			const ERPGStrikeLimb Limb = ExpectedLimb();
			const bool bFoot = Limb == ERPGStrikeLimb::RightFoot || Limb == ERPGStrikeLimb::LeftFoot;
			const bool bRight = Limb == ERPGStrikeLimb::RightHand || Limb == ERPGStrikeLimb::RightFoot;
			const TCHAR* S = bRight ? TEXT("r") : TEXT("l");
			auto B = [S](const TCHAR* Base) { return FName(*FString::Printf(TEXT("%s_%s"), Base, S)); };
			// Radii roughly the mannequin's mesh thickness there.
			const FLimbSample ArmSamples[] = {
				{TEXT("hand"), TEXT("middle_01"), 0.55f, 4.5f, GEnd}, {TEXT("middle_01"), TEXT("middle_01"), 0.f, 3.5f, GEnd},
				{TEXT("hand"), TEXT("hand"), 0.f, 3.5f, GMid}, {TEXT("lowerarm"), TEXT("hand"), 0.5f, 4.f, GMid},
				{TEXT("lowerarm"), TEXT("lowerarm"), 0.f, 4.5f, GJoint}};
			const FLimbSample LegSamples[] = {
				{TEXT("foot"), TEXT("ball"), 0.5f, 5.f, GEnd}, {TEXT("ball"), TEXT("ball"), 0.f, 4.f, GEnd}, {TEXT("foot"), TEXT("foot"), 0.f, 5.f, GEnd},
				{TEXT("calf"), TEXT("foot"), 0.5f, 5.5f, GMid},
				{TEXT("calf"), TEXT("calf"), 0.f, 6.f, GJoint}};
			USkeletalMeshComponent* Mesh = Player->GetMesh();
			// Observe the rendered skeleton, including downstream IK and blending, on all limbs.
			for (int32 L = 0; L < RPGStrikeContact::NumLimbs; ++L)
			{
				const RPGStrikeContact::FLimbDef& Def = RPGStrikeContact::Limb(L);
				const float Upper = FVector::Dist(Mesh->GetBoneLocation(Def.Root), Mesh->GetBoneLocation(Def.Mid));
				const float Lower = FVector::Dist(Mesh->GetBoneLocation(Def.Mid), Mesh->GetBoneLocation(Def.End));
				MaxLengthError = FMath::Max(MaxLengthError, FMath::Max(FMath::Abs(Upper - Lengths[L][0]), FMath::Abs(Lower - Lengths[L][1])));
			}
			if (!FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_CONTACT_DENSE")).IsEmpty())
			{
				const float Elapsed = Player->GetWorld()->GetTimeSeconds() - PressTime;
				const int32 Frame = FMath::FloorToInt(Elapsed * 30.f);
				if (Frame >= 0 && Frame < 30 && Frame > LastDenseFrame)
				{
					LastDenseFrame = Frame;
					const FString Tag = FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_CONTACT_TAG"));
					FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / FString::Printf(TEXT("Screenshots/PoleStudy/%s_%s_%02d.png"), *Tag, bFirstPerson ? TEXT("FP") : TEXT("TP"), Frame), false, false);
				}
			}
			// Optional real mesh captures: run with rendering enabled and RPG_CONTACT_SHOTS=1.
			// These are evidence for human visual review, never a substitute for assertions.
			if (!FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_CONTACT_SHOTS")).IsEmpty())
			{
				const float T = Player->GetWorld()->GetTimeSeconds() - PressTime;
				const float Times[] = {Cases[CaseIndex].Hold + 0.12f, Cases[CaseIndex].Hold + 0.24f, Cases[CaseIndex].Hold + 0.4f, Cases[CaseIndex].Hold + 0.65f};
				for (int32 I = 0; I < UE_ARRAY_COUNT(Times); ++I)
					if (!(ShotMask & (1 << I)) && T >= Times[I])
					{
						ShotMask |= 1 << I;
						FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / FString::Printf(TEXT("Screenshots/Contact/%s_%02d_%d.png"), bFirstPerson ? TEXT("FP") : TEXT("TP"), CaseIndex, I), false, false);
						break;
					}
			}
			for (int32 G = 0; G < GCount; ++G) { FrameDepth[G] = 0.f; FrameBone[G] = NAME_None; }
			for (const FLimbSample& Sample : bFoot ? TArrayView<const FLimbSample>(LegSamples) : TArrayView<const FLimbSample>(ArmSamples))
			{
				const FVector P = FMath::Lerp(Mesh->GetBoneLocation(B(Sample.A)), Mesh->GetBoneLocation(B(Sample.B)), Sample.T);
				for (const FBodyInstance* Body : Dummy->Mesh->Bodies)
				{
					FMTDResult MTD;
					if (!Body || !Body->OverlapTest(P, FQuat::Identity, FCollisionShape::MakeSphere(Sample.Radius), &MTD)) continue;
					if (MTD.Distance > FrameDepth[Sample.Group]) FrameBone[Sample.Group] = Body->BodySetup.IsValid() ? Body->BodySetup->BoneName : NAME_None;
					FrameDepth[Sample.Group] = FMath::Max(FrameDepth[Sample.Group], float(MTD.Distance));
					if (MTD.Distance > MaxDepth[Sample.Group])
					{
						MaxDepth[Sample.Group] = MTD.Distance;
						DepthBone[Sample.Group] = Body->BodySetup.IsValid() ? Body->BodySetup->BoneName : NAME_None;
					}
				}
			}
			// Elbow / knee: angle between the upper and lower segment (180 = straight).
			const FVector Root = Mesh->GetBoneLocation(B(bFoot ? TEXT("thigh") : TEXT("upperarm")));
			const FVector Mid = Mesh->GetBoneLocation(B(bFoot ? TEXT("calf") : TEXT("lowerarm")));
			const FVector End = Mesh->GetBoneLocation(B(bFoot ? TEXT("foot") : TEXT("hand")));
			const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct((Root - Mid).GetSafeNormal(), (End - Mid).GetSafeNormal()), -1.f, 1.f)));
			MinJointAngle = FMath::Min(MinJointAngle, Angle);
			if (Dummy->PhysicsReaction->GetReactionState() != ERPGPhysicsReactionState::Animated) SeenState = Dummy->PhysicsReaction->GetReactionState();
			// The contact correction on this limb (0 without the contact component).
			if (const URPGStrikeContactComponent* Contact = Player->FindComponentByClass<URPGStrikeContactComponent>())
			{
				const float C = Contact->GetCorrection(Limb).Size();
				MaxCorrection = FMath::Max(MaxCorrection, C);
				if (bHaveLast)
				{
					// Easing off: how fast the limb is let back to its animation (a snap would be a big drop in one frame).
					const float Step = C - LastCorrection;
					MaxRelease = FMath::Max(MaxRelease, -Step);
					// Jitter: the correction going back and forth (not just rising at impact and falling off after).
					if (C > 1.f && FMath::Abs(Step) > 0.5f && FMath::Abs(LastStep) > 0.5f && FMath::Sign(Step) != FMath::Sign(LastStep)) ++Reversals;
					if (FMath::Abs(Step) > 0.5f) LastStep = Step;
				}
				if (C > 0.2f) ++FramesInContact;
				if (bTrace && (C > 0.05f || LastCorrection > 0.05f || FrameDepth[GEnd] > 0.5f))
				{
					float A2, C2; Contact->GetJointAngles(Limb, A2, C2);
					UE_LOG(LogRPGInput, Display, TEXT("RPGCONTACTTRACE [%s] t %.3f corr %.2f depth %.1f[%s]/%.1f/%.1f angle %.0f->%.0f contact %d striking %d"), *Cases[CaseIndex].Name,
						Player->GetWorld()->GetTimeSeconds() - PressTime, C, FrameDepth[GEnd], *FrameBone[GEnd].ToString(), FrameDepth[GMid], FrameDepth[GJoint], A2, C2,
						Contact->IsInContact(Limb) ? 1 : 0, Contact->HasStrikingContact(Limb) ? 1 : 0);
				}
				LastCorrection = C;
				CorrectionAtEnd = C;
				bHaveLast = true;
				// The elbow / knee must not be folded tighter than the animation has it, beyond its fold limit.
				float Animated, Corrected;
				Contact->GetJointAngles(Limb, Animated, Corrected);
				const float Limit = FMath::Min(Animated, bFoot ? 30.f : 35.f);
				MaxFoldDeficit = FMath::Max(MaxFoldDeficit, Limit - Corrected);
			}
		}

		void Finish(ACharacter* Player)
		{
			const FContactCase& Case = Cases[CaseIndex];
			const bool bFoot = ExpectedLimb() == ERPGStrikeLimb::RightFoot || ExpectedLimb() == ERPGStrikeLimb::LeftFoot;
			const int32 Hits = Dummy->HitCount - HitsBefore;
			const int32 Impulses = Dummy->PhysicsReaction->ImpulsesApplied - ImpulsesBefore;
			const FRPGMeleeHitInfo& Hit = Dummy->LastHit;
			TArray<FString> Fails;
			if (!FMath::IsFinite(MaxLengthError) || MaxLengthError > 0.2f) Fails.Add(FString::Printf(TEXT("limb length changed %.3f cm"), MaxLengthError));
			if (Hits != 1 && !(Case.bMayMiss && Hits == 0)) Fails.Add(FString::Printf(TEXT("hits %d"), Hits));
			else if (Hits == 1 && Hit.AttackName != Case.ExpectedAttack) Fails.Add(FString::Printf(TEXT("attack '%s'"), *Hit.AttackName));
			const int32 ExpectedImpulses = bResisting ? 0 : Hits;
			if (Impulses != ExpectedImpulses) Fails.Add(FString::Printf(TEXT("impulses %d, expected %d for %d hits"), Impulses, ExpectedImpulses, Hits));
			// A little give at the surface is allowed (compression); beyond that it reads as passing through.
			const float EndLimit = 2.5f, OtherLimit = 3.f;
			if (MaxDepth[GEnd] > EndLimit) Fails.Add(FString::Printf(TEXT("%s %.1f cm inside [%s]"), GroupName(bFoot, GEnd), MaxDepth[GEnd], *DepthBone[GEnd].ToString()));
			for (int32 G = GMid; G < GCount; ++G)
				if (MaxDepth[G] > OtherLimit) Fails.Add(FString::Printf(TEXT("%s %.1f cm inside [%s]"), GroupName(bFoot, G), MaxDepth[G], *DepthBone[G].ToString()));
			if (MaxFoldDeficit > 2.f) Fails.Add(FString::Printf(TEXT("%s folded %.0f deg past its limit"), bFoot ? TEXT("knee") : TEXT("elbow"), MaxFoldDeficit));
			if (MaxRelease > 6.f) Fails.Add(FString::Printf(TEXT("let go %.1f cm in one frame (snap)"), MaxRelease));
			if (Reversals > 2) Fails.Add(FString::Printf(TEXT("jitter: correction reversed %d times"), Reversals));
			// A rapid pair intentionally starts its next strike during target recovery;
			// score full release after the pair, while retaining per-frame checks throughout.
			const bool bStartsRapidPair = Cases.IsValidIndex(CaseIndex + 1) && Cases[CaseIndex + 1].bContinue;
			if (!bStartsRapidPair && CorrectionAtEnd > 0.5f) Fails.Add(FString::Printf(TEXT("limb still held off its animation %.1f cm at the end"), CorrectionAtEnd));
			const URPGStrikeContactComponent* Contact = Player->FindComponentByClass<URPGStrikeContactComponent>();
			const float Pushback = Contact ? Contact->PushbackDistance - PushbackBefore : 0.f;
			const bool bPass = Fails.IsEmpty();
			const FString Line = FString::Printf(TEXT("RPGTEST %s  [%s] %s: hits %d, impulses %d | deepest inside the dummy: %s %.1f cm [%s], %s %.1f cm [%s], %s %.1f cm [%s] | ")
				TEXT("%s min %.0f deg | contact correction max %.1f cm, largest release %.2f cm/frame, frames in contact %d, at the end %.2f cm | hit [%s] speed %.0f cm/s dir (%.2f, %.2f, %.2f) | reaction %s strength %.2f | pushed back %.1f cm, reversals %d, fold past limit %.1f%s"),
				bPass ? TEXT("PASS") : TEXT("FAIL"), bFirstPerson ? TEXT("first person") : TEXT("third person"), *Case.Name, Hits, Impulses,
				GroupName(bFoot, GEnd), MaxDepth[GEnd], *DepthBone[GEnd].ToString(), GroupName(bFoot, GMid), MaxDepth[GMid], *DepthBone[GMid].ToString(),
				GroupName(bFoot, GJoint), MaxDepth[GJoint], *DepthBone[GJoint].ToString(),
				bFoot ? TEXT("knee") : TEXT("elbow"), MinJointAngle, MaxCorrection, MaxRelease, FramesInContact, CorrectionAtEnd,
				*Hit.HitBone.ToString(), Hit.LimbSpeed, Hit.StrikeDirection.X, Hit.StrikeDirection.Y, Hit.StrikeDirection.Z,
				*UEnum::GetDisplayValueAsText(SeenState).ToString(), Dummy->PhysicsReaction->LastReaction.Strength,
				Pushback, Reversals, MaxFoldDeficit,
				bPass ? TEXT("") : *FString::Printf(TEXT(" | FAILED: %s"), *FString::Join(Fails, TEXT(", "))));
			UE_LOG(LogRPGInput, Display, TEXT("%s"), *Line);
			if (bPass) Test->AddInfo(Line); else Test->AddError(Line);
		}

		bool Timeout(const TCHAR* Why)
		{
			if (++WaitFrames < 600) return false;
			Test->AddError(FString::Printf(TEXT("RPGTEST could not start: %s"), Why));
			return true;
		}

		FAutomationTestBase* Test;
		bool bFirstPerson = false;
		bool bResisting = !FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_CONTACT_RESISTING")).IsEmpty();
		TArray<FContactCase> Cases;
		TWeakObjectPtr<ARPGCombatTestDummy> Dummy;
		TWeakObjectPtr<ACameraActor> ReviewCamera;
		FVector Home = FVector::ZeroVector;
		EStage Stage = EStage::Boot;
		int32 CaseIndex = 0;
		int32 WaitFrames = 0;
		int32 HitsBefore = 0;
		int32 ImpulsesBefore = 0;
		float StageStart = 0.f;
		float PressTime = 0.f;
		float MaxDepth[GCount] = {};
		float FrameDepth[GCount] = {};
		FName FrameBone[GCount];
		bool bTrace = !FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_CONTACT_TRACE")).IsEmpty();
		FName DepthBone[GCount];
		float MinJointAngle = 180.f;
		float Lengths[RPGStrikeContact::NumLimbs][2] = {};
		float MaxLengthError = 0.f;
		int32 ShotMask = 0;
		int32 LastDenseFrame = INDEX_NONE;
		ERPGPhysicsReactionState SeenState = ERPGPhysicsReactionState::Animated;
		float MaxCorrection = 0.f;
		float MaxRelease = 0.f;
		float LastCorrection = 0.f;
		float LastStep = 0.f;
		int32 Reversals = 0;
		float MaxFoldDeficit = 0.f;
		float PushbackBefore = 0.f;
		float CorrectionAtEnd = 0.f;
		int32 FramesInContact = 0;
		bool bHaveLast = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGStrikeContactTest, "RPG.Combat.StrikeContact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRPGStrikeContactTest::RunTest(const FString& Parameters)
{
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_CONTACT_SHOTS")).IsEmpty())
	{
		ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	}
	else RequestContactReviewPIE();
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRPGStrikeContactTestCommand(this, false));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGStrikeContactFirstPersonTest, "RPG.Combat.StrikeContactFirstPerson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRPGStrikeContactFirstPersonTest::RunTest(const FString& Parameters)
{
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_CONTACT_SHOTS")).IsEmpty())
	{
		ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	}
	else RequestContactReviewPIE();
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRPGStrikeContactTestCommand(this, true));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
