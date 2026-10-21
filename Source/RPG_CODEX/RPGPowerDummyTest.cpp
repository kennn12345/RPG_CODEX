// Editor automation test for the Power System on the combat test dummy: Session Frontend > Automation >
// RPG.Power.DummyLaunch, or UnrealEditor-Cmd RPG_CODEX.uproject -ExecCmds="Automation RunTests RPG.Power.DummyLaunch"
// -TestExit="Automation Test Queue Empty" -unattended -nullrhi
// Plays the startup map, builds a flat 200 m floor high above the level (the map's dummy stands on a raised platform
// with drops and slopes around it), stands the dummy on it, gives the player Strength 12 and hits the dummy once: it
// must take the calculated damage, be launched (LIMP), land about the calculated distance away (it slides a little
// after landing), and get up where it landed instead of snapping back to where it stood.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "RPG_CODEX.h"
#include "RPGAttributeSet.h"
#include "RPGCharacterStatsComponent.h"
#include "RPGCombatTestDummy.h"
#include "RPGPhysicsReactionComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

namespace
{
	constexpr float TestStrength = 12.f;

	class FRPGPowerDummyLaunchCommand : public IAutomationLatentCommand
	{
	public:
		explicit FRPGPowerDummyLaunchCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World) return Timeout(TEXT("no play world"));
			if (!Dummy.IsValid())
				for (TActorIterator<ARPGCombatTestDummy> It(World); It; ++It) Dummy = *It;
			ACharacter* Player = UGameplayStatics::GetPlayerCharacter(World, 0);
			URPGCharacterStatsComponent* PlayerStats = URPGCharacterStatsComponent::FindStats(Player);
			if (!Dummy.IsValid() || !Player || !PlayerStats || !Dummy->Stats) return Timeout(TEXT("dummy, player or stats not found"));
			ARPGCombatTestDummy* D = Dummy.Get();
			const float Now = World->GetTimeSeconds();

			if (Start < 0.f)
			{
				D->Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				D->StopWanderingAtHome();

				// Flat floor: the engine cube scaled to 200 m x 200 m x 1 m, its top 80 m above the dummy's spot.
				const FVector ArenaTop = D->GetActorLocation() + FVector(0.f, 0.f, 8000.f);
				AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>(ArenaTop - FVector(0.f, 0.f, 50.f), FRotator::ZeroRotator);
				UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
				if (!Floor || !Cube) { Test->AddError(TEXT("RPGTEST could not build the flat test floor")); return true; }
				Floor->SetMobility(EComponentMobility::Movable);
				Floor->GetStaticMeshComponent()->SetStaticMesh(Cube);
				Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
				Floor->SetActorScale3D(FVector(200.f, 200.f, 1.f));

				// Dummy standing on it; player parked well off to the side (out of the flight path).
				D->SetActorLocation(ArenaTop + FVector(0.f, 0.f, D->Capsule->GetScaledCapsuleHalfHeight() + 1.f), false, nullptr, ETeleportType::ResetPhysics);
				Player->SetActorLocation(ArenaTop + FVector(0.f, 3000.f, 120.f), false, nullptr, ETeleportType::ResetPhysics);
				D->bUsePowerSystem = true;
				PlayerStats->SetStatBaseByName(TEXT("Strength"), TestStrength);
				Start = Now;
				return false;
			}

			if (!bHit)
			{
				if (Now - Start < 0.5f) return false;
				const URPGAttributeSet* A = D->Stats->GetAttributes();
				HealthBefore = A->GetHealth();
				StartSpot = D->GetActorLocation();

				// A normal punch to the chest, throwing it the way it faces.
				FRPGMeleeHitInfo Hit;
				Hit.AttackName = TEXT("Test Power Punch");
				Hit.Attacker = Player;
				Hit.HitActor = D;
				Hit.HitBone = TEXT("spine_05");
				Hit.ImpactLocation = D->Mesh->GetBoneLocation(Hit.HitBone);
				Hit.StrikeDirection = D->GetActorForwardVector();
				IRPGMeleeTarget::Execute_ReceiveMeleeHit(D, Hit);
				bHit = true;
				HitAt = Now;
				Expected = D->LastPowerResult;
				HealthAfterHit = A->GetHealth();
				return false;
			}

