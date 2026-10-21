// Visual check (needs rendering, so it is NOT under "RPG"): plays animation clips on the combat test dummy and films each
// one from three cameras (side, front, three-quarter) at evenly spaced moments - to look at a clip on the real
// character before deciding to use it.
//   set RPG_SHOT_CLIPS=/Game/Path/ClipA,/Game/Path/ClipB
//   UnrealEditor.exe RPG_CODEX.uproject -ExecCmds="Automation RunTests Visual.ClipShots" -TestExit="Automation Test Queue Empty" -unattended
// PNGs go to Saved/Screenshots/Clips/<clip>/<view>_<nn>_<t>s.png (t = seconds into the clip).

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "RPG_CODEX.h"
#include "RPGCombatTestDummy.h"
#include "RPGPhysicsReactionComponent.h"
#include "Animation/AnimSequenceBase.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealClient.h"

namespace
{
	constexpr int32 ShotsPerClip = 8;

	struct FClipView { const TCHAR* Name; FVector Offset; };   // camera offset from the dummy: X forward, Y right, Z up (cm)
	const FClipView ClipViews[] = {
		{ TEXT("side"),  FVector(30.f, 380.f, 20.f) },
		{ TEXT("front"), FVector(380.f, 10.f, 20.f) },
		{ TEXT("threequarter"), FVector(270.f, 270.f, 70.f) },
	};

	class FRPGClipShotsCommand : public IAutomationLatentCommand
	{
	public:
		explicit FRPGClipShotsCommand(FAutomationTestBase* InTest) : Test(InTest)
		{
			FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_SHOT_CLIPS")).ParseIntoArray(ClipPaths, TEXT(","));
		}

		virtual bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World) return Wait(TEXT("no play world"));
			APlayerController* Controller = UGameplayStatics::GetPlayerController(World, 0);
			if (!Dummy.IsValid())
				for (TActorIterator<ARPGCombatTestDummy> It(World); It; ++It) Dummy = *It;
			if (!Controller || !Dummy.IsValid()) return Wait(TEXT("no player controller / dummy"));
			if (ClipPaths.IsEmpty()) { Test->AddError(TEXT("CLIPSHOT: set RPG_SHOT_CLIPS")); return true; }
			const float Now = World->GetTimeSeconds();

			if (!CameraActor)
			{
				// The dummy stands still at its spot, no reactions; the player is moved out of the way and hidden.
				Dummy->StopWanderingAtHome();
				Dummy->PhysicsReaction->bReactionsEnabled = false;
				if (APawn* Player = Controller->GetPawn())
				{
					Player->SetActorLocation(Dummy->GetActorLocation() + FVector(0.f, -2000.f, 0.f), false, nullptr, ETeleportType::ResetPhysics);
					Player->SetActorHiddenInGame(true);
				}
				CameraActor = World->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator);
				CameraActor->GetCameraComponent()->SetFieldOfView(45.f);
				CameraActor->GetCameraComponent()->bConstrainAspectRatio = false;
				Controller->SetViewTarget(CameraActor);
				Phase = 0;
				return false;
			}

			if (ClipIndex >= ClipPaths.Num())
			{
				World->GetWorldSettings()->SetTimeDilation(1.f);
				UE_LOG(LogRPGInput, Display, TEXT("CLIPSHOT done"));
				return true;
			}

			UAnimSequenceBase* Clip = LoadObject<UAnimSequenceBase>(nullptr, *ClipPaths[ClipIndex]);
			if (!Clip)
			{
				Test->AddError(FString::Printf(TEXT("CLIPSHOT: could not load %s"), *ClipPaths[ClipIndex]));
				++ClipIndex;
				return false;
			}
			const float Length = Clip->GetPlayLength();

			if (Phase == 0)
			{
				// Place the camera for this view, restart the clip from its first frame, slow time so shots land on time.
				const FTransform Body = Dummy->GetActorTransform();
				// RPG_SHOT_RAISE (cm): look higher, for clips whose body rises above the ground (hovering, jumps)
				const float Raise = 20.f + FCString::Atof(*FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_SHOT_RAISE")));
				const FVector Chest = Body.GetLocation() + FVector(0.f, 0.f, Raise);
				const FVector Eye = Body.TransformPosition(ClipViews[View].Offset) + FVector(0.f, 0.f, Raise);
				CameraActor->SetActorLocationAndRotation(Eye, (Chest - Eye).Rotation());
				Dummy->Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
				Dummy->Mesh->PlayAnimation(Clip, true);
				World->GetWorldSettings()->SetTimeDilation(0.25f);
				ClipStart = Now;
				NextShot = 0;
				Phase = 1;
				return false;
			}

			const float T = Now - ClipStart;
			while (NextShot < ShotsPerClip && T >= Length * NextShot / ShotsPerClip)
			{
				const FString Dir = FPaths::ProjectSavedDir() / TEXT("Screenshots/Clips") / Clip->GetName();
				FScreenshotRequest::RequestScreenshot(Dir / FString::Printf(TEXT("%s_%02d_%.2fs.png"), ClipViews[View].Name, NextShot, T), false, false);
				++NextShot;
			}
			if (NextShot >= ShotsPerClip && T >= Length + 0.05f)
			{
				Phase = 0;
				if (++View >= UE_ARRAY_COUNT(ClipViews)) { View = 0; ++ClipIndex; }
			}
			return false;
		}

	private:
		bool Wait(const TCHAR* Why)
		{
			if (++WaitFrames < 900) return false;
			Test->AddError(FString::Printf(TEXT("CLIPSHOT could not start: %s"), Why));
			return true;
		}

		FAutomationTestBase* Test;
		TArray<FString> ClipPaths;
		TWeakObjectPtr<ARPGCombatTestDummy> Dummy;
		TObjectPtr<ACameraActor> CameraActor = nullptr;
		int32 ClipIndex = 0, View = 0, NextShot = 0, Phase = 0, WaitFrames = 0;
		float ClipStart = 0.f;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGClipShots, "Visual.ClipShots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRPGClipShots::RunTest(const FString& Parameters)
{
	ULevelEditorPlaySettings* Play = NewObject<ULevelEditorPlaySettings>();
	Play->NewWindowWidth = 1280; Play->NewWindowHeight = 720; Play->NewWindowPosition = FIntPoint(0, 0); Play->CenterNewWindow = false;
	Play->LastExecutedPlayModeType = PlayMode_InEditorFloating;
	FRequestPlaySessionParams Session;
	Session.WorldType = EPlaySessionWorldType::PlayInEditor;
	Session.EditorPlaySettings = Play;
	GEditor->RequestPlaySession(Session);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRPGClipShotsCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
