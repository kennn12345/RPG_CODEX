// Visual check (needs rendering, so it is NOT under "RPG"): opens L_WaterTest, plays it, and photographs the water from
// every CameraActor tagged "WaterShot" (after letting the water animate), to compare the stylized water with its targets.
//   UnrealEditor.exe RPG_CODEX.uproject -ExecCmds="Automation RunTests Visual.WaterShots" -TestExit="Automation Test Queue Empty" -unattended
// PNGs go to Saved/Screenshots/Water/<camera label>.png.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Camera/CameraActor.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "ShaderCompiler.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealClient.h"

namespace
{
	class FRPGWaterShotsCommand : public IAutomationLatentCommand
	{
	public:
		explicit FRPGWaterShotsCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			APlayerController* Controller = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
			if (!Controller) return ++WaitFrames > 900 ? Fail(TEXT("no play world / controller")) : false;
			// New water/terrain shaders compile on first view; shooting before that photographs placeholder materials.
			if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return false;
			if (Cameras.IsEmpty())
			{
				for (TActorIterator<ACameraActor> It(World); It; ++It)
					if (It->ActorHasTag(TEXT("WaterShot"))) Cameras.Add(*It);
				if (Cameras.IsEmpty()) return Fail(TEXT("no CameraActor tagged WaterShot"));
				Cameras.Sort([](const TWeakObjectPtr<ACameraActor>& A, const TWeakObjectPtr<ACameraActor>& B) { return A->GetActorNameOrLabel() < B->GetActorNameOrLabel(); });
				if (APawn* Player = Controller->GetPawn())
				{
					Player->SetActorHiddenInGame(true);
					Player->SetActorEnableCollision(false);
				}
			}
			if (Index >= Cameras.Num()) return true;
			const float Now = World->GetTimeSeconds();
			if (!bAimed)
			{
				Controller->SetViewTarget(Cameras[Index].Get());
				ShotAt = Now + 1.5f;                     // let the water animate and exposure settle
				bAimed = true;
				return false;
			}
			if (Now < ShotAt) return false;
			const FString Dir = FPaths::ProjectSavedDir() / TEXT("Screenshots/Water");
			FScreenshotRequest::RequestScreenshot(Dir / (Cameras[Index]->GetActorNameOrLabel() + TEXT(".png")), false, false);
			++Index;
			bAimed = false;
			return false;
		}

	private:
		bool Fail(const TCHAR* Why)
		{
			Test->AddError(FString::Printf(TEXT("WATERSHOT: %s"), Why));
			return true;
		}

		FAutomationTestBase* Test;
		TArray<TWeakObjectPtr<ACameraActor>> Cameras;
		int32 Index = 0, WaitFrames = 0;
		bool bAimed = false;
		float ShotAt = 0.f;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGWaterShots, "Visual.WaterShots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRPGWaterShots::RunTest(const FString& Parameters)
{
	FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/RPG/Maps/L_WaterTest"));
	ULevelEditorPlaySettings* Play = NewObject<ULevelEditorPlaySettings>();
	Play->NewWindowWidth = 1280; Play->NewWindowHeight = 720; Play->NewWindowPosition = FIntPoint(0, 0); Play->CenterNewWindow = false;
	Play->LastExecutedPlayModeType = PlayMode_InEditorFloating;
	FRequestPlaySessionParams Session;
	Session.WorldType = EPlaySessionWorldType::PlayInEditor;
	Session.EditorPlaySettings = Play;
	GEditor->RequestPlaySession(Session);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRPGWaterShotsCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
