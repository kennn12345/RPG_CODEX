// Visual check (needs rendering, so it is NOT under "RPG"; the name must not contain it either, RunTests matches substrings): first-person screenshots of the combat stance guard and the
// block (RT + LT) - going in, held, held while walking, coming out - through the real first-person camera on the mesh.
//   UnrealEditor.exe RPG_CODEX.uproject -ExecCmds="Automation RunTests Visual.FPGuardShots;Quit" -unattended
// PNGs go to Saved/Screenshots/FPGuard/ (or RPG_SHOT_DIR if set). Every frame from the block going in to the stance after it
// is logged (FPGUARDTRACK: both fists' screen x, y and distance from the camera, the body, the montages and their weights);
// RPG_SHOT_TRACKONLY=1 skips the captures so the track has no capture stalls.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "RPG_CODEX.h"
#include "RPGAnimInstance.h"
#include "RPGCameraModeComponent.h"
#include "RPGCombatTestDummy.h"
#include "RPGControllerInputComponent.h"
#include "Components/CapsuleComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealClient.h"

namespace
{
	struct FShotStep
	{
		float At;            // seconds after the step list starts
		FString Action;      // "punch", "block_on", "block_off", "move_fwd", "move_back", "move_side", "move_off", "shot:<name>"
	};

	class FRPGGuardShotsCommand : public IAutomationLatentCommand
	{
	public:
		explicit FRPGGuardShotsCommand(FAutomationTestBase* InTest) : Test(InTest)
		{
			Dir = FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_SHOT_DIR"));
			if (Dir.IsEmpty()) Dir = FPaths::ProjectSavedDir() / TEXT("Screenshots/FPGuard");
			Steps = {
				{0.0f, TEXT("punch")},                 // one tap so the combat stance (the normal guard) is up
				{1.6f, TEXT("shot:01_combat_stance")},
				{2.0f, TEXT("block_on")},
				{2.06f, TEXT("shot:02_block_in_0.06s")},
				{2.15f, TEXT("shot:03_block_in_0.15s")},
				{2.30f, TEXT("shot:04_block_in_0.30s")},
				{2.9f, TEXT("shot:05_block_held")},
				{3.6f, TEXT("shot:06_block_held_breath")},
				{3.8f, TEXT("move_fwd")},
				{4.25f, TEXT("shot:07_block_walking_fwd_a")},
				{4.4f, TEXT("shot:08_block_walking_fwd_b")},
				{4.45f, TEXT("move_back")},
				{4.9f, TEXT("shot:08b_block_walking_back")},
				{5.0f, TEXT("move_side")},
				{5.3f, TEXT("shot:08c_block_strafing")},
				{5.4f, TEXT("move_off")},
				{5.8f, TEXT("block_off")},
				{5.88f, TEXT("shot:09_block_out_0.08s")},
				{6.0f, TEXT("shot:10_block_out_0.2s")},
				{6.4f, TEXT("shot:11_back_to_stance")},
				{6.6f, TEXT("move_back")},
				{7.3f, TEXT("shot:12_stance_walking_back")},
				{7.5f, TEXT("move_off")},
				{8.0f, TEXT("end")},
			};
		}

