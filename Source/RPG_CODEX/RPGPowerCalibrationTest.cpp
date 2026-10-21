// Power System calibration sweep: Session Frontend > Automation > RPG.Power.Calibration, or
// UnrealEditor-Cmd RPG_CODEX.uproject -ExecCmds="Automation RunTests RPG.Power.Calibration"
// -TestExit="Automation Test Queue Empty" -unattended -nullrhi
// On a flat floor above the level, punches the standard bandit (normal punch) at each Strength in the sweep and
// measures how far it really ended up against the distance the Power System calculated. Prints a table (search the
// log for RPGCALIB). Launches must end up 70-135% of the calculated distance; ragdolls tumble differently every time.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "RPG_CODEX.h"
#include "RPGCharacterStatsComponent.h"
#include "RPGCombatTestDummy.h"
#include "RPGPhysicsReactionComponent.h"
#include "RPGPowerArena.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
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
		explicit FRPGPowerCalibrationCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World) return Timeout(TEXT("no play world"));
			if (!Dummy.IsValid())
				for (TActorIterator<ARPGCombatTestDummy> It(World); It; ++It) Dummy = *It;
			ACharacter* Player = UGameplayStatics::GetPlayerCharacter(World, 0);
			URPGCharacterStatsComponent* PlayerStats = URPGCharacterStatsComponent::FindStats(Player);
			if (!Dummy.IsValid() || !Player || !PlayerStats) return Timeout(TEXT("dummy, player or stats not found"));
			ARPGCombatTestDummy* D = Dummy.Get();
			const float Now = World->GetTimeSeconds();

			if (!bArenaBuilt)
			{
				D->Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				D->StopWanderingAtHome();
				D->bUsePowerSystem = true;
				ArenaTop = D->GetActorLocation() + FVector(0.f, 0.f, 8000.f);
				if (!RPGPowerArena::BuildFlatFloor(World, ArenaTop, 300.f)) { Test->AddError(TEXT("RPGTEST could not build the flat floor")); return true; }
				Player->SetActorLocation(ArenaTop + FVector(0.f, 4000.f, 120.f), false, nullptr, ETeleportType::ResetPhysics);
				bArenaBuilt = true;
				PhaseStart = Now;
				return false;
			}

			if (Case >= UE_ARRAY_COUNT(SweepStrengths)) return Finish();
			const float Strength = SweepStrengths[Case];

			switch (Phase)
			{
			case 0:   // place: back on its feet, full health, at the arena spot, facing -X (hits throw it that way)
				D->ResetDummy();
				D->SetActorLocationAndRotation(ArenaTop + FVector(0.f, 0.f, D->Capsule->GetScaledCapsuleHalfHeight() + 1.f),
					FRotator(0.f, 180.f, 0.f), false, nullptr, ETeleportType::ResetPhysics);
				PlayerStats->SetStatBaseByName(TEXT("Strength"), Strength);
				Phase = 1; PhaseStart = Now;
				return false;

			case 1:   // settle, then hit
			{
				if (Now - PhaseStart < 0.5f) return false;
				StartSpot = D->GetActorLocation();
				FRPGMeleeHitInfo Hit;
				Hit.AttackName = TEXT("Calibration Punch");
				Hit.Attacker = Player;
				Hit.HitActor = D;
				Hit.HitBone = TEXT("spine_05");
				Hit.ImpactLocation = D->Mesh->GetBoneLocation(Hit.HitBone);
				Hit.StrikeDirection = D->GetActorForwardVector();
				IRPGMeleeTarget::Execute_ReceiveMeleeHit(D, Hit);
				Expected = D->LastPowerResult;
				bSawLimp = false;
				Phase = 2; PhaseStart = Now;
				return false;
			}

			case 2:   // watch until it is standing still on its feet again
			{
				const ERPGPhysicsReactionState State = D->PhysicsReaction->GetReactionState();
				bSawLimp |= State == ERPGPhysicsReactionState::Limp;
				const bool bStill = State == ERPGPhysicsReactionState::Animated && !D->PhysicsReaction->IsReturning();
				if (!(bStill && Now - PhaseStart > 2.f) && Now - PhaseStart < 15.f) return false;

				const float Actual = float(FVector::Dist2D(D->GetActorLocation(), StartSpot));
				const float Ratio = Expected.Displacement > 1.f ? Actual / Expected.Displacement : 0.f;
				const bool bLaunch = Expected.Reaction == ERPGPowerReaction::Launch;
				const bool bOk = !bLaunch || (Ratio > 0.7f && Ratio < 1.35f);
				bAllOk &= bOk;
				if (bLaunch) { RatioSum += Ratio; ++Launches; }

				const FString Line = FString::Printf(TEXT("RPGCALIB Strength %4.1f  %-9s  ladder max %6.1f ft  calculated %6.1f ft  actual %6.1f ft  (%3.0f%%)  ragdoll %d  damage %5.1f%s"),
					Strength, *UEnum::GetDisplayValueAsText(Expected.Reaction).ToString(), Expected.MaxDisplacement / 30.48f,
					Expected.Displacement / 30.48f, Actual / 30.48f, Ratio * 100.f, bSawLimp, Expected.Damage, bOk ? TEXT("") : TEXT("   <-- OUT OF RANGE"));
				UE_LOG(LogRPGInput, Display, TEXT("%s"), *Line);
				Test->AddInfo(Line);
				++Case; Phase = 0;
				return false;
			}
			}
			return false;
		}

	private:
		bool Finish()
		{
			const FString Line = FString::Printf(TEXT("RPGTEST %s  Power calibration sweep: %d launches, average %.0f%% of the calculated distance"),
				bAllOk ? TEXT("PASS") : TEXT("FAIL"), Launches, Launches > 0 ? 100.f * RatioSum / Launches : 0.f);
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
		TWeakObjectPtr<ARPGCombatTestDummy> Dummy;
		int32 WaitFrames = 0, Case = 0, Phase = 0, Launches = 0;
		bool bArenaBuilt = false, bSawLimp = false, bAllOk = true;
		float PhaseStart = 0.f, RatioSum = 0.f;
		FVector ArenaTop = FVector::ZeroVector, StartSpot = FVector::ZeroVector;
		FRPGPowerResult Expected;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPowerCalibrationTest, "RPG.Power.Calibration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRPGPowerCalibrationTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRPGPowerCalibrationCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
