// RPG.Npc.Collision: the player flies into an NPC at 1500 cm/s. Both must go down by momentum (the NPC thrown, the
// player knocked out of the air too: over SelfKnockDownSpeed) and both must get back up. Logs IMPACTTEST lines.
// RPG_SHOT_IMPACT=1 (rendered editor) films it from the side into Saved/Screenshots/Impact/.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "RPG_CODEX.h"
#include "RPGBodyImpactComponent.h"
#include "RPGMovementAbilityComponent.h"
#include "RPGNpcCharacter.h"
#include "RPGPhysicsReactionComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealClient.h"

namespace
{
	class FRPGImpactCommand : public IAutomationLatentCommand
	{
	public:
		explicit FRPGImpactCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World) return ++Frames > 600 ? Fail(TEXT("no play world")) : false;
			ACharacter* Player = UGameplayStatics::GetPlayerCharacter(World, 0);
			const float Now = World->GetTimeSeconds();
			if (!Npc.IsValid())
			{
				for (TActorIterator<ARPGNpcCharacter> It(World); It; ++It) { Npc = *It; break; }
				if (!Npc.IsValid() || !Player) return ++Frames > 600 ? Fail(TEXT("no NPC or player")) : false;
				Start = Now;
				return false;
			}
			ARPGNpcCharacter* N = Npc.Get();
			URPGBodyImpactComponent* PlayerImpact = Player ? Player->FindComponentByClass<URPGBodyImpactComponent>() : nullptr;
			if (!PlayerImpact) return ++Frames > 600 ? Fail(TEXT("player has no body impact component")) : false;

			if (Phase == 0 && Now - Start > 1.f)
			{
				// NPC stands still; the player hovers 6 m away, level with it, facing it, and flies in.
				N->bWander = false;
				N->GetController()->StopMovement();
				const FVector Target = N->GetActorLocation();
				const FVector From = Target - N->GetActorForwardVector() * -600.f;   // in front of it
				Player->SetActorLocation(From + FVector(0.f, 0.f, 5.f), false, nullptr, ETeleportType::TeleportPhysics);
				if (URPGMovementAbilityComponent* Abilities = Player->FindComponentByClass<URPGMovementAbilityComponent>()) Abilities->StartFlight();
				Dir = (Target - From).GetSafeNormal2D();
				Phase = 1; PhaseStart = Now;
				return false;
			}
			if (Phase == 1)
			{
				Film(World, N, Now);
				if (!PlayerImpact->IsDown() && !N->BodyImpact->IsDown()) Player->GetCharacterMovement()->Velocity = Dir * 1500.f;
				if (N->BodyImpact->IsDown()) bNpcDown = true;
				if (PlayerImpact->IsDown()) bPlayerDown = true;
				if (bNpcDown && !N->BodyImpact->IsDown() && !N->IsBusy()) bNpcUp = true;
				if (bPlayerDown && !PlayerImpact->IsDown()) bPlayerUp = true;
				const bool bDone = bNpcDown && bNpcUp && bPlayerDown && bPlayerUp;
				if (!bDone && Now - PhaseStart < 14.f) return false;
				const bool bPass = bDone;
				const FString Line = FString::Printf(TEXT("IMPACTTEST %s  NPC knocked down %d, got up %d; player knocked down %d, got up %d; NPC moved %.0f cm"),
					bPass ? TEXT("PASS") : TEXT("FAIL"), bNpcDown, bNpcUp, bPlayerDown, bPlayerUp, float(FVector::Dist2D(N->GetActorLocation(), N->GetHome())));
				UE_LOG(LogRPGInput, Display, TEXT("%s"), *Line);
				if (bPass) Test->AddInfo(Line); else Test->AddError(Line);
				return true;
			}
			return false;
		}

	private:
		void Film(UWorld* World, ARPGNpcCharacter* N, float Now)
		{
			if (FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_SHOT_IMPACT")) != TEXT("1") || Now - PhaseStart > 5.f || Now - LastShot < 0.15f) return;
			if (!Camera)
			{
				Camera = World->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator);
				Camera->GetCameraComponent()->SetFieldOfView(60.f);
				Camera->GetCameraComponent()->bConstrainAspectRatio = false;
				if (APlayerController* PC = World->GetFirstPlayerController()) PC->SetViewTarget(Camera);
				Look = N->GetHome();
			}
			const FVector Side = FVector::CrossProduct(Dir, FVector::UpVector);
			const FVector Eye = Look + Side * 900.f + Dir * 150.f + FVector(0.f, 0.f, 120.f);
			Camera->SetActorLocationAndRotation(Eye, (Look + Dir * 200.f - Eye).Rotation());
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / FString::Printf(TEXT("Screenshots/Impact/impact_%02d_%.2fs.png"), Shot++, Now - PhaseStart), false, false);
			LastShot = Now;
		}
		bool Fail(const TCHAR* Why) { Test->AddError(FString::Printf(TEXT("IMPACTTEST could not run: %s"), Why)); return true; }

		FAutomationTestBase* Test;
		TWeakObjectPtr<ARPGNpcCharacter> Npc;
		ACameraActor* Camera = nullptr;
		FVector Dir = FVector::ForwardVector, Look = FVector::ZeroVector;
		int32 Frames = 0, Phase = 0, Shot = 0;
		float Start = 0.f, PhaseStart = 0.f, LastShot = -1.f;
		bool bNpcDown = false, bNpcUp = false, bPlayerDown = false, bPlayerUp = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBodyImpactTest, "RPG.Npc.Collision", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRPGBodyImpactTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FRPGImpactCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
