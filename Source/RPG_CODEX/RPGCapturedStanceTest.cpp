#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "RPGPrototypeAnimationComponent.h"
#include "RPGCameraModeComponent.h"
#include "RPGControllerInputComponent.h"
#include "RPGCombatAimComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealClient.h"

namespace
{
class FCapturedStancePlay : public IAutomationLatentCommand
{
public:
    FCapturedStancePlay(FAutomationTestBase* InTest, bool InVisual, bool InFirstPerson=false) : Test(InTest), bVisual(InVisual), bFirstPerson(InFirstPerson) {}
    bool Update() override
    {
        UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
        ACharacter* Player = World ? UGameplayStatics::GetPlayerCharacter(World, 0) : nullptr;
        APlayerController* Controller = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
        URPGPrototypeAnimationComponent* Animation = Player ? Player->FindComponentByClass<URPGPrototypeAnimationComponent>() : nullptr;
        if (!Player || !Controller || !Animation)
        {
            if (++Wait < 600) return false;
            Test->AddError(TEXT("Captured stance: player unavailable")); return true;
        }
        const float Now = World->GetTimeSeconds();
        if (!bStarted)
        {
            bStarted = true; Start = Now;
            Player->GetCharacterMovement()->StopMovementImmediately();
            Player->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            if (URPGCameraModeComponent* Mode = Player->FindComponentByClass<URPGCameraModeComponent>(); Mode && Mode->bIsFirstPerson != bFirstPerson) Mode->TogglePerspective();
            Animation->CombatStanceTimeout = 30.f;
            Controller->SetControlRotation(Player->GetActorRotation());
            // Isolate recorded root travel/turns from ordinary gameplay aim square-up.
            // Aim remains enabled in the punch/contact/locomotion integration tests.
            if (auto* Aim=Player->FindComponentByClass<URPGCombatAimComponent>())Aim->SetComponentTickEnabled(false);
            Rotation = Player->GetActorRotation(); Location = Player->GetActorLocation();
            Press(Player, Controller, true);
            if (bVisual)
            {
                Camera = World->SpawnActor<ACameraActor>(); Camera->GetCameraComponent()->SetFieldOfView(43.f);
                Camera->GetCameraComponent()->bConstrainAspectRatio = false;
                Camera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount=true;
                Camera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount=0.f;
                const FVector Look = Location + FVector(0,0,-15);
                const FVector Eye = Look + Player->GetActorTransform().TransformVectorNoScale(FVector(400,330,75));
                Camera->SetActorLocationAndRotation(Eye, (Look-Eye).Rotation());
                Controller->bAutoManageActiveCameraTarget = false; Controller->SetViewTarget(Camera);
                Dir = FPaths::ProjectSavedDir()/TEXT("Mocap8619/GameplayVisual"); IFileManager::Get().MakeDirectory(*Dir,true);
            }
            return false;
        }
        const float T = Now-Start;
        if (!bReleased && T > .15f) { Press(Player, Controller, false); bReleased = true; }
        if (!bPunched && T > 4.f) { RightPunch(Player,Controller,true); bPunched=true; }
        if (bPunched && !bPunchReleased && T > 4.08f) { RightPunch(Player,Controller,false); bPunchReleased=true; }
        if (T > 1.5f)
        {
            const float Turn = FMath::Abs(FMath::FindDeltaAngleDegrees(Rotation.Yaw, Player->GetActorRotation().Yaw));
            MaxTurn = FMath::Max(MaxTurn, Turn);
            MaxTravel = FMath::Max(MaxTravel, FVector::Dist2D(Location, Player->GetActorLocation()));
            UAnimInstance* Instance = Player->GetMesh()->GetAnimInstance();
            if (Instance) for (const FAnimMontageInstance* Playing : Instance->MontageInstances)
                if (Playing && Playing->Montage && !Playing->IsStopped())
                    for (const FSlotAnimationTrack& Track : Playing->Montage->SlotAnimTracks)
                        for (const FAnimSegment& Segment : Track.AnimTrack.AnimSegments)
                            if (Segment.GetAnimReference())
                            {
                                const FString Name=Segment.GetAnimReference()->GetName();
                                const auto* Reference=IConsoleManager::Get().FindConsoleVariable(TEXT("rpg.ReferencePunches"));
                                const bool bReference=Reference && Reference->GetInt()!=0;
                                const FString Expected=FString::Printf(TEXT("RPG_%sCombatStance_%s"),bFirstPerson?TEXT("FP_"):TEXT(""),bReference?TEXT("Reference1027"):TEXT("Capture8619"));
                                const FString PunchPrefix=FString::Printf(TEXT("RPG_%sPunch_R_Light_%s"),bFirstPerson?TEXT("FP_"):TEXT(""),bReference?TEXT("Reference1027_V"):TEXT("V"));
                                if (Name==Expected) { bSawCapture=true; if (T>6.f) bSawReturn=true; }
                                if (Name.StartsWith(PunchPrefix)) bSawPunch=true;
                            }
        }
        if (bVisual && T-LastShot >= 1.f/30.f)
        {
            CaptureTimes+=FString::Printf(TEXT("%d,%.6f\n"),Shot,T);
            FScreenshotRequest::RequestScreenshot(Dir/FString::Printf(TEXT("frame_%05d.png"), Shot++),false,false); LastShot = T;
        }
        if (T < 9.f) return false;
        Test->TestTrue(TEXT("Real combat input uses the recorded stance"), bSawCapture);
        Test->TestTrue(TEXT("A real punch uses the fitted gameplay rig"), bSawPunch);
        Test->TestTrue(TEXT("The punch returns to the captured stance"), bSawReturn);
        Test->AddInfo(FString::Printf(TEXT("Captured stance: max actor yaw %.4f deg, travel %.4f cm"),MaxTurn,MaxTravel));
        Test->TestTrue(TEXT("Recorded reference turns do not turn the player"), MaxTurn < .5f);
        Test->TestTrue(TEXT("Recorded reference steps do not move the player"), MaxTravel < 1.f);
        if (bVisual) FFileHelper::SaveStringToFile(FString::Printf(TEXT("frames=%d\nmax_actor_yaw=%.4f\nmax_travel_cm=%.4f\ncaptured_stance=%d\n"),Shot,MaxTurn,MaxTravel,bSawCapture),*(Dir/TEXT("result.txt")));
        if (bVisual) FFileHelper::SaveStringToFile(CaptureTimes,*(Dir/TEXT("captures.csv")));
        return true;
    }
private:
    void RightPunch(ACharacter* Player,APlayerController* Controller,bool bDown)
    {
        UEnhancedInputLocalPlayerSubsystem* Input=ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Controller->GetLocalPlayer());
        UObject* Owner=Player->FindComponentByClass<URPGControllerInputComponent>();
        if (UInputAction* Action=Input?FindObject<UInputAction>(Owner,TEXT("IA_RightAttack")):nullptr)
        {
            if (bDown) Input->StartContinuousInputInjectionForAction(Action,FInputActionValue(1.f),{},{});
            else Input->StopContinuousInputInjectionForAction(Action);
        }
    }
    void Press(ACharacter* Player, APlayerController* Controller, bool bDown)
    {
        UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Controller->GetLocalPlayer());
        UObject* Owner = Player->FindComponentByClass<URPGControllerInputComponent>();
        for (const TCHAR* Name : {TEXT("IA_RightAttack"),TEXT("IA_Defense")})
            if (UInputAction* Action = Input ? FindObject<UInputAction>(Owner,Name) : nullptr)
            {
                if (bDown) Input->StartContinuousInputInjectionForAction(Action,FInputActionValue(1.f),{},{});
                else Input->StopContinuousInputInjectionForAction(Action);
            }
    }
    FAutomationTestBase* Test;
    bool bVisual=false,bFirstPerson=false,bStarted=false,bReleased=false,bSawCapture=false,bPunched=false,bPunchReleased=false,bSawPunch=false,bSawReturn=false;
    int32 Wait=0,Shot=0;
    float Start=0,LastShot=-1,MaxTurn=0,MaxTravel=0;
    FRotator Rotation; FVector Location; ACameraActor* Camera=nullptr; FString Dir;
    FString CaptureTimes=TEXT("frame,time\n");
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCapturedStanceGameplay,"RPG.Combat.CapturedStance",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCapturedStanceGameplay::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCapturedStancePlay(this,false));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand()); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCapturedStanceVisual,"Visual.CapturedStance",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCapturedStanceVisual::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCapturedStancePlay(this,true));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand()); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCapturedStanceFirstPerson,"RPG.Combat.CapturedStanceFirstPerson",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCapturedStanceFirstPerson::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCapturedStancePlay(this,false,true));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand()); return true;
}
#endif
