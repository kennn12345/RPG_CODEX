// Editor automation test for the dummy's physical hit reactions: Session Frontend > Automation > RPG.Combat.PhysicsReaction,
// or UnrealEditor-Cmd RPG_CODEX.uproject -ExecCmds="Automation RunTests RPG.Combat.PhysicsReaction;Quit" -unattended -nullrhi
// Plays the startup map, presses the real RT / LT / Y input actions at the dummy and checks the whole chain:
// melee hit -> hit data -> URPGPhysicsReactionComponent -> POWERED / LIMP -> impulse -> back to ANIMATED.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "RPG_CODEX.h"
#include "RPGCombatTestDummy.h"
#include "RPGControllerInputComponent.h"
#include "RPGPhysicsReactionComponent.h"
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
#include "PhysicsControlComponent.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

namespace
{
	struct FReactionCase
	{
		FString Name;
		FString Button;			// input action ("" = no press: the dummy just stands)
		float Hold = 0.08f;
		FString ExpectedAttack;
		ERPGPhysicsReactionState ExpectedState = ERPGPhysicsReactionState::Powered;
		float MaxTime = 2.f;	// seconds after the press for the whole reaction, back to ANIMATED
	};

	class FRPGPhysicsReactionTestCommand : public IAutomationLatentCommand
	{
	public:
		explicit FRPGPhysicsReactionTestCommand(FAutomationTestBase* InTest) : Test(InTest)
		{
			// Kicks alternate legs: right, then left. A 1.3 s hold is past FullChargeTime (1.0 s): full charge -> LIMP.
			Cases = {
				{TEXT("Dummy stands normally"), TEXT(""), 0.f, TEXT(""), ERPGPhysicsReactionState::Animated, 1.5f},
				{TEXT("Right punch (RT tap)"), TEXT("IA_RightAttack"), 0.08f, TEXT("Right Punch")},
				{TEXT("Left punch (LT tap)"), TEXT("IA_Defense"), 0.08f, TEXT("Left Punch")},
				{TEXT("Right kick (Y tap)"), TEXT("IA_Kick"), 0.08f, TEXT("Right Kick"), ERPGPhysicsReactionState::Powered, 2.2f},
				{TEXT("Left kick (Y tap)"), TEXT("IA_Kick"), 0.08f, TEXT("Left Kick"), ERPGPhysicsReactionState::Powered, 2.2f},
				{TEXT("Full-charge right punch (RT hold): full ragdoll"), TEXT("IA_RightAttack"), 1.3f, TEXT("Charged Right Punch"), ERPGPhysicsReactionState::Limp, 5.5f},
				{TEXT("Right punch after the ragdoll (back to normal)"), TEXT("IA_RightAttack"), 0.08f, TEXT("Right Punch")},
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
			if (!Player || !Controller || !Dummy.IsValid() || !Dummy->PhysicsReaction || !Player->FindComponentByClass<URPGControllerInputComponent>())
				return Timeout(TEXT("player, controller, input component, dummy or its physics reaction not found"));
			URPGPhysicsReactionComponent* Reaction = Dummy->PhysicsReaction;
			if (Stage == EStage::Boot)
			{
				// No renderer in a headless run: keep both skeletons evaluating so bones (and hit bodies) move.
				Player->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				Dummy->Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				Dummy->StopWanderingAtHome();   // the cases place the player relative to a dummy standing where it was placed
				Dummy->bUsePowerSystem = false; // this test covers the physics mechanics with the fixed charge -> state rule
				StartCase(Player, Controller, Now);
				return false;
			}
			const FReactionCase& Case = Cases[CaseIndex];
			UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Controller->GetLocalPlayer());
			UInputAction* Action = Case.Button.IsEmpty() ? nullptr : FindObject<UInputAction>(Player->FindComponentByClass<URPGControllerInputComponent>(), *Case.Button);
			switch (Stage)
			{
			case EStage::Settle:
				if (Now - StageStart < 0.8f) return false;
				// Standing reference, from the animation alone.
				StandPelvisZ = Dummy->Mesh->GetBoneLocation(TEXT("pelvis")).Z;
				StandHeadZ = Dummy->Mesh->GetBoneLocation(TEXT("head")).Z;
				HitsBefore = Dummy->HitCount;
				if (Action && Input) Input->StartContinuousInputInjectionForAction(Action, FInputActionValue(1.f), {}, {});
				Stage = EStage::Hold; StageStart = PressTime = Now;
				return false;
			case EStage::Hold:
				Observe(Reaction);
				if (Now - StageStart < Case.Hold) return false;
				if (Action && Input) Input->StopContinuousInputInjectionForAction(Action);
				Stage = EStage::Watch; StageStart = Now;
				return false;
			case EStage::Watch:
			{
				Observe(Reaction);
				const bool bBack = Reaction->GetReactionState() == ERPGPhysicsReactionState::Animated && !Reaction->IsReturning();
				const bool bReacted = Case.ExpectedState == ERPGPhysicsReactionState::Animated || bSawState;
				// Done once it has reacted and is back on the animation (plus a moment to check it stays there).
				if (bReacted && bBack && ReturnedAt < 0.f) ReturnedAt = Now;
				if (!(ReturnedAt >= 0.f && Now - ReturnedAt >= 0.3f) && Now - PressTime < Case.MaxTime) return false;
				Finish(Reaction, Now);
				if (++CaseIndex >= Cases.Num()) return true;
				StartCase(Player, Controller, Now);
				return false;
			}
			default:
				return true;
			}
		}

