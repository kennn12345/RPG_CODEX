// RPG.Npc.Collision / RPG.Player.FlightCrashFirstPerson: the player flies into an NPC at 1500 cm/s. Both must go down by momentum (the NPC thrown, the
// player knocked out of the air too: over SelfKnockDownSpeed) and both must get back up. Logs IMPACTTEST lines.
// RPG_SHOT_IMPACT=1 (rendered editor) films it from the side into Saved/Screenshots/Impact/.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "RPG_CODEX.h"
#include "RPGBodyImpactComponent.h"
#include "RPGMovementAbilityComponent.h"
#include "RPGNpcCharacter.h"
#include "RPGPhysicsReactionComponent.h"
#include "RPGAnimInstance.h"
#include "RPGCameraModeComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Engine/LocalPlayer.h"
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
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Components/CapsuleComponent.h"
#include "MessageLogModule.h"
#include "IMessageLogListing.h"
#include "Modules/ModuleManager.h"
#include "PhysicsEngine/BodyInstance.h"

namespace
{
	// Keep the listener alive through EndPIE: Message Log can flush its physics warnings at teardown.
	class FImpactLogCapture : public FOutputDevice
	{
	public:
		FImpactLogCapture() { GLog->AddOutputDevice(this); }
		~FImpactLogCapture() { GLog->RemoveOutputDevice(this); }
		void Serialize(const TCHAR* Message, ELogVerbosity::Type, const FName&) override
		{
			if (FCString::Strstr(Message, TEXT("Attempting to move a fully simulated skeletal mesh"))) ++PhysicsMoveWarnings;
		}
		TAtomic<int32> PhysicsMoveWarnings{0};
	};

	class FCheckImpactLog : public IAutomationLatentCommand
	{
	public:
		FCheckImpactLog(FAutomationTestBase* InTest, TSharedRef<FImpactLogCapture> InLog) : Test(InTest), Log(InLog) {}
		bool Update() override
		{
			Test->TestEqual(TEXT("No simulated-mesh movement warnings during collision, recovery or PIE teardown"), Log->PhysicsMoveWarnings.Load(), 0);
			const FString Messages = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog").GetLogListing("PIE")->GetAllMessagesAsString();
			Test->TestFalse(TEXT("PIE Message Log contains no simulated-mesh movement warnings"), Messages.Contains(TEXT("Attempting to move a fully simulated skeletal mesh")));
			return true;
		}
	private:
		FAutomationTestBase* Test;
		TSharedRef<FImpactLogCapture> Log;
	};

