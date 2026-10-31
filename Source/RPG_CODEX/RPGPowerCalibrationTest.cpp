// Power System calibration sweep: Session Frontend > Automation > RPG.Power.Calibration, or
// UnrealEditor-Cmd RPG_CODEX.uproject -ExecCmds="Automation RunTests RPG.Power.Calibration"
// -TestExit="Automation Test Queue Empty" -unattended -nullrhi
// On a flat floor above the level, punches the level's Standard Bandit (a GASP NPC; normal punch) at each Strength in the sweep and
// measures falling pelvis travel before the get-up against the Power System's arc + slide distance; standing outcomes
// still report final actor displacement. Prints a table (search the
// log for RPGCALIB). Launches must end up 70-135% of the calculated distance; below 8 ft allow 3 ft absolute error
// for body-length-scale toppling / get-up offsets. Long fantasy launches keep the proportional check.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "RPG_CODEX.h"
#include "RPGCharacterStatsComponent.h"
#include "RPGNpcCharacter.h"
#include "RPGAttributeSet.h"
#include "RPGPhysicsReactionComponent.h"
#include "RPGHitOutcomeComponent.h"
#include "RPGPowerArena.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

namespace
{
	const float SweepStrengths[] = { 6.f, 8.f, 10.f, 12.f, 14.f, 16.f, 18.f, 20.f };

	class FRPGPowerCalibrationCommand : public IAutomationLatentCommand
	{
	public:
		explicit FRPGPowerCalibrationCommand(FAutomationTestBase* InTest, int32 InCase) : Test(InTest), Case(InCase), EndCase(InCase + 1) {}

		virtual bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World) return Timeout(TEXT("no play world"));
			if (!Dummy.IsValid()) Dummy = RPGPowerArena::FindReferenceNpc(World);
			ACharacter* Player = UGameplayStatics::GetPlayerCharacter(World, 0);
			URPGCharacterStatsComponent* PlayerStats = URPGCharacterStatsComponent::FindStats(Player);
			if (!Dummy.IsValid() || !Player || !PlayerStats) return Timeout(TEXT("dummy, player or stats not found"));
			ARPGNpcCharacter* D = Dummy.Get();
			USkeletalMeshComponent* Mesh = D->GetMesh();
			const float Now = World->GetTimeSeconds();

			if (!bArenaBuilt)
			{
				Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				ArenaTop = D->GetActorLocation() + FVector(0.f, 0.f, 8000.f);
				if (!RPGPowerArena::BuildFlatFloor(World, ArenaTop, 300.f)) { Test->AddError(TEXT("RPGTEST could not build the flat floor")); return true; }
				Player->SetActorLocation(ArenaTop + FVector(0.f, 4000.f, 120.f), false, nullptr, ETeleportType::ResetPhysics);
				bArenaBuilt = true;
				PhaseStart = Now;
				return false;
			}

			if (Case >= EndCase) return Finish();
			const float Strength = SweepStrengths[Case];

