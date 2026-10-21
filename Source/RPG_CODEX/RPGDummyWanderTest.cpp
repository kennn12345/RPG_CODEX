// Editor automation test for the dummy's wandering: Session Frontend > Automation > RPG.Combat.DummyWander, or
// UnrealEditor-Cmd RPG_CODEX.uproject -ExecCmds="Automation RunTests RPG.Combat.DummyWander;Quit" -unattended -nullrhi
// Plays the startup map with the player parked well away, watches the dummy for a while, then hits it once.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "RPG_CODEX.h"
#include "RPGCombatTestDummy.h"
#include "RPGPhysicsReactionComponent.h"
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
	class FRPGDummyWanderCommand : public IAutomationLatentCommand
	{
	public:
		explicit FRPGDummyWanderCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World) return Timeout(TEXT("no play world"));
			if (!Dummy.IsValid())
				for (TActorIterator<ARPGCombatTestDummy> It(World); It; ++It) Dummy = *It;
			ACharacter* Player = UGameplayStatics::GetPlayerCharacter(World, 0);
			if (!Dummy.IsValid() || !Player) return Timeout(TEXT("dummy or player not found"));
			const float Now = World->GetTimeSeconds();
			ARPGCombatTestDummy* D = Dummy.Get();
			if (Start < 0.f)
			{
				// Park the player far off so it never blocks the walk.
				Player->SetActorLocation(D->GetActorLocation() + FVector(0.f, 1500.f, 50.f), false, nullptr, ETeleportType::ResetPhysics);
				D->Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				D->bUsePowerSystem = false;   // a fixed reaction to the one hit, not a Power System roll
				Start = Now; Origin = D->GetActorLocation(); FloorZ = Origin.Z; Last = Origin;
				return false;
			}
			const FVector L = D->GetActorLocation();
			const float Step = FVector::Dist2D(L, Last);
			if (Step > 0.05f)
			{
				const FVector Moved = (L - Last).GetSafeNormal2D();
				FacingWorst = FMath::Min(FacingWorst, float(FVector::DotProduct(Moved, D->GetActorForwardVector().GetSafeNormal2D())));
				MaxSpeed = FMath::Max(MaxSpeed, Step / FMath::Max(World->GetDeltaSeconds(), 1e-3f));
			}
			Travelled += Step;
			FarthestFromHome = FMath::Max(FarthestFromHome, float(FVector::Dist2D(L, Origin)));
			// height may change over the walk (steps, ramps); a single frame must never jump more than one step
			FloorDrift = FMath::Max(FloorDrift, FMath::Abs(float(L.Z - Last.Z)));
			Last = L;
			if (!bHit && Now - Start >= 10.f)
			{
				// Hit it mid-walk (or wherever it is): it must stop and stand while reacting.
				FRPGMeleeHitInfo Hit;
				Hit.AttackName = TEXT("Test Hit");
				Hit.HitBone = TEXT("spine_05");
				Hit.ImpactLocation = D->Mesh->GetBoneLocation(Hit.HitBone);
				Hit.StrikeDirection = D->GetActorForwardVector() * -1.f;
				SpeedAtHit = MaxRecentSpeed();
				IRPGMeleeTarget::Execute_ReceiveMeleeHit(D, Hit);
				bHit = true; HitAt = Now; HitSpot = L;
				return false;
			}
			if (bHit && Now - HitAt < 1.0f) { MovedAfterHit = FMath::Max(MovedAfterHit, float(FVector::Dist2D(L, HitSpot))); return false; }
			if (!bHit) { Recent.Add(Step / FMath::Max(World->GetDeltaSeconds(), 1e-3f)); if (Recent.Num() > 10) Recent.RemoveAt(0); return false; }

			const float Radius = D->WanderRadius;
			const bool bPass = Travelled > 150.f && FarthestFromHome <= Radius + 40.f && FloorDrift <= D->MaxStepHeight + 2.f && FacingWorst > 0.5f
				&& MaxSpeed <= D->WalkSpeed * 1.2f && MovedAfterHit < 2.f;
			const FString Line = FString::Printf(TEXT("RPGTEST %s  Dummy wander: walked %.0f cm in 10 s, farthest %.0f cm from its start (radius %.0f), largest one-frame height change %.1f cm (steps up to %.0f allowed), ")
				TEXT("worst facing vs travel %.2f, top speed %.0f cm/s (walk %.0f), speed when hit %.0f, moved %.1f cm in the second after the hit"),
				bPass ? TEXT("PASS") : TEXT("FAIL"), Travelled, FarthestFromHome, Radius, FloorDrift, D->MaxStepHeight, FacingWorst, MaxSpeed, D->WalkSpeed, SpeedAtHit, MovedAfterHit);
			UE_LOG(LogRPGInput, Display, TEXT("%s"), *Line);
			if (bPass) Test->AddInfo(Line); else Test->AddError(Line);
			return true;
		}

	private:
		float MaxRecentSpeed() const { float M = 0.f; for (float V : Recent) M = FMath::Max(M, V); return M; }
		bool Timeout(const TCHAR* Why)
		{
			if (++WaitFrames < 600) return false;
			Test->AddError(FString::Printf(TEXT("RPGTEST could not start: %s"), Why));
			return true;
		}

		FAutomationTestBase* Test;
		TWeakObjectPtr<ARPGCombatTestDummy> Dummy;
		int32 WaitFrames = 0;
		float Start = -1.f, FloorZ = 0.f, Travelled = 0.f, FarthestFromHome = 0.f, FloorDrift = 0.f, FacingWorst = 1.f, MaxSpeed = 0.f;
		bool bHit = false; float HitAt = 0.f, SpeedAtHit = 0.f, MovedAfterHit = 0.f;
		FVector Origin = FVector::ZeroVector, Last = FVector::ZeroVector, HitSpot = FVector::ZeroVector;
		TArray<float> Recent;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDummyWanderTest, "RPG.Combat.DummyWander",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRPGDummyWanderTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRPGDummyWanderCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
