// Visual check (needs rendering, so it is NOT under "RPG"): opens L_WaterTest, plays it, and photographs the water from
// every CameraActor tagged "WaterShot" (after letting the water animate), to compare the stylized water with its targets.
// Visual.WorldShots does the same for L_WorldVista (tag "WorldShot", Saved/Screenshots/World/), and Visual.SkyShots
// photographs the "SkyShot" cameras there at morning / noon / sunset / night (ARPGSkyCycle) -> Saved/Screenshots/Sky/.
//   UnrealEditor.exe RPG_CODEX.uproject -ExecCmds="Automation RunTests Visual.WaterShots" -TestExit="Automation Test Queue Empty" -unattended
// PNGs go to Saved/Screenshots/Water/<camera label>.png (sky shots: <camera label>_<hour>.png).

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
#include "RPGSkyCycle.h"
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
		FRPGWaterShotsCommand(FAutomationTestBase* InTest, const TCHAR* InTag, const TCHAR* InFolder, TArray<float> InHours)
			: Test(InTest), Tag(InTag), Folder(InFolder), Hours(MoveTemp(InHours)) {}

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
					if (It->ActorHasTag(Tag)) Cameras.Add(*It);
				if (Cameras.IsEmpty()) return Fail(*FString::Printf(TEXT("no CameraActor tagged %s"), *Tag.ToString()));
				Cameras.Sort([](const TWeakObjectPtr<ACameraActor>& A, const TWeakObjectPtr<ACameraActor>& B) { return A->GetActorNameOrLabel() < B->GetActorNameOrLabel(); });
				if (APawn* Player = Controller->GetPawn())
				{
					Player->SetActorHiddenInGame(true);
					Player->SetActorEnableCollision(false);
				}
				// Hold the time of day still so every shot of a set matches.
				for (TActorIterator<ARPGSkyCycle> It(World); It; ++It)
				{
					Sky = *It;
					It->bAnimate = false;
				}
				if (!Hours.IsEmpty() && !Sky.IsValid()) return Fail(TEXT("no ARPGSkyCycle for the time-of-day shots"));
			}
			const int32 Sets = FMath::Max(Hours.Num(), 1);
			if (Index >= Cameras.Num() * Sets) return true;
			const int32 Cam = Index % Cameras.Num(), Hour = Index / Cameras.Num();
			const float Now = World->GetTimeSeconds();
			if (!bAimed)
			{
				float Settle = 1.5f;                     // let the water animate and exposure settle
				if (!Hours.IsEmpty() && Cam == 0)
				{
					Sky->SetTimeOfDay(Hours[Hour]);
					Settle = 4.f;                        // sky light recapture + eye adaptation after a time jump
				}
				Controller->SetViewTarget(Cameras[Cam].Get());
				ShotAt = Now + Settle;
				bAimed = true;
				return false;
			}
			if (Now < ShotAt) return false;
			FString Name = Cameras[Cam]->GetActorNameOrLabel();
			if (!Hours.IsEmpty()) Name += FString::Printf(TEXT("_%05.2f"), Hours[Hour]);
			const FString Dir = FPaths::ProjectSavedDir() / TEXT("Screenshots") / Folder;
			FScreenshotRequest::RequestScreenshot(Dir / (Name + TEXT(".png")), false, false);
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
		FName Tag;
		FString Folder;
		TArray<float> Hours;
		TArray<TWeakObjectPtr<ACameraActor>> Cameras;
		TWeakObjectPtr<ARPGSkyCycle> Sky;
		int32 Index = 0, WaitFrames = 0;
		bool bAimed = false;
		float ShotAt = 0.f;
	};
}

static void RunShots(FAutomationTestBase* Test, const TCHAR* Map, const TCHAR* Tag, const TCHAR* Folder, TArray<float> Hours = {})
{
	FAutomationEditorCommonUtils::LoadMap(Map);
	ULevelEditorPlaySettings* Play = NewObject<ULevelEditorPlaySettings>();
	Play->NewWindowWidth = 1280; Play->NewWindowHeight = 720; Play->NewWindowPosition = FIntPoint(0, 0); Play->CenterNewWindow = false;
	Play->LastExecutedPlayModeType = PlayMode_InEditorFloating;
	FRequestPlaySessionParams Session;
	Session.WorldType = EPlaySessionWorldType::PlayInEditor;
	Session.EditorPlaySettings = Play;
	GEditor->RequestPlaySession(Session);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRPGWaterShotsCommand(Test, Tag, Folder, MoveTemp(Hours)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGWaterShots, "Visual.WaterShots", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRPGWaterShots::RunTest(const FString&) { RunShots(this, TEXT("/Game/RPG/Maps/L_WaterTest"), TEXT("WaterShot"), TEXT("Water")); return true; }

// Same, for the stylized-world vista: cameras tagged "WorldShot" in L_WorldVista -> Saved/Screenshots/World/.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGWorldShots, "Visual.WorldShots", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRPGWorldShots::RunTest(const FString&) { RunShots(this, TEXT("/Game/RPG/Maps/L_WorldVista"), TEXT("WorldShot"), TEXT("World")); return true; }

// The day/night cycle: "SkyShot" cameras at morning, noon, sunset, dusk and midnight -> Saved/Screenshots/Sky/.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSkyShots, "Visual.SkyShots", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRPGSkyShots::RunTest(const FString&) { RunShots(this, TEXT("/Game/RPG/Maps/L_WorldVista"), TEXT("SkyShot"), TEXT("Sky"), {6.3f, 12.f, 17.6f, 19.f, 0.f}); return true; }

#endif