	private:
		enum class EStage { Boot, Settle, Hold, Watch };

		void StartCase(ACharacter* Player, APlayerController* Controller, float Now)
		{
			// In front of the dummy, facing it, feet on the same floor.
			const FVector Facing = Dummy->GetActorForwardVector().GetSafeNormal2D();
			const float FloorZ = Dummy->GetActorLocation().Z - Dummy->Capsule->GetScaledCapsuleHalfHeight();
			FVector Location = Dummy->GetActorLocation() + Facing * 100.f;
			Location.Z = FloorZ + Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.f;
			const FRotator ToTarget = (-Facing).Rotation();
			Player->SetActorLocationAndRotation(Location, FRotator(0.f, ToTarget.Yaw, 0.f), false, nullptr, ETeleportType::ResetPhysics);
			Controller->SetControlRotation(FRotator(0.f, ToTarget.Yaw, 0.f));
			bSawState = false;
			SawStateAt = -1.f;
			ReturnedAt = -1.f;
			MaxSimulating = 0;
			PeakDeviation = 0.f;
			DeviationAtReturn = -1.f;
			LowestPelvisZ = 1e9f;
			MaxHeadDrift = 0.f;
			bAnyBadState = false;
			Stage = EStage::Settle; StageStart = Now;
		}

		/** Every frame: state seen, bodies simulating, how far the hit bone is off its animated position. */
		void Observe(URPGPhysicsReactionComponent* Reaction)
		{
			const UWorld* World = Dummy->GetWorld();
			const FReactionCase& Case = Cases[CaseIndex];
			const ERPGPhysicsReactionState State = Reaction->GetReactionState();
			if (State == Case.ExpectedState && State != ERPGPhysicsReactionState::Animated && !bSawState)
			{
				bSawState = true;
				SawStateAt = World->GetTimeSeconds();
			}
			if (State != Case.ExpectedState && State != ERPGPhysicsReactionState::Animated) bAnyBadState = true;
			MaxSimulating = FMath::Max(MaxSimulating, Reaction->GetNumSimulatingBodies());
			USkeletalMeshComponent* Mesh = Dummy->Mesh;
			LowestPelvisZ = FMath::Min(LowestPelvisZ, Mesh->GetBoneLocation(TEXT("pelvis")).Z);
			if (Case.ExpectedState == ERPGPhysicsReactionState::Animated)
				MaxHeadDrift = FMath::Max(MaxHeadDrift, FMath::Abs(Mesh->GetBoneLocation(TEXT("head")).Z - StandHeadZ));
			// The struck chain (its furthest bone) against where the animation has it.
			if (bSawState && State != ERPGPhysicsReactionState::Animated)
			{
				const float Deviation = Reaction->GetStruckChainDeviation();
				PeakDeviation = FMath::Max(PeakDeviation, Deviation);
				// Where the motors had pulled it to by the time the fade back to the animation starts.
				if (Reaction->IsReturning() && DeviationAtReturn < 0.f) DeviationAtReturn = Deviation;
			}
		}