	class FRPGImpactCommand : public IAutomationLatentCommand
	{
	public:
		explicit FRPGImpactCommand(FAutomationTestBase* InTest, bool bInFirstPerson = false) : Test(InTest), bFirstPerson(bInFirstPerson) {}

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
				// NPC stands still; the player starts 24 m away, level with it, facing it, and flies in.
				N->bWander = false;
				N->GetController()->StopMovement();
				NpcMeshParent = N->GetMesh()->GetAttachParent();
				NpcMeshRelative = N->GetMesh()->GetRelativeTransform();
				const FVector Target = N->GetActorLocation();
				const FVector From = Target - N->GetActorForwardVector() * -2400.f;   // enough flight to exercise lean and its exit
				Player->SetActorLocation(From + FVector(0.f, 0.f, 5.f), false, nullptr, ETeleportType::TeleportPhysics);
				MeshParent = Player->GetMesh()->GetAttachParent();
				MeshSocket = Player->GetMesh()->GetAttachSocketName();
				MeshRelative = Player->GetMesh()->GetRelativeTransform();
				if (URPGMovementAbilityComponent* Abilities = Player->FindComponentByClass<URPGMovementAbilityComponent>()) Abilities->StartFlight();
				Dir = (Target - From).GetSafeNormal2D();
				APlayerController* PC = Cast<APlayerController>(Player->GetController());
				if (!PC || !PC->GetLocalPlayer()) return Fail(TEXT("no local player input"));
				PC->SetControlRotation(Dir.Rotation());
				Input = PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
				MoveAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/IA_Move.IA_Move"));
				if (!Input.IsValid() || !MoveAction.IsValid()) return Fail(TEXT("forward input unavailable"));
				if (bFirstPerson)
					if (URPGCameraModeComponent* CameraMode = Player->FindComponentByClass<URPGCameraModeComponent>(); CameraMode && !CameraMode->bIsFirstPerson) CameraMode->TogglePerspective();
				Phase = 1; PhaseStart = Now;
				return false;
			}
			if (Phase == 1)
			{
				Film(World, N, Now);
				Trace(Player, N, Now);
				Input->InjectInputForAction(MoveAction.Get(), FInputActionValue(FVector2D(0.f, 1.f)), {}, {});
				if (const URPGAnimInstance* Anim = Cast<URPGAnimInstance>(Player->GetMesh()->GetAnimInstance())) bSawFlightPose |= Anim->FlightWeight > .5f;
				if (!PlayerImpact->IsDown() && !N->BodyImpact->IsDown()) Player->GetCharacterMovement()->Velocity = Dir * 1500.f;
				if (N->BodyImpact->IsDown()) bNpcDown = true;
				if (PlayerImpact->IsDown() && !bPlayerDown)
				{
					bPlayerDown = true; PlayerDownAt = Player->GetActorLocation();
					USkeletalMeshComponent* Mesh = Player->GetMesh();
					Test->TestNull(TEXT("Player ragdoll is detached from its camera capsule"), Mesh->GetAttachParent());
					const FVector Before = Mesh->GetBoneLocation(TEXT("pelvis"));
					const FBodyInstance* PelvisBody = Mesh->GetBodyInstance(TEXT("pelvis"));
					Test->TestNotNull(TEXT("Crash has an actual physical pelvis body"), PelvisBody);
					const FTransform PhysicalBefore = PelvisBody ? PelvisBody->GetUnrealWorldTransform() : FTransform::Identity;
					const FVector VelocityBefore = PelvisBody ? PelvisBody->GetUnrealWorldVelocity() : FVector::ZeroVector;
					const FVector Capsule = Player->GetActorLocation();
					Player->GetCapsuleComponent()->SetWorldLocation(Capsule + FVector(60.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
					Test->TestTrue(TEXT("Camera capsule movement leaves physical pelvis in world space"), Mesh->GetBoneLocation(TEXT("pelvis")).Equals(Before, .01f));
					if (PelvisBody)
					{
						Test->TestTrue(TEXT("Capsule teleport preserves actual pelvis physics transform"), PelvisBody->GetUnrealWorldTransform().Equals(PhysicalBefore, .01f));
						Test->TestTrue(TEXT("Capsule teleport preserves actual pelvis physics velocity"), PelvisBody->GetUnrealWorldVelocity().Equals(VelocityBefore, .01f));
						Test->AddInfo(FString::Printf(TEXT("Physical capsule/body isolation: pelvis position delta %.6f cm; velocity delta %.6f cm/s"), FVector::Dist(PelvisBody->GetUnrealWorldTransform().GetLocation(), PhysicalBefore.GetLocation()), FVector::Dist(PelvisBody->GetUnrealWorldVelocity(), VelocityBefore)));
					}
					Player->GetCapsuleComponent()->SetWorldLocation(Capsule, false, nullptr, ETeleportType::TeleportPhysics);
				}
				if (bPlayerDown && !bPlayerUp && !PlayerImpact->IsDown()) PlayerUpAt = Player->GetActorLocation();
				if (bNpcDown && !N->BodyImpact->IsDown() && !N->IsBusy()) bNpcUp = true;
				if (bPlayerDown && !bPlayerUp && !PlayerImpact->IsDown())
				{
					bPlayerUp = true;
				}
				const bool bDone = bNpcDown && bNpcUp && bPlayerDown && bPlayerUp;
				if (!bDone && Now - PhaseStart < 14.f) return false;
				Test->TestTrue(TEXT("Crash followed active flight animation, including its mesh-lean handoff"), bSawFlightPose);
				Test->TestTrue(TEXT("Get-up restores original mesh parent and socket"), Player->GetMesh()->GetAttachParent() == MeshParent.Get() && Player->GetMesh()->GetAttachSocketName() == MeshSocket);
				Test->TestTrue(TEXT("Get-up restores original animation-frame transform"), Player->GetMesh()->GetRelativeTransform().Equals(MeshRelative, .01f));
				Test->TestTrue(TEXT("NPC get-up restores original mesh parent and animation frame"), N->GetMesh()->GetAttachParent() == NpcMeshParent.Get() && N->GetMesh()->GetRelativeTransform().Equals(NpcMeshRelative, .01f));
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
		TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> Input;
		TWeakObjectPtr<UInputAction> MoveAction;
		TWeakObjectPtr<USceneComponent> MeshParent;
		TWeakObjectPtr<USceneComponent> NpcMeshParent;
		FName MeshSocket;
		FTransform MeshRelative;
		FTransform NpcMeshRelative;
		ACameraActor* Camera = nullptr;
		FVector Dir = FVector::ForwardVector, Look = FVector::ZeroVector, PlayerDownAt = FVector::ZeroVector, PlayerUpAt = FVector::ZeroVector;
		int32 Frames = 0, Phase = 0, Shot = 0;
		float Start = 0.f, PhaseStart = 0.f, LastShot = -1.f;
		bool bNpcDown = false, bNpcUp = false, bPlayerDown = false, bPlayerUp = false;
		bool bFirstPerson = false, bSawFlightPose = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBodyImpactTest, "RPG.Npc.Collision", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRPGBodyImpactTest::RunTest(const FString& Parameters)
{
	TSharedRef<FImpactLogCapture> Log = MakeShared<FImpactLogCapture>();
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FRPGImpactCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FCheckImpactLog(this, Log));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGFirstPersonCrashTest, "RPG.Player.FlightCrashFirstPerson", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRPGFirstPersonCrashTest::RunTest(const FString&)
{
	TSharedRef<FImpactLogCapture> Log = MakeShared<FImpactLogCapture>();
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FRPGImpactCommand(this, true));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FCheckImpactLog(this, Log));
	return true;
}

#endif