		virtual bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World) return Wait(TEXT("no play world"));
			ACharacter* Player = UGameplayStatics::GetPlayerCharacter(World, 0);
			APlayerController* Controller = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
			if (!Player || !Controller) return Wait(TEXT("no player"));
			const float Now = World->GetTimeSeconds();
			if (Start < 0.f)
			{
				// First person, standing 6 m from the dummy (room to walk up), looking at it.
				for (TActorIterator<ARPGCombatTestDummy> It(World); It; ++It) { Dummy = *It; It->StopWanderingAtHome(); }
				// RPG_SHOT_THIRDPERSON=1: the same sequence through the third-person camera
				const bool bWantFirstPerson = FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_SHOT_THIRDPERSON")).IsEmpty();
				if (URPGCameraModeComponent* Cam = Player->FindComponentByClass<URPGCameraModeComponent>(); Cam && Cam->bIsFirstPerson != bWantFirstPerson) Cam->TogglePerspective();
				if (Dummy.IsValid())
				{
					const FVector Facing = Dummy->GetActorForwardVector().GetSafeNormal2D();
					FVector L = Dummy->GetActorLocation() + Facing * 600.f;
					L.Z = Dummy->GetActorLocation().Z - Dummy->Capsule->GetScaledCapsuleHalfHeight() + Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.f;
					Player->SetActorLocationAndRotation(L, (-Facing).Rotation(), false, nullptr, ETeleportType::ResetPhysics);
					Controller->SetControlRotation((-Facing).Rotation());
				}
				if (const USkeleton* Skel = Player->GetMesh()->GetAnimInstance() ? Player->GetMesh()->GetAnimInstance()->CurrentSkeleton : nullptr)
					UE_LOG(LogRPGInput, Display, TEXT("FPGUARDSHOT slot groups: DefaultSlot %s, UpperBody %s"), *Skel->GetSlotGroupName(TEXT("DefaultSlot")).ToString(), *Skel->GetSlotGroupName(TEXT("UpperBody")).ToString());
				Start = Now + 1.0f;   // let the camera settle
				return false;
			}
			const float T = Now - Start;
			UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Controller->GetLocalPlayer());
			UObject* Owner = Player->FindComponentByClass<URPGControllerInputComponent>();
			auto Find = [&](const TCHAR* Name) -> UInputAction* { return Owner ? FindObject<UInputAction>(Owner, Name) : nullptr; };
			UInputAction* Move = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Move.IA_Move"));
			while (Next < Steps.Num() && T >= Steps[Next].At)
			{
				const FString& A = Steps[Next].Action;
				if (A == TEXT("punch") && Input) { if (UInputAction* RT = Find(TEXT("IA_RightAttack"))) { Input->StartContinuousInputInjectionForAction(RT, FInputActionValue(1.f), {}, {}); TapOff = Now + 0.08f; } }
				else if (A == TEXT("block_on") && Input) { for (const TCHAR* N : {TEXT("IA_RightAttack"), TEXT("IA_Defense")}) if (UInputAction* B = Find(N)) Input->StartContinuousInputInjectionForAction(B, FInputActionValue(1.f), {}, {}); }
				else if (A == TEXT("block_off") && Input) { for (const TCHAR* N : {TEXT("IA_RightAttack"), TEXT("IA_Defense")}) if (UInputAction* B = Find(N)) Input->StopContinuousInputInjectionForAction(B); }
				else if (A == TEXT("move_fwd") && Input && Move) Input->StartContinuousInputInjectionForAction(Move, FInputActionValue(FVector2D(0.f, 1.f)), {}, {});
				else if (A == TEXT("move_back") && Input && Move) Input->StopContinuousInputInjectionForAction(Move), Input->StartContinuousInputInjectionForAction(Move, FInputActionValue(FVector2D(0.f, -1.f)), {}, {});
				else if (A == TEXT("move_side") && Input && Move) { Input->StopContinuousInputInjectionForAction(Move); Input->StartContinuousInputInjectionForAction(Move, FInputActionValue(FVector2D(1.f, 0.f)), {}, {}); }
				else if (A == TEXT("move_off") && Input && Move) Input->StopContinuousInputInjectionForAction(Move);
				else if (A.StartsWith(TEXT("shot:")))
				{
					const FString File = Dir / (A.Mid(5) + TEXT(".png"));
					// RPG_SHOT_TRACKONLY=1: no captures, so no capture stalls in the per-frame track
					if (FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_SHOT_TRACKONLY")).IsEmpty()) FScreenshotRequest::RequestScreenshot(File, false, false);
					// Where the hands really are: mesh space (compare with the ProtoAnim model) and screen (-1..1, - = left / down).
					FString Where;
					int32 VX = 0, VY = 0; Controller->GetViewportSize(VX, VY);
					USkeletalMeshComponent* M = Player->GetMesh();
					const FTransform MT = M->GetComponentTransform();
					for (const TCHAR* B : {TEXT("hand_l"), TEXT("hand_r"), TEXT("middle_01_l"), TEXT("middle_01_r"), TEXT("lowerarm_l"), TEXT("lowerarm_r")})
					{
						const FVector W = M->GetBoneLocation(B);
						FVector2D P(0.f, 0.f); const bool bOn = Controller->ProjectWorldLocationToScreen(W, P, true);
						const FVector L = MT.InverseTransformPosition(W);
						Where += FString::Printf(TEXT(" %s[%.1f,%.1f,%.1f](%+.2f,%+.2f)%s"), B, L.X, L.Y, L.Z, VX ? 2.f * P.X / VX - 1.f : 0.f, VY ? 1.f - 2.f * P.Y / VY : 0.f, bOn ? TEXT("") : TEXT("[off]"));
					}
					const APlayerCameraManager* PCM = Controller->PlayerCameraManager;
					const FVector CamLocal = MT.InverseTransformPosition(PCM->GetCameraLocation());
					const FVector CamFwd = MT.InverseTransformVector(PCM->GetCameraRotation().Vector()).GetSafeNormal();
					Where += FString::Printf(TEXT(" camfwd(mesh) %s"), *CamFwd.ToString());
					UE_LOG(LogRPGInput, Display, TEXT("FPGUARDSHOT %s | viewport %dx%d fov %.1f cam(mesh space) %s |%s"), *A.Mid(5), VX, VY, PCM->GetFOVAngle(), *CamLocal.ToString(), *Where);
				}
				else if (A == TEXT("end")) { Test->AddInfo(TEXT("FPGUARDSHOT done: ") + Dir); return true; }
				++Next;
			}
			// Per-frame screen track of both fists through the block going in / held / coming out, to catch snaps.
			if (T >= 1.9f && T <= 6.5f)
			{
				USkeletalMeshComponent* M = Player->GetMesh();
				int32 VX = 0, VY = 0; Controller->GetViewportSize(VX, VY);
				FString Row = FString::Printf(TEXT("FPGUARDTRACK %.3f"), T);
				for (const TCHAR* S : {TEXT("l"), TEXT("r")})
				{
					const FVector Fist = FMath::Lerp(M->GetBoneLocation(*FString::Printf(TEXT("hand_%s"), S)), M->GetBoneLocation(*FString::Printf(TEXT("middle_01_%s"), S)), 0.6f);
					FVector2D P(0.f, 0.f); Controller->ProjectWorldLocationToScreen(Fist, P, true);
					Row += FString::Printf(TEXT(" %+.3f %+.3f %.2f"), VX ? 2.f * P.X / VX - 1.f : 0.f, VY ? 1.f - 2.f * P.Y / VY : 0.f, FVector::Dist(Fist, Controller->PlayerCameraManager->GetCameraLocation()));
				}
				// body in mesh space: pelvis, chest, left fist (cm), and the camera
				{
					const FTransform MT = M->GetComponentTransform();
					for (const TCHAR* B : {TEXT("pelvis"), TEXT("spine_05"), TEXT("hand_l")})
					{
						const FVector L = MT.InverseTransformPosition(M->GetBoneLocation(B));
						Row += FString::Printf(TEXT(" %s[%.1f,%.1f,%.1f]"), B, L.X, L.Y, L.Z);
					}
					const FVector C = MT.InverseTransformPosition(Controller->PlayerCameraManager->GetCameraLocation());
					Row += FString::Printf(TEXT(" cam[%.1f,%.1f,%.1f]"), C.X, C.Y, C.Z);
					if (const URPGAnimInstance* RA = Cast<URPGAnimInstance>(M->GetAnimInstance())) Row += FString::Printf(TEXT(" aim[%.1f,%.1f]"), RA->AimYaw, RA->AimPitch);
					Row += FString::Printf(TEXT(" v%.0f"), Player->GetVelocity().Size2D());
				}
				// which montages are up, on which slot, at what weight
				if (UAnimInstance* Anim = M->GetAnimInstance())
					for (const FAnimMontageInstance* MI : Anim->MontageInstances)
						if (MI && MI->Montage && MI->Montage->SlotAnimTracks.Num() > 0 && MI->Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() > 0)
						{
							const UAnimSequenceBase* Clip = MI->Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference();
							Row += FString::Printf(TEXT(" | %s@%s w%.2f%s"), Clip ? *Clip->GetName().Replace(TEXT("RPG_"), TEXT("")) : TEXT("?"),
								*MI->Montage->SlotAnimTracks[0].SlotName.ToString(), MI->GetWeight(), MI->IsStopped() ? TEXT(" stopping") : TEXT(""));
						}
				UE_LOG(LogRPGInput, Display, TEXT("%s"), *Row);
			}
			if (TapOff > 0.f && Now >= TapOff && Input) { if (UInputAction* RT = Find(TEXT("IA_RightAttack"))) Input->StopContinuousInputInjectionForAction(RT); TapOff = -1.f; }
			return false;
		}

	private:
		bool Wait(const TCHAR* Why)
		{
			if (++WaitFrames < 900) return false;
			Test->AddError(FString::Printf(TEXT("FPGUARDSHOT could not start: %s"), Why));
			return true;
		}

		FAutomationTestBase* Test;
		FString Dir;
		TArray<FShotStep> Steps;
		TWeakObjectPtr<ARPGCombatTestDummy> Dummy;
		float Start = -1.f, TapOff = -1.f;
		int32 Next = 0, WaitFrames = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGFirstPersonGuardShots, "Visual.FPGuardShots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRPGFirstPersonGuardShots::RunTest(const FString& Parameters)
{
	// PIE in its own 1600x900 window, so the shots are 16:9 whatever the editor viewport is
	ULevelEditorPlaySettings* Play = NewObject<ULevelEditorPlaySettings>();
	Play->NewWindowWidth = 1600; Play->NewWindowHeight = 900; Play->NewWindowPosition = FIntPoint(0, 0); Play->CenterNewWindow = false;
	Play->LastExecutedPlayModeType = PlayMode_InEditorFloating;
	FRequestPlaySessionParams Session;
	Session.WorldType = EPlaySessionWorldType::PlayInEditor;
	Session.EditorPlaySettings = Play;
	GEditor->RequestPlaySession(Session);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRPGGuardShotsCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
