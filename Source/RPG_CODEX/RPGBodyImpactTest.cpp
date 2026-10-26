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
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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
				Trace(Player, N, Now);
				if (!PlayerImpact->IsDown() && !N->BodyImpact->IsDown()) Player->GetCharacterMovement()->Velocity = Dir * 1500.f;
				if (N->BodyImpact->IsDown()) bNpcDown = true;
				if (PlayerImpact->IsDown() && !bPlayerDown) { bPlayerDown = true; PlayerDownAt = Player->GetActorLocation(); }
				if (bPlayerDown && !bPlayerUp && !PlayerImpact->IsDown()) PlayerUpAt = Player->GetActorLocation();
				if (bNpcDown && !N->BodyImpact->IsDown() && !N->IsBusy()) bNpcUp = true;
				if (bPlayerDown && !PlayerImpact->IsDown()) bPlayerUp = true;
				const bool bDone = bNpcDown && bNpcUp && bPlayerDown && bPlayerUp;
				if (!bDone && Now - PhaseStart < 14.f) return false;
				const float PlayerTravel = float(FVector::Dist(PlayerDownAt, PlayerUpAt));
				const bool bPass = bDone && PlayerTravel < 1000.f;   // a 15 m/s crash throws you meters, not out of sight
				const FString Line = FString::Printf(TEXT("IMPACTTEST %s  NPC knocked down %d, got up %d; player knocked down %d, got up %d %.0f cm from the crash; NPC moved %.0f cm"),
					bPass ? TEXT("PASS") : TEXT("FAIL"), bNpcDown, bNpcUp, bPlayerDown, bPlayerUp, PlayerTravel, float(FVector::Dist2D(N->GetActorLocation(), N->GetHome())));
				UE_LOG(LogRPGInput, Display, TEXT("%s"), *Line);
				if (bPass) Test->AddInfo(Line); else Test->AddError(Line);
				if (!TraceRows.IsEmpty()) FFileHelper::SaveStringToFile(TraceRows, *(FPaths::ProjectSavedDir() / TEXT("GetUpTrace.csv")));
				return true;
			}
			return false;
		}

	private:
		void Film(UWorld* World, ARPGNpcCharacter* N, float Now)
		{
			if (FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_SHOT_IMPACT")) != TEXT("1") || Now - PhaseStart > 12.f || Now - LastShot < 0.2f) return;   // the crash and both get-ups
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
		/** RPG_GETUP_TRACE=1: every frame, where the player's and the NPC's main bones are (world, cm), into
		 *  Saved/GetUpTrace.csv, for Tools/ProtoAnim/tracesmooth.py to find hitches in what the game really plays. */
		void Trace(ACharacter* Player, ARPGNpcCharacter* N, float Now)
		{
			if (FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_GETUP_TRACE")) != TEXT("1")) return;
			if (TraceRows.IsEmpty()) TraceRows = TEXT("who,t,bone,x,y,z,state\n");
			static const TCHAR* Bones[] = { TEXT("pelvis"), TEXT("spine_05"), TEXT("head"), TEXT("hand_l"), TEXT("hand_r"),
				TEXT("lowerarm_l"), TEXT("lowerarm_r"), TEXT("foot_l"), TEXT("foot_r"), TEXT("calf_l"), TEXT("calf_r") };
			for (ACharacter* C : { Player, static_cast<ACharacter*>(N) })
			{
				URPGBodyImpactComponent* Impact = C ? C->FindComponentByClass<URPGBodyImpactComponent>() : nullptr;
				if (!Impact) continue;
				// bones refreshed even when off screen (otherwise frames the camera doesn't see read the last frame's pose)
				USkeletalMeshComponent* Mesh = C->GetMesh();
				Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				if (!LoggedUpdateRate.Contains(C))
				{
					LoggedUpdateRate.Add(C);
					const FAnimUpdateRateParameters* Rate = Mesh->AnimUpdateRateParams;
					UE_LOG(LogRPGInput, Display, TEXT("GETUPTRACE %s: URO %d, update rate %d, eval rate %d, interpolate %d, tick interval %.3f, anim tick interval %.3f"),
						*C->GetActorNameOrLabel(), Mesh->bEnableUpdateRateOptimizations, Rate ? Rate->UpdateRate : -1, Rate ? Rate->EvaluationRate : -1,
						Rate ? int32(Rate->ShouldInterpolateSkippedFrames()) : -1, Mesh->PrimaryComponentTick.TickInterval, C->PrimaryActorTick.TickInterval);
				}
				const TCHAR* Who = C == Player ? TEXT("player") : TEXT("npc");
				const TCHAR* State = Impact->IsDown() ? TEXT("down") : TEXT("up");
				for (const TCHAR* Bone : Bones)
				{
					const FVector P = C->GetMesh()->GetBoneLocation(Bone);
					TraceRows += FString::Printf(TEXT("%s,%.4f,%s,%.2f,%.2f,%.2f,%s\n"), Who, Now, Bone, P.X, P.Y, P.Z, State);
				}
				// and what carries the body: the capsule, the mesh component and the root bone
				const FVector Carriers[] = { C->GetActorLocation(), Mesh->GetComponentLocation(), Mesh->GetBoneLocation(TEXT("root")) };
				const TCHAR* CarrierNames[] = { TEXT("@actor"), TEXT("@mesh"), TEXT("@root") };
				for (int32 I = 0; I < 3; ++I)
					TraceRows += FString::Printf(TEXT("%s,%.4f,%s,%.2f,%.2f,%.2f,%s\n"), Who, Now, CarrierNames[I], Carriers[I].X, Carriers[I].Y, Carriers[I].Z, State);
			}
		}
		FString TraceRows;
		TSet<ACharacter*> LoggedUpdateRate;

		bool Fail(const TCHAR* Why) { Test->AddError(FString::Printf(TEXT("IMPACTTEST could not run: %s"), Why)); return true; }

		FAutomationTestBase* Test;
		TWeakObjectPtr<ARPGNpcCharacter> Npc;
		ACameraActor* Camera = nullptr;
		FVector Dir = FVector::ForwardVector, Look = FVector::ZeroVector, PlayerDownAt = FVector::ZeroVector, PlayerUpAt = FVector::ZeroVector;
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
