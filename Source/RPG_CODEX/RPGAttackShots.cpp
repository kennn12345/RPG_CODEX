// Visual check (needs rendering, so it is NOT under "RPG"; RunTests matches substrings): real screenshots of an attack on
// the character mesh from three fixed cameras (side, front, three-quarter), at set moments of the attack.
//   UnrealEditor.exe RPG_CODEX.uproject -ExecCmds="Automation RunTests Visual.AttackShots;Quit" -unattended
// Environment: RPG_SHOT_ATTACK = IA_RightAttack (default), IA_Defense (left punch) or IA_Kick; RPG_SHOT_DIR = output folder
// (default Saved/Screenshots/Attack/<action>). The game runs at 1/10 speed while filming so each shot lands on its moment.
// File names: <view>_<nn>_<t>s.png, t = game seconds after the button press.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "RPG_CODEX.h"
#include "RPGCameraModeComponent.h"
#include "RPGCombatTestDummy.h"
#include "RPGControllerInputComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformMisc.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealClient.h"

namespace
{
	/** Moments to film, in game seconds after the press (the tap punches land at ~0.21-0.25 s, the kicks at ~0.33-0.40 s). */
	const float ShotTimes[] = { -0.05f, 0.05f, 0.10f, 0.15f, 0.19f, 0.23f, 0.27f, 0.32f, 0.40f, 0.50f, 0.62f };

	struct FView { const TCHAR* Name; FVector Offset; };   // camera offset from the player: X forward, Y right, Z up (cm)
	const FView Views[] = {
		{ TEXT("side"),  FVector(30.f, 330.f, 20.f) },    // from the character's right
		{ TEXT("front"), FVector(330.f, 10.f, 20.f) },    // facing the character
		{ TEXT("threequarter"), FVector(240.f, 240.f, 60.f) },
	};

	class FRPGAttackShotsCommand : public IAutomationLatentCommand
	{
	public:
		explicit FRPGAttackShotsCommand(FAutomationTestBase* InTest) : Test(InTest)
		{
			Action = FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_SHOT_ATTACK"));
			if (Action.IsEmpty()) Action = TEXT("IA_RightAttack");
			Dir = FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_SHOT_DIR"));
			if (Dir.IsEmpty()) Dir = FPaths::ProjectSavedDir() / TEXT("Screenshots/Attack") / Action;
		}

		virtual bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World) return Wait(TEXT("no play world"));
			ACharacter* Player = UGameplayStatics::GetPlayerCharacter(World, 0);
			APlayerController* Controller = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
			UEnhancedInputLocalPlayerSubsystem* Input = Controller ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Controller->GetLocalPlayer()) : nullptr;
			UObject* Owner = Player ? Player->FindComponentByClass<URPGControllerInputComponent>() : nullptr;
			UInputAction* Button = Owner ? FindObject<UInputAction>(Owner, *Action) : nullptr;
			if (!Player || !Controller || !Input || !Button) return Wait(TEXT("no player / input"));
			const float Now = World->GetTimeSeconds();

			if (!bSetUp)
			{
				// Third person; the dummy put out of the way (the attacks are filmed in the air, nothing to hit).
				if (URPGCameraModeComponent* Cam = Player->FindComponentByClass<URPGCameraModeComponent>(); Cam && Cam->bIsFirstPerson) Cam->TogglePerspective();
				for (TActorIterator<ARPGCombatTestDummy> It(World); It; ++It) { It->StopWanderingAtHome(); It->SetActorHiddenInGame(true); It->SetActorEnableCollision(false); }
				Controller->SetControlRotation(Player->GetActorRotation());
				CameraActor = World->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator);
				CameraActor->GetCameraComponent()->SetFieldOfView(45.f);
				CameraActor->GetCameraComponent()->bConstrainAspectRatio = false;
				bSetUp = true;
				ViewStart = Now + 0.5f;
				return false;
			}

			if (View >= UE_ARRAY_COUNT(Views))
			{
				World->GetWorldSettings()->SetTimeDilation(1.f);
				Test->AddInfo(TEXT("ATTACKSHOT done: ") + Dir);
				UE_LOG(LogRPGInput, Display, TEXT("ATTACKSHOT done: %s"), *Dir);
				return true;
			}

			if (!bViewPlaced)
			{
				// Camera fixed relative to where the character stands now, looking at its chest.
				const FTransform Body = Player->GetActorTransform();
				const FVector Chest = Body.GetLocation() + FVector(0.f, 0.f, 45.f);
				const FVector Eye = Body.TransformPosition(Views[View].Offset) + FVector(0.f, 0.f, 45.f);
				CameraActor->SetActorLocationAndRotation(Eye, (Chest + Body.GetUnitAxis(EAxis::X) * 30.f - Eye).Rotation());
				Controller->SetViewTarget(CameraActor);
				World->GetWorldSettings()->SetTimeDilation(0.1f);
				bViewPlaced = true;
				ViewStart = Now + 0.15f;   // game seconds at 1/10 speed: settle, then press
				NextShot = 0;
				return false;
			}

			const float T = Now - ViewStart;   // game seconds after the press
			if (!bPressed && T >= 0.f)
			{
				Input->StartContinuousInputInjectionForAction(Button, FInputActionValue(1.f), {}, {});
				bPressed = true;
			}
			if (bPressed && !bReleased && T >= 0.08f)   // a tap: released well before the 0.25 s charge threshold
			{
				Input->StopContinuousInputInjectionForAction(Button);
				bReleased = true;
			}
			while (NextShot < UE_ARRAY_COUNT(ShotTimes) && T >= ShotTimes[NextShot])
			{
				const FString File = Dir / FString::Printf(TEXT("%s_%02d_%+.2fs.png"), Views[View].Name, NextShot, ShotTimes[NextShot]);
				FScreenshotRequest::RequestScreenshot(File, false, false);
				++NextShot;
			}
			if (NextShot >= UE_ARRAY_COUNT(ShotTimes) && T >= ShotTimes[UE_ARRAY_COUNT(ShotTimes) - 1] + 0.6f)
			{
				// Next view: back to normal speed for a moment so the stance settles, same spot.
				World->GetWorldSettings()->SetTimeDilation(1.f);
				++View;
				bViewPlaced = bPressed = bReleased = false;
			}
			return false;
		}

	private:
		bool Wait(const TCHAR* Why)
		{
			if (++WaitFrames < 900) return false;
			Test->AddError(FString::Printf(TEXT("ATTACKSHOT could not start: %s"), Why));
			return true;
		}

		FAutomationTestBase* Test;
		FString Action, Dir;
		TObjectPtr<ACameraActor> CameraActor = nullptr;
		bool bSetUp = false, bViewPlaced = false, bPressed = false, bReleased = false;
		int32 View = 0, NextShot = 0, WaitFrames = 0;
		float ViewStart = 0.f;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAttackShots, "Visual.AttackShots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRPGAttackShots::RunTest(const FString& Parameters)
{
	// PIE in its own 1280x720 window, so the shots are 16:9 whatever the editor viewport is
	ULevelEditorPlaySettings* Play = NewObject<ULevelEditorPlaySettings>();
	Play->NewWindowWidth = 1280; Play->NewWindowHeight = 720; Play->NewWindowPosition = FIntPoint(0, 0); Play->CenterNewWindow = false;
	Play->LastExecutedPlayModeType = PlayMode_InEditorFloating;
	FRequestPlaySessionParams Session;
	Session.WorldType = EPlaySessionWorldType::PlayInEditor;
	Session.EditorPlaySettings = Play;
	GEditor->RequestPlaySession(Session);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRPGAttackShotsCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
