// Editor automation test: how steady first person looks. Session Frontend > Automation > RPG.Combat.FirstPersonSteadiness,
// or UnrealEditor-Cmd RPG_CODEX.uproject -ExecCmds="Automation RunTests RPG.Combat.FirstPersonSteadiness;Quit" -unattended -nullrhi
// In first person, in combat: holds still, sweeps the look slowly, then flicks it a few degrees, and measures the camera's
// shake and the fists' jitter on screen (both relative to what the player is looking at).

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "RPG_CODEX.h"
#include "RPGCameraModeComponent.h"
#include "RPGCombatAimComponent.h"
#include "RPGControllerInputComponent.h"
#include "RPGPrototypeAnimationComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
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
	/** Frame-to-frame change in velocity of a tracked point: high-frequency shake (cm/frame^2 RMS, and the worst frame). */
	struct FShake
	{
		TArray<FVector> Points;
		void Add(const FVector& P) { Points.Add(P); }
		void Reset() { Points.Reset(); }
		void Measure(float& OutRms, float& OutMax, int32& OutReversals) const
		{
			OutRms = OutMax = 0.f; OutReversals = 0;
			double Sum = 0.0; int32 Count = 0;
			for (int32 I = 2; I < Points.Num(); ++I)
			{
				const FVector V1 = Points[I - 1] - Points[I - 2], V2 = Points[I] - Points[I - 1];
				const float A = (V2 - V1).Size();
				Sum += A * A; ++Count; OutMax = FMath::Max(OutMax, A);
				if (FVector::DotProduct(V1, V2) < 0.f && V2.Size() > 0.02f) ++OutReversals;
			}
			OutRms = Count ? FMath::Sqrt(Sum / Count) : 0.f;
		}
	};

	class FRPGSteadinessCommand : public IAutomationLatentCommand
	{
	public:
		explicit FRPGSteadinessCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			ACharacter* Player = World ? UGameplayStatics::GetPlayerCharacter(World, 0) : nullptr;
			APlayerController* Controller = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
			if (!Player || !Controller || !Player->FindComponentByClass<URPGControllerInputComponent>())
			{
				if (++WaitFrames > 600) { Test->AddError(TEXT("RPGSTEADY could not start")); return true; }
				return false;
			}
			const float Now = World->GetTimeSeconds();
			if (Phase == 0)
			{
				Player->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				if (URPGCameraModeComponent* CameraMode = Player->FindComponentByClass<URPGCameraModeComponent>(); CameraMode && !CameraMode->bIsFirstPerson)
					CameraMode->TogglePerspective();
				BaseYaw = Controller->GetControlRotation().Yaw;
				// Stay in the combat stance for the whole measurement (it normally relaxes 8 s after the last action).
				if (URPGPrototypeAnimationComponent* Animation = Player->FindComponentByClass<URPGPrototypeAnimationComponent>())
					Animation->CombatStanceTimeout = 120.f;
				// Enter combat with a quick guard tap, then let things settle.
				Press(Player, Controller, true);
				Phase = 1; PhaseStart = Now;
				return false;
			}
			if (Phase == 1)
			{
				if (Now - PhaseStart > 0.15f) Press(Player, Controller, false);
				if (Now - PhaseStart < 1.2f) return false;
				Begin(TEXT("Standing in the combat stance (4 s)"), 4.f, Now);
				return false;
			}
			// Drive the look for the current segment.
			const float T = Now - PhaseStart;
			float Yaw = BaseYaw;
			if (Phase == 3) Yaw = BaseYaw + 30.f * FMath::Sin(T * PI / 3.f);          // slow sweep, +-30 deg over 6 s
			if (Phase == 4) Yaw = BaseYaw + ((int32(T / 0.5f) % 2) ? 4.f : 0.f);     // 4 deg flicks every half second
			if (Phase >= 3) Controller->SetControlRotation(FRotator(0.f, Yaw, 0.f));
			Sample(Player, Controller);
			if (T < Duration) return false;
			Report();
			if (Phase == 2) { Begin(TEXT("Slow look sweep, 30 deg each way (6 s)"), 6.f, Now); Phase = 3; return false; }
			if (Phase == 3) { Begin(TEXT("Small look flicks, 4 deg (3 s)"), 3.f, Now); Phase = 4; return false; }
			return true;
		}

	private:
		void Press(ACharacter* Player, APlayerController* Controller, bool bDown)
		{
			UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Controller->GetLocalPlayer());
			UObject* Owner = Player->FindComponentByClass<URPGControllerInputComponent>();
			for (const TCHAR* Name : {TEXT("IA_RightAttack"), TEXT("IA_Defense")})
				if (UInputAction* Action = Input ? FindObject<UInputAction>(Owner, Name) : nullptr)
				{
					if (bDown) Input->StartContinuousInputInjectionForAction(Action, FInputActionValue(1.f), {}, {});
					else Input->StopContinuousInputInjectionForAction(Action);
				}
		}

		void Begin(const TCHAR* Name, float Seconds, float Now)
		{
			Segment = Name; Duration = Seconds; PhaseStart = Now;
			if (Phase < 2) Phase = 2;
			Camera.Reset(); CameraLocal.Reset(); FistL.Reset(); FistR.Reset();
			MaxFistCameraGap = 0.f;
		}

		void Sample(ACharacter* Player, APlayerController* Controller)
		{
			const UCameraComponent* Cam = Player->FindComponentByClass<UCameraComponent>();
			if (!Cam) return;
			// Camera: in the world, and relative to the body. Fists: in the camera's own frame (what the player sees).
			const FTransform View(Controller->GetControlRotation(), Cam->GetComponentLocation());
			Camera.Add(Cam->GetComponentLocation());
			CameraLocal.Add(Player->GetActorTransform().InverseTransformPosition(Cam->GetComponentLocation()));
			const USkeletalMeshComponent* Mesh = Player->GetMesh();
			FistL.Add(View.InverseTransformPosition(Mesh->GetBoneLocation(TEXT("middle_01_l"))));
			FistR.Add(View.InverseTransformPosition(Mesh->GetBoneLocation(TEXT("middle_01_r"))));
		}

		void Report()
		{
			float CR, CM, LR, LM, RR, RM, BR, BM; int32 CRev, LRev, RRev, BRev;
			Camera.Measure(CR, CM, CRev); CameraLocal.Measure(BR, BM, BRev); FistL.Measure(LR, LM, LRev); FistR.Measure(RR, RM, RRev);
			auto Spread = [](const FShake& S) { FBox Box(ForceInit); for (const FVector& P : S.Points) Box += P; return Box.GetSize(); };
			const FVector SL = Spread(FistL), SR = Spread(FistR), SC = Spread(CameraLocal);
			const FString Line = FString::Printf(
				TEXT("RPGSTEADY %s: camera shake %.3f (worst %.2f) cm/frame2, camera vs body moved %.1f x %.1f x %.1f cm | fists on screen: L shake %.3f (worst %.2f) reversals %d, drift %.1f x %.1f x %.1f cm; R shake %.3f (worst %.2f) reversals %d, drift %.1f x %.1f x %.1f cm"),
				*Segment, CR, CM, SC.X, SC.Y, SC.Z, LR, LM, LRev, SL.X, SL.Y, SL.Z, RR, RM, RRev, SR.X, SR.Y, SR.Z);
			UE_LOG(LogRPGInput, Display, TEXT("%s"), *Line);
			Test->AddInfo(Line);
		}

		FAutomationTestBase* Test;
		int32 Phase = 0, WaitFrames = 0;
		float PhaseStart = 0.f, Duration = 0.f, BaseYaw = 0.f, MaxFistCameraGap = 0.f;
		FString Segment;
		FShake Camera, CameraLocal, FistL, FistR;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGFirstPersonSteadinessTest, "RPG.Combat.FirstPersonSteadiness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRPGFirstPersonSteadinessTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRPGSteadinessCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