			switch (Phase)
			{
			case 0:   // place: back on its feet, full health, at the arena spot, facing -X (hits throw it that way)
			{
				// (back on its feet and settled from the last case first; then full health)
				const bool bReady = !D->IsBusy() && D->PhysicsReaction->GetReactionState() == ERPGPhysicsReactionState::Animated
					&& !D->PhysicsReaction->IsReturning();
				if (!bReady && Now - PhaseStart < 20.f) return false;
				if (const URPGAttributeSet* A = D->Stats->GetAttributes()) D->Stats->SetStatBaseByName(TEXT("Health"), A->GetMaxHealth());
				D->SetActorLocationAndRotation(ArenaTop + FVector(0.f, 0.f, D->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.f),
					FRotator(0.f, 180.f, 0.f), false, nullptr, ETeleportType::ResetPhysics);
				D->GetCharacterMovement()->StopMovementImmediately();   // (the retired dummy's reset did this: no slide left over from the last case)
				PlayerStats->SetStatBaseByName(TEXT("Strength"), Strength);
				Phase = 1; PhaseStart = Now;
				return false;
			}

			case 1:   // settle, then hit
			{
				// Actor velocity alone can be zero while the get-up / locomotion pose is still moving its feet.
				const FVector Left = Mesh->GetBoneLocation(TEXT("foot_l")), Right = Mesh->GetBoneLocation(TEXT("foot_r"));
				const FVector Pelvis = Mesh->GetBoneLocation(TEXT("pelvis"));
				const float Dt = FMath::Max(World->GetDeltaSeconds(), SMALL_NUMBER);
				const bool bPoseQuiet = bHavePose && FVector::Dist2D(Left, LastLeft) / Dt < 5.f
					&& FVector::Dist2D(Right, LastRight) / Dt < 5.f && FVector::Dist2D(Pelvis, LastPelvis) / Dt < 15.f;
				PoseQuietTime = bPoseQuiet && D->GetVelocity().Size2D() < 5.f ? PoseQuietTime + Dt : 0.f;
				LastLeft = Left; LastRight = Right; LastPelvis = Pelvis; bHavePose = true;
				if (Now - PhaseStart < .5f || PoseQuietTime < .25f)
				{
					if (Now - PhaseStart < 5.f) return false;
					Test->AddError(TEXT("RPGCALIB fixture did not reach a stationary standing pose")); return true;
				}
				StartSpot = D->GetActorLocation();
				StartPelvis = Mesh->GetBoneLocation(TEXT("pelvis"));
				FRPGMeleeHitInfo Hit;
				Hit.AttackName = TEXT("Calibration Punch");
				Hit.Attacker = Player;
				Hit.HitActor = D;
				Hit.HitBone = TEXT("spine_05");
				Hit.ImpactLocation = Mesh->GetBoneLocation(Hit.HitBone);
				Hit.StrikeDirection = D->GetActorForwardVector();
				IRPGMeleeTarget::Execute_ReceiveMeleeHit(D, Hit);
				Expected = D->LastPowerResult;
				bSawLimp = false; AtLimp = AtLimpEnd = -30.48f;
				bSawGrip = false; AfterGripMoved = 0.f; FirstLimpTime = -1.f;
				PhysicsDistance = 0.f;
				Phase = 2; PhaseStart = Now;
				return false;
			}

			case 2:   // watch until it is standing still on its feet again
			{
				const ERPGPhysicsReactionState State = D->PhysicsReaction->GetReactionState();
				// (where it is at each stage: the push before it goes down, the ragdoll, the get-up)
				if (State == ERPGPhysicsReactionState::Limp && !bSawLimp)
				{
					AtLimp = float(FVector::Dist2D(D->GetMesh()->GetBoneLocation(TEXT("pelvis")), StartSpot));
					FirstLimpTime = Now - PhaseStart;
				}
				// Freeze the physical endpoint before the get-up fade starts moving the displayed pelvis.
				if (State == ERPGPhysicsReactionState::Limp && !D->PhysicsReaction->IsReturning())
				{
					AtLimpEnd = float(FVector::Dist2D(Mesh->GetBoneLocation(TEXT("pelvis")), StartSpot));
					PhysicsDistance = float(FVector::Dist2D(Mesh->GetBoneLocation(TEXT("pelvis")), StartPelvis));
				}
				bSawLimp |= State == ERPGPhysicsReactionState::Limp;
				if (State == ERPGPhysicsReactionState::Limp && D->PhysicsReaction->IsGroundGripping())
				{
					const FVector Pelvis = Mesh->GetBoneLocation(TEXT("pelvis"));
					if (!bSawGrip) { GripSpot = Pelvis; bSawGrip = true; }
					AfterGripMoved = FMath::Max(AfterGripMoved, float(FVector::Dist2D(Pelvis, GripSpot)));
				}
				const bool bStill = State == ERPGPhysicsReactionState::Animated && !D->PhysicsReaction->IsReturning();
				if (!(bStill && Now - PhaseStart > 2.f) && Now - PhaseStart < 15.f) return false;

				const float Actual = float(FVector::Dist2D(D->GetActorLocation(), StartSpot));
				const bool bLaunch = Expected.Reaction == ERPGPowerReaction::Launch;
				// The launch model covers arc + slide, not the selected get-up's pelvis-to-capsule placement.
				const float Measured = bSawLimp ? PhysicsDistance : Actual;
				const float Ratio = Expected.Displacement > 1.f ? Measured / Expected.Displacement : 0.f;
				const bool bShortLaunch = Expected.Displacement < 8.f * 30.48f;
				const bool bLaunchOk = !bLaunch || (bSawLimp && (bShortLaunch
					? FMath::Abs(Measured - Expected.Displacement) <= 3.f * 30.48f
					: (Ratio > 0.7f && Ratio < 1.35f)));
				const bool bFallOk = Expected.Reaction != ERPGPowerReaction::Knockdown || (bSawGrip && AfterGripMoved <= 2.f * 30.48f
					&& (FirstLimpTime > .2f || PhysicsDistance <= Expected.Displacement + 2.f * 30.48f));
				const bool bOk = bLaunchOk && bFallOk && bStill;
				bAllOk &= bOk;
				if (bLaunch) { RatioSum += Ratio; ++Launches; }

				const FString Line = FString::Printf(TEXT("RPGCALIB Strength %4.1f  %-9s  ladder max %6.1f ft  calculated %6.1f ft  measured %6.1f ft  (%3.0f%%)  ragdoll %d  damage %5.1f  (down at %.1f ft, still at %.1f ft)%s"),
					Strength, *UEnum::GetDisplayValueAsText(Expected.Reaction).ToString(), Expected.MaxDisplacement / 30.48f,
					Expected.Displacement / 30.48f, Measured / 30.48f, Ratio * 100.f, bSawLimp, Expected.Damage, AtLimp / 30.48f, AtLimpEnd / 30.48f, bOk ? TEXT("") : TEXT("   <-- OUT OF RANGE"));
				UE_LOG(LogRPGInput, Display, TEXT("%s"), *Line);
				Test->AddInfo(Line);
				if (bLaunch)
					Test->AddInfo(FString::Printf(TEXT("RPGCALIB launch physical travel %.1f ft (%.0f%%); final actor %.1f ft (get-up included)"),
						PhysicsDistance / 30.48f, Ratio * 100.f, Actual / 30.48f));
				if (Expected.Reaction == ERPGPowerReaction::Knockdown)
					Test->AddInfo(FString::Printf(TEXT("RPGCALIB knockdown grip %d, travel after grip %.1f ft"), bSawGrip, AfterGripMoved / 30.48f));
				++Case; Phase = 0;
				return false;
			}
			}
			return false;
		}

	private:
		bool Finish()
		{
			const FString Line = FString::Printf(TEXT("RPGTEST %s  Power calibration Strength %.1f: %d launches, %.0f%% of the calculated distance"),
				bAllOk ? TEXT("PASS") : TEXT("FAIL"), SweepStrengths[EndCase - 1], Launches, Launches > 0 ? 100.f * RatioSum / Launches : 0.f);
			UE_LOG(LogRPGInput, Display, TEXT("%s"), *Line);
			if (bAllOk) Test->AddInfo(Line); else Test->AddError(Line);
			return true;
		}

		bool Timeout(const TCHAR* Why)
		{
			if (++WaitFrames < 600) return false;
			Test->AddError(FString::Printf(TEXT("RPGTEST could not start: %s"), Why));
			return true;
		}

		FAutomationTestBase* Test;
		TWeakObjectPtr<ARPGNpcCharacter> Dummy;
		int32 WaitFrames = 0, Case = 0, Phase = 0, Launches = 0;
		int32 EndCase;
		bool bHavePose = false;
		float PoseQuietTime = 0.f;
		FVector LastLeft, LastRight, LastPelvis;
		bool bArenaBuilt = false, bSawLimp = false, bAllOk = true;
		float PhaseStart = 0.f, RatioSum = 0.f, AtLimp = -30.48f, AtLimpEnd = -30.48f;
		FVector ArenaTop = FVector::ZeroVector, StartSpot = FVector::ZeroVector;
		FRPGPowerResult Expected;
		FVector StartPelvis = FVector::ZeroVector;
		float PhysicsDistance = 0.f;
		float FirstLimpTime = -1.f;
		bool bSawGrip = false;
		float AfterGripMoved = 0.f;
		FVector GripSpot = FVector::ZeroVector;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPowerCalibrationTest, "RPG.Power.Calibration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRPGPowerCalibrationTest::RunTest(const FString& Parameters)
{
	// Each roll starts from a fresh Standard Bandit, without a prior catch, get-up, balance loss or body velocity.
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(SweepStrengths); ++Index)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.1f));
		ADD_LATENT_AUTOMATION_COMMAND(FRPGPowerCalibrationCommand(this, Index));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	}
	return true;
}