		void Finish(URPGPhysicsReactionComponent* Reaction, float Now)
		{
			const FReactionCase& Case = Cases[CaseIndex];
			const FRPGPhysicsReactionInfo& Info = Reaction->LastReaction;
			const FRPGMeleeHitInfo& Hit = Dummy->LastHit;
			const int32 Hits = Dummy->HitCount - HitsBefore;
			USkeletalMeshComponent* Mesh = Dummy->Mesh;
			const bool bBack = Reaction->GetReactionState() == ERPGPhysicsReactionState::Animated && !Reaction->IsReturning();
			const int32 SimulatingNow = Reaction->GetNumSimulatingBodies();
			// Settled back on the animation: the pose stands where it did before the hit.
			const float PelvisOff = FMath::Abs(Mesh->GetBoneLocation(TEXT("pelvis")).Z - StandPelvisZ);
			const float HeadOff = FMath::Abs(Mesh->GetBoneLocation(TEXT("head")).Z - StandHeadZ);
			const bool bStanding = bBack && SimulatingNow == 0 && PelvisOff <= 2.f && HeadOff <= 3.f;

			TArray<FString> Fails;
			FString Detail;
			if (Case.ExpectedState == ERPGPhysicsReactionState::Animated)
			{
				if (!bStanding || MaxSimulating > 0 || MaxHeadDrift > 5.f) Fails.Add(TEXT("not standing still on the animation"));
				Detail = FString::Printf(TEXT("state %s, bodies simulating %d (max %d), head moved %.1f cm, head %.0f cm above the floor"),
					*UEnum::GetDisplayValueAsText(Reaction->GetReactionState()).ToString(), SimulatingNow, MaxSimulating, MaxHeadDrift,
					StandHeadZ - (Dummy->GetActorLocation().Z - Dummy->Capsule->GetScaledCapsuleHalfHeight()));
			}
			else
			{
				const float DirDot = FVector::DotProduct(Info.ImpulseDirection, Hit.StrikeDirection.GetSafeNormal());
				if (Hits != 1) Fails.Add(FString::Printf(TEXT("hits %d"), Hits));
				if (Hit.AttackName != Case.ExpectedAttack) Fails.Add(FString::Printf(TEXT("attack '%s'"), *Hit.AttackName));
				if (!bSawState || bAnyBadState) Fails.Add(TEXT("wrong reaction state"));
				if (Info.HitBone != Hit.HitBone || Info.HitBone == NAME_None || Info.Limb == NAME_None) Fails.Add(TEXT("hit bone / limb not reported"));
				if (!Info.bImpulseApplied || Info.ImpulseMagnitude <= 0.f) Fails.Add(TEXT("impulse not applied"));
				if (DirDot < 0.999f) Fails.Add(TEXT("impulse direction != StrikeDirection"));
				if (!bBack) Fails.Add(TEXT("did not return to ANIMATED"));
				if (!bStanding) Fails.Add(TEXT("not standing on the animation afterwards"));
				if (Case.ExpectedState == ERPGPhysicsReactionState::Powered)
				{
					// The struck chain visibly gives way, stays attached, and the motors pull it back before the fade.
					if (PeakDeviation < 2.f) Fails.Add(TEXT("no visible yield"));
					if (PeakDeviation > 60.f) Fails.Add(TEXT("unstable (bone flew off)"));
					if (DeviationAtReturn < 0.f || DeviationAtReturn > PeakDeviation * 0.5f) Fails.Add(TEXT("motors did not pull it back toward the animation"));
					if (StandPelvisZ - LowestPelvisZ > 2.f) Fails.Add(TEXT("pelvis left the animation (anchor)"));
				}
				else
				{
					// Ragdoll: everything simulates and the body actually falls.
					if (MaxSimulating < 10) Fails.Add(TEXT("not a full ragdoll"));
					if (StandPelvisZ - LowestPelvisZ < 30.f) Fails.Add(TEXT("did not fall"));
				}
				Detail = FString::Printf(TEXT("'%s' on [%s] (limb %s) at (%.0f, %.0f, %.0f) | reaction %s after %.2fs, bodies simulating max %d | ")
					TEXT("impulse %.0f kg cm/s (TEMPORARY) dir (%.2f, %.2f, %.2f) vs StrikeDirection dot %.4f | struck chain off the animation: peak %.1f cm, %.1f cm when the fade back began | ")
					TEXT("pelvis lowest %.1f cm below standing | back to ANIMATED after %.2fs, bodies simulating %d, pelvis %.1f cm / head %.1f cm off standing | charge %.0f%%, limb speed %.0f cm/s"),
					*Hit.AttackName, *Info.HitBone.ToString(), *Info.Limb.ToString(), Info.ImpactPoint.X, Info.ImpactPoint.Y, Info.ImpactPoint.Z,
					*UEnum::GetDisplayValueAsText(Case.ExpectedState).ToString(), SawStateAt >= 0.f ? SawStateAt - PressTime : -1.f, MaxSimulating,
					Info.ImpulseMagnitude, Info.ImpulseDirection.X, Info.ImpulseDirection.Y, Info.ImpulseDirection.Z, DirDot, PeakDeviation, DeviationAtReturn,
					StandPelvisZ - LowestPelvisZ, ReturnedAt >= 0.f ? ReturnedAt - PressTime : -1.f, SimulatingNow, PelvisOff, HeadOff,
					Hit.ChargePercent * 100.f, Hit.LimbSpeed);
			}
			const bool bPass = Fails.IsEmpty();
			const FString Line = FString::Printf(TEXT("RPGTEST %s  %s: %s%s"), bPass ? TEXT("PASS") : TEXT("FAIL"), *Case.Name, *Detail,
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
		TArray<FReactionCase> Cases;
		TWeakObjectPtr<ARPGCombatTestDummy> Dummy;
		EStage Stage = EStage::Boot;
		int32 CaseIndex = 0;
		int32 WaitFrames = 0;
		int32 HitsBefore = 0;
		float StageStart = 0.f;
		float PressTime = 0.f;
		float StandPelvisZ = 0.f;
		float StandHeadZ = 0.f;
		bool bSawState = false;
		float SawStateAt = -1.f;
		float ReturnedAt = -1.f;
		int32 MaxSimulating = 0;
		float PeakDeviation = 0.f;
		float DeviationAtReturn = -1.f;
		float LowestPelvisZ = 1e9f;
		float MaxHeadDrift = 0.f;
		bool bAnyBadState = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPhysicsReactionTest, "RPG.Combat.PhysicsReaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRPGPhysicsReactionTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRPGPhysicsReactionTestCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