			const ERPGPhysicsReactionState State = D->PhysicsReaction->GetReactionState();
			if (State == ERPGPhysicsReactionState::Limp) bSawLimp = true;

			// Flight trace every 0.25 s (diagnostics for calibration).
			if (Now - LastTrace >= 0.25f)
			{
				LastTrace = Now;
				const FVector P = D->Mesh->GetBoneLocation(TEXT("pelvis"));
				const FBodyInstance* Body = D->Mesh->GetBodyInstance(TEXT("pelvis"));
				const FVector V = Body ? Body->GetUnrealWorldVelocity() : FVector::ZeroVector;
				UE_LOG(LogRPGInput, Display, TEXT("RPGLAUNCH t=%.2f state %d pelvis (%.0f, %.0f, %.0f) %.0f cm out, velocity (%.0f, %.0f, %.0f)"),
					Now - HitAt, int32(State), P.X, P.Y, P.Z, FVector::Dist2D(P, StartSpot), V.X, V.Y, V.Z);
			}

			// While fading back to the animation the body must stay near the actor (no snap back to the start).
			if (D->PhysicsReaction->IsReturning())
				MaxReturnGap = FMath::Max(MaxReturnGap, float(FVector::Dist2D(D->Mesh->GetBoneLocation(TEXT("pelvis")), D->GetActorLocation())));

			const bool bDone = bSawLimp && State == ERPGPhysicsReactionState::Animated;
			if (!bDone && Now - HitAt < 12.f) return false;

			const float Landed = float(FVector::Dist2D(D->GetActorLocation(), StartSpot));
			const float Ratio = Expected.Displacement > 0.f ? Landed / Expected.Displacement : 0.f;
			const bool bDamageOk = FMath::IsNearlyEqual(HealthBefore - HealthAfterHit, FMath::Min(Expected.Damage, HealthBefore), 0.5f);
			// On flat ground arc + slide should cover the calculated distance; ragdolls tumble differently each time.
			const bool bPass = bDone && Expected.Reaction == ERPGPowerReaction::Launch && bDamageOk && Ratio > 0.7f && Ratio < 1.35f && MaxReturnGap < 80.f;

			const FString Line = FString::Printf(TEXT("RPGTEST %s  Power launch (Strength %.0f normal punch on the standard bandit): reaction %s, ")
				TEXT("damage %.1f (health %.0f -> %.0f), calculated displacement %.1f ft, got up %.1f ft away (%.0f%% of calculated), ")
				TEXT("ragdoll seen %d, back to animated %d, largest body-to-actor gap while standing up %.0f cm"),
				bPass ? TEXT("PASS") : TEXT("FAIL"), TestStrength, *UEnum::GetDisplayValueAsText(Expected.Reaction).ToString(),
				Expected.Damage, HealthBefore, HealthAfterHit, Expected.Displacement / 30.48f, Landed / 30.48f, Ratio * 100.f,
				bSawLimp, bDone, MaxReturnGap);
			UE_LOG(LogRPGInput, Display, TEXT("%s"), *Line);
			if (bPass) Test->AddInfo(Line); else Test->AddError(Line);
			return true;
		}

	private:
		bool Timeout(const TCHAR* Why)
		{
			if (++WaitFrames < 600) return false;
			Test->AddError(FString::Printf(TEXT("RPGTEST could not start: %s"), Why));
			return true;
		}

		FAutomationTestBase* Test;
		TWeakObjectPtr<ARPGCombatTestDummy> Dummy;
		int32 WaitFrames = 0;
		float Start = -1.f, HitAt = 0.f, HealthBefore = 0.f, HealthAfterHit = 0.f, MaxReturnGap = 0.f, LastTrace = -1.f;
		bool bHit = false, bSawLimp = false;
		FVector StartSpot = FVector::ZeroVector;
		FRPGPowerResult Expected;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPowerDummyLaunchTest, "RPG.Power.DummyLaunch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRPGPowerDummyLaunchTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRPGPowerDummyLaunchCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