namespace
{
	// Exercise actual knockdowns without relying on the random power roll to select one instead of a launch.
	class FGroundGripCommand : public IAutomationLatentCommand
	{
	public:
		explicit FGroundGripCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World) { if (++Wait < 600) return false; Test->AddError(TEXT("GroundGrip: no PIE world")); return true; }
			if (!Npc.IsValid()) Npc = RPGPowerArena::FindReferenceNpc(World);
			if (!Npc.IsValid()) { Test->AddError(TEXT("GroundGrip: no reference NPC")); return true; }
			ARPGNpcCharacter* N = Npc.Get();
			const float Now = World->GetTimeSeconds();
			if (Phase == 0)
			{
				Top = N->GetActorLocation() + FVector(0, 0, 8000);
				if (!RPGPowerArena::BuildFlatFloor(World, Top, 300.f)) { Test->AddError(TEXT("GroundGrip: floor failed")); return true; }
				N->SetActorLocationAndRotation(Top + FVector(0, 0, N->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1),
					FRotator(0, 180, 0), false, nullptr, ETeleportType::ResetPhysics);
				N->GetCharacterMovement()->StopMovementImmediately();
				N->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				Started = Now; Phase = 1;
				return false;
			}
			if (Phase == 1)
			{
				if (Now - Started < .5f) return false;
				FRPGMeleeHitInfo Hit; Hit.HitBone = TEXT("spine_05"); Hit.ImpactLocation = N->GetMesh()->GetBoneLocation(Hit.HitBone);
				Hit.StrikeDirection = FVector(-1, 0, 0);
				StartPelvis = N->GetMesh()->GetBoneLocation(TEXT("pelvis"));
				// The roughly 700 cm/s straight knockdown reproduced the old missed-pelvis-deceleration slide.
				FRPGPowerResult Result; Result.Reaction = ERPGPowerReaction::Knockdown; Result.DisplacementVelocity = FVector(-700, 0, 0);
				Result.Displacement = 140.f;
				N->HitOutcome->BeginFall(Result.DisplacementVelocity, false);
				N->PhysicsReaction->HandlePowerHit(Hit, Result);
				Started = Now; Phase = 2;
				return false;
			}
			const bool bLimp = N->PhysicsReaction->GetReactionState() == ERPGPhysicsReactionState::Limp && !N->PhysicsReaction->IsReturning();
			if (bLimp) TotalTravel = FMath::Max(TotalTravel, float(FVector::Dist2D(N->GetMesh()->GetBoneLocation(TEXT("pelvis")), StartPelvis)));
			if (bLimp && N->PhysicsReaction->IsGroundGripping())
			{
				const FVector P = N->GetMesh()->GetBoneLocation(TEXT("pelvis"));
				if (!bSawGrip) { bSawGrip = true; GripSpot = P; GripAt = Now - Started; }
				Moved = FMath::Max(Moved, float(FVector::Dist2D(P, GripSpot)));
			}
			if ((Now - Started < 5.f && bLimp) || Now - Started < .5f) return false;
			const FString Line = FString::Printf(TEXT("GROUNDGRIP grip %d at %.2f s, pelvis moved %.0f cm after grip (limit 61 cm), total %.0f cm (limit 201 cm)"), bSawGrip, GripAt, Moved, TotalTravel);
			if (bSawGrip && Moved <= 2.f * 30.48f && TotalTravel <= 140.f + 2.f * 30.48f) Test->AddInfo(Line); else Test->AddError(Line);
			return true;
		}
	private:
		FAutomationTestBase* Test;
		TWeakObjectPtr<ARPGNpcCharacter> Npc;
		int32 Wait = 0, Phase = 0;
		float Started = 0, Moved = 0, GripAt = -1;
		bool bSawGrip = false;
		FVector Top, GripSpot;
		FVector StartPelvis;
		float TotalTravel = 0.f;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGGroundGripTest, "RPG.Power.GroundGrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRPGGroundGripTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FGroundGripCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
