#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "RPGPrototypeAnimationComponent.h"
#include "RPGCombatStateComponent.h"
#include "RPGCameraModeComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealClient.h"

namespace
{
/** Name of the playing montage clip containing Part, or empty. */
FString PlayingClip(UAnimInstance* Anim,const TCHAR* Part)
{
    for(const FAnimMontageInstance* Playing:Anim->MontageInstances)
        if(Playing && Playing->Montage && !Playing->IsStopped() && Playing->GetWeight()>.01f)
            for(const auto& Track:Playing->Montage->SlotAnimTracks)
                for(const auto& Segment:Track.AnimTrack.AnimSegments)
                    if(Segment.GetAnimReference() && Segment.GetAnimReference()->GetName().Contains(Part))return Segment.GetAnimReference()->GetName();
    return FString();
}
/**
 * The filmed punches (IMG_8634; the left one mirrored): for each hand a tap and a held press released as a Heavy
 * attack. Checks the actual clip, one strike window per punch, real fist travel and the return to the fighting stance.
 */
class FFilmedPunchReview:public IAutomationLatentCommand
{
public:
    FFilmedPunchReview(FAutomationTestBase* InTest,bool InFirst,bool InVisual):Test(InTest),bFirst(InFirst),bVisual(InVisual){}
    bool Update() override
    {
        UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
        ACharacter* Player=World?UGameplayStatics::GetPlayerCharacter(World,0):nullptr;
        APlayerController* Controller=Player?Cast<APlayerController>(Player->GetController()):nullptr;
        URPGPrototypeAnimationComponent* Animation=Player?Player->FindComponentByClass<URPGPrototypeAnimationComponent>():nullptr;
        URPGCombatStateComponent* State=Player?Player->FindComponentByClass<URPGCombatStateComponent>():nullptr;
        UAnimInstance* Anim=Player?Player->GetMesh()->GetAnimInstance():nullptr;
        if(!Player || !Controller || !Animation || !State || !Anim)
        {if(++Wait<600)return false;Test->AddError(TEXT("Filmed punch: player components unavailable"));return true;}
        const float Now=World->GetTimeSeconds();
        if(!bReady)
        {
            bReady=true;Start=Now;
            Player->GetCharacterMovement()->StopMovementImmediately();
            Player->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            if(URPGCameraModeComponent* Mode=Player->FindComponentByClass<URPGCameraModeComponent>();Mode && Mode->bIsFirstPerson!=bFirst)Mode->TogglePerspective();
            Animation->CombatStanceTimeout=60.f;
            if(bVisual)
            {
                Camera=World->SpawnActor<ACameraActor>();Camera->GetCameraComponent()->SetFieldOfView(40.f);
                Camera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount=true;Camera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount=0.f;
                Controller->bAutoManageActiveCameraTarget=false;Controller->SetViewTarget(Camera);
                Dir=FPaths::ProjectSavedDir()/TEXT("Mocap8634/GameplayVisual");IFileManager::Get().DeleteDirectory(*Dir,false,true);IFileManager::Get().MakeDirectory(*Dir,true);
            }
            // Enter combat first so the punch starts from the fighting stance.
            State->ReportSimpleAction(ERPGPlayerAction::UnarmedGuard,ERPGHand::None);
            State->ReportSimpleAction(ERPGPlayerAction::DefenseReleased,ERPGHand::None);
            return false;
        }
        const float T=Now-Start;
        if(bVisual && Now-LastShot>=1.f/30.f && !FScreenshotRequest::IsScreenshotRequested())
        {
            Times+=FString::Printf(TEXT("%d,%.6f,%d\n"),Shot,T,Case);
            FScreenshotRequest::RequestScreenshot(Dir/FString::Printf(TEXT("frame_%05d.png"),Shot++),false,false);LastShot=Now;
        }
        // Cases: 0 right tap, 1 right held then released as a Heavy attack, 2 left tap, 3 left held.
        const bool bLeft=Case>=2,bHeld=Case%2==1;
        const TCHAR* H=bLeft?TEXT("L"):TEXT("R");
        const FTransform Actor=Player->GetActorTransform();
        const FVector Fist=Actor.InverseTransformPosition(Player->GetMesh()->GetSocketLocation(bLeft?TEXT("hand_l"):TEXT("hand_r")));
        if(Phase==0)
        {
            if(T<1.2f)return false;
            if(!Rest[bLeft].IsSet())Rest[bLeft]=Fist;
            if(Camera)
            {
                // Side-on from the punching side at chest height, framing the whole body and the full reach.
                const FVector Look=Player->GetActorLocation()+Player->GetActorRotation().RotateVector(FVector(30,0,20));
                const FVector Eye=Look+Player->GetActorRotation().RotateVector(FVector(60,bLeft?-430:430,15));
                Camera->SetActorLocationAndRotation(Eye,(Look-Eye).Rotation());
            }
            Test->TestFalse(TEXT("Stance plays before the punch"),PlayingClip(Anim,TEXT("FightStance")).IsEmpty());
            Releases=Animation->CapturedPunchReleaseCount;Windows=0;MaxReach=0.f;bWasOpen=false;Clip.Reset();
            State->ReportSimpleAction(ERPGPlayerAction::AttackChargeStarted,bLeft?ERPGHand::Left:ERPGHand::Right);
            Phase=1;PhaseStart=Now;return false;
        }
        const float P=Now-PhaseStart;
        const FString Part=FString::Printf(TEXT("Punch_%s_Capture8634"),H);
        const FString Seen=PlayingClip(Anim,*Part);if(!Seen.IsEmpty())Clip=Seen;
        const bool bOpen=State->bStrikeHitWindowOpen && State->StrikeLimb==(bLeft?ERPGStrikeLimb::LeftHand:ERPGStrikeLimb::RightHand);
        if(bOpen && !bWasOpen){++Windows;OpenedAt=P;}
        bWasOpen=bOpen;
        MaxReach=FMath::Max(MaxReach,Fist.X-Rest[bLeft].GetValue().X);
        const FString Label=FString::Printf(TEXT("%s %s"),bLeft?TEXT("left"):TEXT("right"),bHeld?TEXT("held/Heavy"):TEXT("tap"));
        if(Phase==1)
        {
            if(P<(bHeld?.9f:.05f))return false;
            if(bHeld)
            {
                Test->TestEqual(Label+TEXT(": held press waits at the load pose (no strike yet)"),Animation->CapturedPunchReleaseCount,Releases);
                Test->TestEqual(Label+TEXT(": no strike window while held"),Windows,0);
            }
            ReleasedAt=P;
            State->ReportSimpleAction(bHeld?ERPGPlayerAction::HeavyAttack:ERPGPlayerAction::Attack,bLeft?ERPGHand::Left:ERPGHand::Right,bHeld?.6f:0.f);
            Phase=2;return false;
        }
        if(P<ReleasedAt+1.6f)return false;
        const FString Body=FString::Printf(TEXT("RPG_Punch_%s_Capture8634"),H);
        Test->TestEqual(Label+TEXT(": one punch released"),Animation->CapturedPunchReleaseCount,Releases+1);
        Test->TestEqual(Label+TEXT(": the filmed clip played"),Animation->LastCapturedPunch.ToString(),Body);
        Test->TestEqual(Label+(bFirst?TEXT(": first-person presentation"):TEXT(": body presentation")),Clip,
            bFirst?FString::Printf(TEXT("RPG_FP_Punch_%s_Capture8634"),H):Body);
        Test->TestEqual(Label+TEXT(": exactly one strike window"),Windows,1);
        Test->TestTrue(FString::Printf(TEXT("%s: window opens near impact (%.2f s after release)"),*Label,OpenedAt-ReleasedAt),OpenedAt-ReleasedAt>.05f && OpenedAt-ReleasedAt<.5f);
        // The first-person lead fist already rests far out in the camera guard, so it travels less.
        Test->TestTrue(FString::Printf(TEXT("%s: fist travels forward (%.1f cm)"),*Label,MaxReach),MaxReach>(bFirst?(bLeft?8.f:15.f):25.f));
        Test->TestFalse(Label+TEXT(": back in the fighting stance"),PlayingClip(Anim,TEXT("FightStance")).IsEmpty());
        Test->TestTrue(Label+TEXT(": punch finished"),PlayingClip(Anim,*Part).IsEmpty());
        Test->AddInfo(FString::Printf(TEXT("%s %s: window at %.3f s after release, reach %.1f cm"),bFirst?TEXT("FP"):TEXT("TP"),*Label,OpenedAt-ReleasedAt,MaxReach));
        if(++Case<4){Phase=0;Start=Now-1.2f;return false;}
        if(bVisual)FFileHelper::SaveStringToFile(Times,*(Dir/TEXT("captures.csv")));
        return true;
    }
private:
    FAutomationTestBase* Test;bool bFirst,bVisual;bool bReady=false;int32 Wait=0,Phase=0,Case=0,Releases=0,Windows=0,Shot=0;
    float Start=0.f,PhaseStart=0.f,ReleasedAt=0.f,OpenedAt=-1.f,MaxReach=0.f,LastShot=-1.f;bool bWasOpen=false;
    TOptional<FVector> Rest[2];FString Clip,Dir,Times;ACameraActor* Camera=nullptr;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFilmedPunchTest,"RPG.Combat.FilmedRightPunch",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FFilmedPunchTest::RunTest(const FString&)
{ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));ADD_LATENT_AUTOMATION_COMMAND(FFilmedPunchReview(this,false,false));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFilmedPunchFirst,"RPG.Combat.FilmedRightPunchFirstPerson",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FFilmedPunchFirst::RunTest(const FString&)
{ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));ADD_LATENT_AUTOMATION_COMMAND(FFilmedPunchReview(this,true,false));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFilmedPunchVisual,"Visual.FilmedRightPunch",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FFilmedPunchVisual::RunTest(const FString&)
{ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));ADD_LATENT_AUTOMATION_COMMAND(FFilmedPunchReview(this,false,true));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;}
#endif
