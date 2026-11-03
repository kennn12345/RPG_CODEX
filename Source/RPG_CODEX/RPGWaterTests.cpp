// Water gameplay checks on L_WaterTest (no rendering needed):
//   RPG.Water.Buoyancy - floating props settle at the surface (not sunk, not launched); the river log drifts downstream.
//   RPG.Water.Wading   - a CharacterMovement walker goes from dry ground into wading depth and over its head: wading is
//                        slower, deep water is "walk along the bottom" (slower, lower gravity), and leaving restores it.
//   UnrealEditor-Cmd.exe RPG_CODEX.uproject -ExecCmds="Automation RunTests RPG.Water" -TestExit="Automation Test Queue Empty" -unattended -nullrhi

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Components/CapsuleComponent.h"
#include "Editor.h"
#include "Engine/TargetPoint.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "RPGBuoyancyComponent.h"
#include "RPGFloatingProp.h"
#include "RPGWaterZoneComponent.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

namespace
{
	void StartWaterPIE()
	{
		FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/RPG/Maps/L_WaterTest"));
		FRequestPlaySessionParams Session;
		Session.WorldType = EPlaySessionWorldType::PlayInEditor;
		Session.EditorPlaySettings = NewObject<ULevelEditorPlaySettings>();
		GEditor->RequestPlaySession(Session);
	}

