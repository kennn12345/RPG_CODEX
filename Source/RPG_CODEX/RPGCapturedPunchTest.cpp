#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "RPGPrototypeAnimationComponent.h"
#include "RPGControllerInputComponent.h"
#include "RPGCombatStateComponent.h"
#include "RPGCombatAimComponent.h"
#include "RPGCameraModeComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
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
#include "Settings/LevelEditorPlaySettings.h"
#include "UnrealClient.h"

namespace
{
void RequestPunchVisualPIE()
{
    auto* Play=NewObject<ULevelEditorPlaySettings>();Play->NewWindowWidth=1280;Play->NewWindowHeight=720;
    Play->CenterNewWindow=false;Play->LastExecutedPlayModeType=PlayMode_InEditorFloating;
    FRequestPlaySessionParams Session;Session.WorldType=EPlaySessionWorldType::PlayInEditor;Session.EditorPlaySettings=Play;
    GEditor->RequestPlaySession(Session);
}
class FCapturedPunchesPlay : public IAutomationLatentCommand
{
public:
    FCapturedPunchesPlay(FAutomationTestBase* InTest,bool InFirst=false,bool InVisual=false):Test(InTest),bFirst(InFirst),bVisual(InVisual) {}
    bool Update() override
    {
        UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
        ACharacter* Player=World?UGameplayStatics::GetPlayerCharacter(World,0):nullptr;
        APlayerController* Controller=Player?Cast<APlayerController>(Player->GetController()):nullptr;
        URPGPrototypeAnimationComponent* Animation=Player?Player->FindComponentByClass<URPGPrototypeAnimationComponent>():nullptr;
        URPGCombatStateComponent* State=Player?Player->FindComponentByClass<URPGCombatStateComponent>():nullptr;
        URPGControllerInputComponent* Buttons=Player?Player->FindComponentByClass<URPGControllerInputComponent>():nullptr;
        if (!Player || !Controller || !Animation || !State || !Buttons)
        { if (++Wait<600) return false; Test->AddError(TEXT("Captured punches: player components unavailable"));return true; }
        const float Now=World->GetTimeSeconds();
        if (!bReady)
        {
            bReady=true;GlobalStart=Now;
            const auto* Reference=IConsoleManager::Get().FindConsoleVariable(TEXT("rpg.ReferencePunches"));
            bReferenceReview=Reference && Reference->GetInt()!=0;
            AStaticMeshActor* Floor=World->SpawnActor<AStaticMeshActor>(FVector(12000,16000,-25),FRotator::ZeroRotator);
            Floor->SetMobility(EComponentMobility::Movable);Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
            Floor->SetActorScale3D(FVector(50,50,.5));Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
            Player->SetActorLocationAndRotation(FVector(12000,16000,Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2),FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
            Player->GetCharacterMovement()->StopMovementImmediately();
            Player->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            State->SetEquippedHandTypes(ERPGHandItemType::Empty,ERPGHandItemType::Empty);
            if (URPGCameraModeComponent* Mode=Player->FindComponentByClass<URPGCameraModeComponent>();Mode && Mode->bIsFirstPerson!=bFirst) Mode->TogglePerspective();
            Controller->SetControlRotation(Player->GetActorRotation());Animation->CombatStanceTimeout=120.f;
            if (bVisual)
            {
                if(!bFirst)
                {
                ACameraActor* Camera=World->SpawnActor<ACameraActor>();Camera->GetCameraComponent()->SetFieldOfView(43.f);Camera->GetCameraComponent()->bConstrainAspectRatio=false;
                Camera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount=true;Camera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount=0.f;
                const FVector Look=Player->GetActorLocation()+FVector(10,0,-10);const FVector Eye=Look+(bReferenceReview?FVector(-250,360,155):FVector(420,350,80));
                Camera->SetActorLocationAndRotation(Eye,(Look-Eye).Rotation());Controller->bAutoManageActiveCameraTarget=false;Controller->SetViewTarget(Camera);
                }
                Dir=FPaths::ProjectSavedDir()/(bReferenceReview?(bFirst?TEXT("ReferenceCombat1027/GameplayFP"):TEXT("ReferenceCombat1027/GameplayTP")):TEXT("Mocap8630/GameplayVisual"));
                const FString Tag=FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_REFERENCE_PLAY_TAG"));if(!Tag.IsEmpty())Dir+=TEXT("_")+Tag;
                IFileManager::Get().MakeDirectory(*Dir,true);
            }
            Phase=-1;Start=Now;return false;
        }
        if (Phase==-1) {if (Now-Start<.5f)return false;Phase=0;}
        const int32 Total=bVisual?(bReferenceReview && FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_REFERENCE_VISUAL_ALL_TIERS")).IsEmpty()?2:6):18;
        const int32 Tier=Case/(bVisual?2:6),Variant=bVisual?0:(Case%6)/2;
        const bool bLeft=Case%2==1;
        if (bVisual && Case<Total && Now-LastShot>=1.f/30.f && !FScreenshotRequest::IsScreenshotRequested())
        {
            Captures+=FString::Printf(TEXT("%d,%.6f,%d,%d,%d,%d\n"),Shot,Now-GlobalStart,Case,Tier,bLeft,Phase);
            FScreenshotRequest::RequestScreenshot(Dir/FString::Printf(TEXT("frame_%05d.png"),Shot++),false,false);LastShot=Now;
        }
        if (Phase==0)
        {
            Before=Animation->CapturedPunchReleaseCount;BeforeStrike=State->StrikeId;bSawClip=false;bSawStance=false;Start=Now;
            Hold=Tier==0?FMath::Min(.08f,Buttons->TapMaxTime*.5f):Tier==1?(Buttons->TapMaxTime+Buttons->FullChargeTime)*.5f:Buttons->FullChargeTime+.15f;
            if (!bVisual && Case==17) { Hold=Buttons->FullChargeTime+9.f;Animation->CombatStanceTimeout=8.f; }
            Press(Player,Controller,bLeft,true);Phase=1;return false;
        }
        if (Phase==1)
        {
            if (Animation->CapturedPunchReleaseCount!=Before || State->StrikeId!=BeforeStrike)
            {Test->AddError(TEXT("Held trigger fired before release or repeated a punch"));return true;}
            if (Now-Start<Hold)return false;
            Press(Player,Controller,bLeft,false);Released=Now;Phase=2;return false;
        }
        if (Phase==2)
        {
            const TCHAR* Tiers[]={TEXT("Light"),TEXT("Heavy"),TEXT("Power")};
            const auto* Reference=IConsoleManager::Get().FindConsoleVariable(TEXT("rpg.ReferencePunches"));
            const bool bReference=Reference && Reference->GetInt()!=0;
            const FString Expected=FString::Printf(TEXT("RPG_%sPunch_%s_%s%s_V%d"),bFirst?TEXT("FP_"):TEXT(""),bLeft?TEXT("L"):TEXT("R"),Tiers[Tier],bReference?TEXT("_Reference1027"):TEXT(""),Variant+1);
            const FString Stance=FString::Printf(TEXT("RPG_%sCombatStance_%s"),bFirst?TEXT("FP_"):TEXT(""),bReference?TEXT("Reference1027"):TEXT("Capture8619"));
            if (UAnimInstance* Instance=Player->GetMesh()->GetAnimInstance())
                for (const FAnimMontageInstance* Playing:Instance->MontageInstances)
                    if (Playing && Playing->Montage && !Playing->IsStopped())
                        for (const FSlotAnimationTrack& Track:Playing->Montage->SlotAnimTracks)
                            for (const FAnimSegment& Segment:Track.AnimTrack.AnimSegments)
                                if (Segment.GetAnimReference())
                                {
                                    const FString Name=Segment.GetAnimReference()->GetName();
                                    if (Name==Expected)bSawClip=true;
                                    if (Now-Released>.8f && Name==Stance)bSawStance=true;
                                }
            if (Now-Released<(Tier==0?1.3f:Tier==1?1.5f:1.7f))return false;
            Test->TestEqual(TEXT("Exactly one punch released per trigger press"),Animation->CapturedPunchReleaseCount,Before+1);
            Test->TestEqual(TEXT("One gameplay strike window per punch"),State->StrikeId,BeforeStrike+1);
            Test->TestEqual(TEXT("Release selects the requested charge tier"),Animation->LastCapturedPunchTier,Tier);
            Test->TestEqual(TEXT("Per-hand/per-tier variations cycle 1,2,3 independently"),Animation->LastCapturedPunchVariant,Variant);
            Test->TestTrue(TEXT("Expected captured punch really played in this perspective"),bSawClip);
            Test->TestTrue(TEXT("Every released punch returns to the approved fighting stance"),bSawStance);
            Test->TestFalse(TEXT("Recovery closes the hit window"),State->bStrikeHitWindowOpen);
            Test->TestEqual(TEXT("Recovery completes after the one strike"),State->AttackPhase,ERPGAttackPhase::None);
            Rows+=FString::Printf(TEXT("%d,%d,%d,%d,%s,%d\n"),Case,Tier,bLeft,Variant,*Animation->LastCapturedPunch.ToString(),State->StrikeId-BeforeStrike);
            ++Case;
            if (Case<Total) {Phase=0;return false;}
            Before=Animation->CapturedPunchReleaseCount;BeforeStrike=State->StrikeId;Start=Now;
            Press(Player,Controller,false,true);Phase=3;return false;
        }
        if (Phase==3)
        {if (Now-Start<.06f)return false;Press(Player,Controller,true,true);Start=Now;Phase=4;return false;}
        if (Phase==4)
        {
            if (Now-Start<1.3f)return false;
            Test->TestTrue(TEXT("Two triggers still raise the unarmed guard"),State->bIsBlocking);
            Press(Player,Controller,false,false);Press(Player,Controller,true,false);Start=Now;Phase=5;return false;
        }
        if (Now-Start<.4f)return false;
        Test->TestEqual(TEXT("Guard cancellation does not fire a queued punch"),Animation->CapturedPunchReleaseCount,Before);
        Test->TestEqual(TEXT("Guard cancellation never opens a strike window"),State->StrikeId,BeforeStrike);
        const FString Results=FPaths::ProjectSavedDir()/FString::Printf(TEXT("%s/%s-results.csv"),bReferenceReview?TEXT("ReferenceCombat1027"):TEXT("Mocap8630"),bVisual?(bFirst?TEXT("visual-first-person"):TEXT("visual")):bFirst?TEXT("first-person"):TEXT("third-person"));
        FFileHelper::SaveStringToFile(Rows,*Results);
        if (bVisual)FFileHelper::SaveStringToFile(Captures,*(Dir/TEXT("captures.csv")));
        Test->AddInfo(FString::Printf(TEXT("Captured punches: %d one-shot releases verified; hand, tier, variants, charge hold, windows, recovery and guard cancellation"),Total));
        return true;
    }
private:
    void Press(ACharacter* Player,APlayerController* Controller,bool bLeft,bool bDown)
    {
        UEnhancedInputLocalPlayerSubsystem* Input=ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Controller->GetLocalPlayer());UObject* Owner=Player->FindComponentByClass<URPGControllerInputComponent>();
        if (UInputAction* Action=Input?FindObject<UInputAction>(Owner,bLeft?TEXT("IA_Defense"):TEXT("IA_RightAttack")):nullptr)
        {if(bDown)Input->StartContinuousInputInjectionForAction(Action,FInputActionValue(1.f),{},{});else Input->StopContinuousInputInjectionForAction(Action);}
    }
    FAutomationTestBase* Test;bool bFirst=false,bVisual=false,bReady=false,bSawClip=false,bSawStance=false,bReferenceReview=false;
    int32 Phase=0,Wait=0,Case=0,Before=0,BeforeStrike=0,Shot=0;
    float Start=0,Released=0,Hold=0,GlobalStart=0,LastShot=-1;
    FString Dir,Rows=TEXT("case,tier,left,variant,clip,strikes\n"),Captures=TEXT("frame,time,case,tier,left,phase\n");
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCapturedPunchTest,"RPG.Combat.CapturedPunches",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCapturedPunchTest::RunTest(const FString&)
{ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));ADD_LATENT_AUTOMATION_COMMAND(FCapturedPunchesPlay(this));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCapturedPunchFirstTest,"RPG.Combat.CapturedPunchesFirstPerson",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCapturedPunchFirstTest::RunTest(const FString&)
{ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));ADD_LATENT_AUTOMATION_COMMAND(FCapturedPunchesPlay(this,true));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCapturedPunchVisual,"Visual.CapturedPunches",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCapturedPunchVisual::RunTest(const FString&)
{RequestPunchVisualPIE();ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));ADD_LATENT_AUTOMATION_COMMAND(FCapturedPunchesPlay(this,false,true));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCapturedPunchFirstVisual,"Visual.CapturedPunchesFirstPerson",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCapturedPunchFirstVisual::RunTest(const FString&)
{RequestPunchVisualPIE();ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));ADD_LATENT_AUTOMATION_COMMAND(FCapturedPunchesPlay(this,true,true));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;}
#endif
