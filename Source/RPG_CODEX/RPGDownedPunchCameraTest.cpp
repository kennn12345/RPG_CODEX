#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "RPGBodyImpactComponent.h"
#include "RPGCameraModeComponent.h"
#include "RPGCombatStateComponent.h"
#include "RPGControllerInputComponent.h"
#include "RPGMeleeHitComponent.h"
#include "RPGNpcCharacter.h"
#include "RPGPhysicsReactionComponent.h"
#include "RPGPowerArena.h"
#include "RPGPrototypeAnimationComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

namespace
{
class FDownedPunchCamera : public IAutomationLatentCommand
{
public:
	FDownedPunchCamera(FAutomationTestBase* InTest, float InPitch, bool bInLeft) : Test(InTest), Pitch(InPitch), bLeft(bInLeft) {}
	bool Update() override
	{
		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		if (World && World->bIsTearingDown) return false;
		if (World && FixtureWorld.Get() != World)
		{
			FixtureWorld = World; Npc.Reset(); Action.Reset(); Input.Reset();
			Phase = 0; Started = MaxYaw = MaxYawStep = MaxViewYawStep = MaxPitch = MaxActorYaw = 0.f;
			bSawPunch = bSawStrike = bLookQueued = false; bStayedPhysical = true; Rows.Reset();
		}
		ACharacter* Player = World ? UGameplayStatics::GetPlayerCharacter(World, 0) : nullptr;
		APlayerController* PC = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
		if (!Npc.IsValid() && World) Npc = RPGPowerArena::FindReferenceNpc(World);
		if (!PC || !PC->PlayerCameraManager || !Npc.IsValid()) return ++Wait < 900 ? false : Fail(TEXT("missing PIE actors/camera"));
		ARPGNpcCharacter* N = Npc.Get();
		const float Now = World->GetTimeSeconds();
		if (Phase == 4)
		{
			const float T = Now - Started;
			if (T < .08f) Input->InjectInputVectorForAction(Action.Get(), FVector(1, 0, 0), {}, {});
			if (T >= .2f && !bLookQueued)
			{
				ManualYawBefore = PC->GetControlRotation().Yaw;
				PC->AddYawInput(2.f);
				bLookQueued = true;
			}
			if (T < .4f) return false;
			const float LookTurn = FMath::FindDeltaAngleDegrees(ManualYawBefore, PC->GetControlRotation().Yaw);
			Test->TestTrue(TEXT("Intentional yaw look input still works during a downward punch"), LookTurn > .5f && LookTurn < 30.f);
			Test->AddInfo(FString::Printf(TEXT("DOWNCAM_LOOK hand=%s pitch=%.0f manualYawTurn=%.3f"), bLeft ? TEXT("L") : TEXT("R"), Pitch, LookTurn));
			return true;
		}
		if (Phase == 0)
		{
			if (Now < 1.2f) return false;
			N->bWander = false;
			N->GetController()->StopMovement();
			Player->SetActorLocation(N->GetActorLocation() + FVector(0, 1000, 0), false, nullptr, ETeleportType::TeleportPhysics);
			N->BodyImpact->DownTime = 30.f;
			N->BodyImpact->KnockDown(FVector(0, 0, 20), N->GetMesh()->GetBoneLocation(TEXT("pelvis")));
			if (auto* Mode = Player->FindComponentByClass<URPGCameraModeComponent>(); Mode && Mode->bIsFirstPerson) Mode->TogglePerspective();
			Started = Now; Phase = 1; return false;
		}
		if (Phase == 1)
		{
			if (Now - Started < 1.f) return false;
			const FVector Pelvis = N->GetMesh()->GetBoneLocation(TEXT("pelvis"));
			// Stand directly over the ragdoll, leaving it physical and leaving boom collision/lag enabled.
			Player->SetActorLocationAndRotation(FVector(Pelvis.X + 20.f, Pelvis.Y, N->GetActorLocation().Z), FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
			Player->GetCharacterMovement()->StopMovementImmediately();
			Started = Now; Phase = 2; return false;
		}
		if (Phase == 2)
		{
			PC->SetControlRotation(FRotator(Pitch, 0, 0));
			if (Now - Started < .6f) return false;
			auto* ControllerInput = Player->FindComponentByClass<URPGControllerInputComponent>();
			Action = ControllerInput ? FindObject<UInputAction>(ControllerInput, bLeft ? TEXT("IA_Defense") : TEXT("IA_RightAttack")) : nullptr;
			Input = PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
			if (!Action.IsValid() || !Input.IsValid()) return Fail(TEXT("missing real punch input"));
			StartControl = LastControl = PC->GetControlRotation();
			LastView = PC->PlayerCameraManager->GetCameraRotation();
			Started = Now; Phase = 3;
			Rows = TEXT("t,pitch,control_yaw,view_pitch,view_yaw,actor_yaw,eye_x,eye_y,eye_z,attack_phase,hit_count\n");
		}
		const float T = Now - Started;
		const FRotator Control = PC->GetControlRotation(), View = PC->PlayerCameraManager->GetCameraRotation();
		const FVector Eye = PC->PlayerCameraManager->GetCameraLocation();
		MaxYaw = FMath::Max(MaxYaw, FMath::Abs(FMath::FindDeltaAngleDegrees(StartControl.Yaw, Control.Yaw)));
		MaxYawStep = FMath::Max(MaxYawStep, FMath::Abs(FMath::FindDeltaAngleDegrees(LastControl.Yaw, Control.Yaw)));
		MaxViewYawStep = FMath::Max(MaxViewYawStep, FMath::Abs(FMath::FindDeltaAngleDegrees(LastView.Yaw, View.Yaw)));
		MaxPitch = FMath::Max(MaxPitch, FMath::Abs(FMath::FindDeltaAngleDegrees(StartControl.Pitch, Control.Pitch)));
		MaxActorYaw = FMath::Max(MaxActorYaw, FMath::Abs(FRotator::NormalizeAxis(Player->GetActorRotation().Yaw)));
		LastControl = Control; LastView = View;
		const auto* Animation = Player->FindComponentByClass<URPGPrototypeAnimationComponent>();
		const auto* State = Player->FindComponentByClass<URPGCombatStateComponent>();
		const auto* Melee = Player->FindComponentByClass<URPGMeleeHitComponent>();
		bSawPunch |= Animation && Animation->IsCapturedStandardPunchPlaying();
		bSawStrike |= State && State->AttackPhase == ERPGAttackPhase::Strike;
		bStayedPhysical &= N->PhysicsReaction->GetReactionState() == ERPGPhysicsReactionState::Limp && N->PhysicsReaction->GetNumSimulatingBodies() > 0;
		Rows += FString::Printf(TEXT("%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.3f,%.3f,%.3f,%d,%d\n"), T, Control.Pitch, Control.Yaw, View.Pitch, View.Yaw, Player->GetActorRotation().Yaw, Eye.X, Eye.Y, Eye.Z, State ? int32(State->AttackPhase) : -1, Melee ? Melee->ConfirmedHitCount : 0);
		// Three ordinary taps, no injected look input and no per-frame camera correction by the fixture.
		for (float Press : {0.f, 1.f, 2.f})
			if (T >= Press && T < Press + .08f) Input->InjectInputVectorForAction(Action.Get(), FVector(1, 0, 0), {}, {});
		if (T < 3.1f) return false;
		const FString Directory = FPaths::ProjectSavedDir() / TEXT("DownedCameraReview");
		IFileManager::Get().MakeDirectory(*Directory, true);
		FFileHelper::SaveStringToFile(Rows, *(Directory / FString::Printf(TEXT("%s_%.0f.csv"), bLeft ? TEXT("L") : TEXT("R"), -Pitch)));
		Test->TestTrue(TEXT("Real standard punch animation and strike phase exercised"), bSawPunch && bSawStrike);
		Test->TestTrue(TEXT("Target stayed in actual ragdoll physics throughout the punches"), bStayedPhysical);
		Test->TestTrue(TEXT("No uncontrolled camera yaw drift while looking down"), MaxYaw < (Pitch <= -75.f ? 5.f : 15.f));
		Test->TestTrue(TEXT("No abrupt control/view yaw flips"), MaxYawStep < 5.f && MaxViewYawStep < 5.f);
		Test->TestTrue(TEXT("No uncontrolled pitch correction"), MaxPitch < 6.f);
		Test->AddInfo(FString::Printf(TEXT("DOWNCAM hand=%s pitch=%.0f yawDrift=%.3f controlStep=%.3f viewStep=%.3f pitchDrift=%.3f actorYaw=%.3f"), bLeft ? TEXT("L") : TEXT("R"), Pitch, MaxYaw, MaxYawStep, MaxViewYawStep, MaxPitch, MaxActorYaw));
		Phase = 4; Started = Now;
		return false;
	}
private:
	bool Fail(const TCHAR* Why) { Test->AddError(Why); return true; }
	FAutomationTestBase* Test;
	float Pitch;
	bool bLeft;
	TWeakObjectPtr<ARPGNpcCharacter> Npc;
	TWeakObjectPtr<UWorld> FixtureWorld;
	TWeakObjectPtr<UInputAction> Action;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> Input;
	int32 Wait = 0, Phase = 0;
	float Started = 0.f, MaxYaw = 0.f, MaxYawStep = 0.f, MaxViewYawStep = 0.f, MaxPitch = 0.f, MaxActorYaw = 0.f, ManualYawBefore = 0.f;
	FRotator StartControl, LastControl, LastView;
	bool bSawPunch = false, bSawStrike = false;
	bool bStayedPhysical = true, bLookQueued = false;
	FString Rows;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDownedPunchCameraTest, "RPG.Camera.DownedPunch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRPGDownedPunchCameraTest::RunTest(const FString&)
{
	for (float Pitch : {-60.f, -75.f, -85.f, -89.f})
		for (bool bLeft : {false, true})
		{
			ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
			ADD_LATENT_AUTOMATION_COMMAND(FDownedPunchCamera(this, Pitch, bLeft));
			ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
		}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDownedPunchVerticalTest, "RPG.Camera.DownedPunchVertical", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRPGDownedPunchVerticalTest::RunTest(const FString&)
{
	for (float Pitch : {-85.f, -89.f})
		for (bool bLeft : {false, true})
		{
			ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
			ADD_LATENT_AUTOMATION_COMMAND(FDownedPunchCamera(this, Pitch, bLeft));
			ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
		}
	return true;
}
#endif