	class FBuoyancyCheck : public IAutomationLatentCommand
	{
	public:
		explicit FBuoyancyCheck(FAutomationTestBase* InTest) : Test(InTest) {}
		virtual bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World) return ++Frames > 900 ? (Test->AddError(TEXT("no play world")), true) : false;
			if (Start < 0.f)
			{
				Start = World->GetTimeSeconds();
				for (TActorIterator<ARPGFloatingProp> It(World); It; ++It) Origins.Add(*It, It->GetActorLocation());
				if (Origins.IsEmpty()) { Test->AddError(TEXT("no ARPGFloatingProp in L_WaterTest")); return true; }
				return false;
			}
			if (World->GetTimeSeconds() - Start < 8.f) return false;
			for (const TPair<TWeakObjectPtr<ARPGFloatingProp>, FVector>& P : Origins)
			{
				ARPGFloatingProp* Prop = P.Key.Get();
				if (!Prop) { Test->AddError(TEXT("floating prop destroyed (fell out of the world?)")); continue; }
				const FVector L = Prop->GetActorLocation();
				const URPGWaterZoneComponent* Zone = URPGWaterZoneComponent::FindZone(World, L);
				const float Surface = Zone ? Zone->GetSurfaceHeight(L) : -1e6f;
				const float Offset = L.Z - Surface;
				Test->AddInfo(FString::Printf(TEXT("%s: z - surface = %.0f cm, in water %d, moved %.0f cm"), *Prop->GetActorNameOrLabel(),
					Offset, Prop->Buoyancy->bInWater, FVector::Dist2D(L, P.Value)));
				Test->TestTrue(FString::Printf(TEXT("%s floats at the surface"), *Prop->GetActorNameOrLabel()), Zone && FMath::Abs(Offset) < 90.f);
				if (Prop->ActorHasTag(TEXT("RiverDrift")) && Zone)
				{
					const float Downstream = FVector::DotProduct(L - P.Value, Zone->FlowVelocity.GetSafeNormal());
					Test->TestTrue(FString::Printf(TEXT("%s drifts downstream (%.0f cm)"), *Prop->GetActorNameOrLabel(), Downstream), Downstream > 150.f);
				}
			}
			return true;
		}
	private:
		FAutomationTestBase* Test;
		TMap<TWeakObjectPtr<ARPGFloatingProp>, FVector> Origins;
		float Start = -1.f;
		int32 Frames = 0;
	};

	class FWadingCheck : public IAutomationLatentCommand
	{
	public:
		explicit FWadingCheck(FAutomationTestBase* InTest) : Test(InTest) {}
		virtual bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World) return ++Frames > 900 ? (Test->AddError(TEXT("no play world")), true) : false;
			// A plain CharacterMovement walker under AI control: the GASP player's animation-driven locomotion does not run
			// headless, so it cannot measure speed here. The zone treats every ACharacter the same way.
			if (!Walker)
			{
				FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				Walker = World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FVector(0.f, 0.f, 5000.f), FRotator::ZeroRotator, P);
				Walker->SpawnDefaultController();
			}
			ACharacter* Player = Walker;
			UCharacterMovementComponent* Move = Player->GetCharacterMovement();
			const float Now = World->GetTimeSeconds();
			if (Points.IsEmpty())
			{
				for (const TCHAR* Tag : { TEXT("WaterTest_Dry"), TEXT("WaterTest_Wade"), TEXT("WaterTest_Deep"), TEXT("WaterTest_Dry") })
				{
					ATargetPoint* Found = nullptr;
					for (TActorIterator<ATargetPoint> It(World); It; ++It) if (It->ActorHasTag(Tag)) Found = *It;
					if (!Found) { Test->AddError(FString::Printf(TEXT("missing TargetPoint tagged %s"), Tag)); return true; }
					Points.Add(Found->GetActorLocation());
				}
				BaseGravity = Move->GravityScale;
			}
			if (Index >= Points.Num()) return Finish(Move);
			if (Phase == 0)
			{
				const float Half = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
				Player->SetActorLocationAndRotation(Points[Index] + FVector(0.f, 0.f, Half + 5.f), FRotator::ZeroRotator, false, nullptr, ETeleportType::ResetPhysics);
				Move->StopMovementImmediately();
				PhaseStart = Now; Phase = 1; Distance = 0.f; Samples = 0; RunState = 0;
				return false;
			}
			if (Now - PhaseStart < 2.f) return false;                       // settle (deep water falls slowly)
			if (Phase == 1) { Phase = 2; Last = Player->GetActorLocation(); RunStart = Last; PhaseStart = Now; return false; }
			Player->AddMovementInput(FVector(1.f, 0.f, 0.f), 1.f);   // walk +X
			const FVector L = Player->GetActorLocation();
			if (Now - PhaseStart > 3.f) { Distance += Move->Velocity.Size2D(); ++Samples; }   // average once up to speed
			RunState = FMath::Max(RunState, URPGWaterZoneComponent::GetCharacterWaterState(Player));   // deepest state on the walk
			Last = L;
			if (Now - PhaseStart < 5.f) return false;
			Speed[Index] = Samples ? Distance / Samples : 0.f;   // (headless frame timing makes distance/time unreliable)
			State[Index] = RunState;
			Gravity[Index] = Move->GravityScale;
			Test->AddInfo(FString::Printf(TEXT("point %d: water state %d, speed %.0f cm/s, gravity scale %.2f"), Index, State[Index], Speed[Index], Gravity[Index]));
			Test->AddInfo(FString::Printf(TEXT("  moved %.0f cm, velocity %.0f, max speed %.0f, mode %d, input %s"), FVector::Dist2D(L, RunStart),
				Move->Velocity.Size2D(), Move->GetMaxSpeed(), (int32)Move->MovementMode.GetValue(), *Move->GetLastInputVector().ToString()));
			++Index; Phase = 0;
			return false;
		}
	private:
		bool Finish(UCharacterMovementComponent* Move)
		{
			Test->TestEqual(TEXT("dry ground: no water state"), State[0], 0);
			Test->TestEqual(TEXT("wading point is wading"), State[1], 1);
			Test->TestEqual(TEXT("deep point is head-under"), State[2], 2);
			Test->TestTrue(FString::Printf(TEXT("wading slower than dry (%.0f < %.0f)"), Speed[1], Speed[0]), Speed[0] > 50.f && Speed[1] < Speed[0] * 0.97f);
			Test->TestTrue(FString::Printf(TEXT("deep water much slower (%.0f <= 0.6 x %.0f)"), Speed[2], Speed[0]), Speed[2] <= Speed[0] * 0.6f && Speed[2] > 5.f);
			Test->TestTrue(TEXT("deep water lowers gravity"), Gravity[2] < BaseGravity * 0.5f);
			Test->TestEqual(TEXT("back on dry ground: state cleared"), State[3], 0);
			Test->TestTrue(TEXT("back on dry ground: gravity restored"), FMath::IsNearlyEqual(Gravity[3], BaseGravity, 0.01f));
			return true;
		}
		FAutomationTestBase* Test;
		TArray<FVector> Points;
		float Speed[4] = {}, Gravity[4] = {};
		int32 State[4] = {};
		int32 Index = 0, Phase = 0, Frames = 0, Samples = 0, RunState = 0;
		float PhaseStart = 0.f, Distance = 0.f, BaseGravity = 1.f;
		FVector Last = FVector::ZeroVector, RunStart = FVector::ZeroVector;
		TObjectPtr<ACharacter> Walker = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGWaterBuoyancyTest, "RPG.Water.Buoyancy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRPGWaterBuoyancyTest::RunTest(const FString&)
{
	StartWaterPIE();
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FBuoyancyCheck(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGWaterWadingTest, "RPG.Water.Wading", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRPGWaterWadingTest::RunTest(const FString&)
{
	StartWaterPIE();
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FWadingCheck(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
