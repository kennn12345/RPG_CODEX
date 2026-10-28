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
 * The right hand's filmed punch (IMG_8634): a tap, a held press released as a Heavy attack, and the left hand (no clip
 * yet, so it must only hold the stance). Checks the actual clip, one strike window per punch, real fist travel and the
 * return to the fighting stance.
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
                ACameraActor* Camera=World->SpawnActor<ACameraActor>();Camera->GetCameraComponent()->SetFieldOfView(40.f);
                Camera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount=true;Camera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount=0.f;
                // Side-on from the punching (right) side at chest height, framing the whole body and the full reach.
                const FVector Look=Player->GetActorLocation()+Player->GetActorRotation().RotateVector(FVector(30,0,20));
                const FVector Eye=Look+Player->GetActorRotation().RotateVector(FVector(60,430,15));
                Camera->SetActorLocationAndRotation(Eye,(Look-Eye).Rotation());Controller->bAutoManageActiveCameraTarget=false;Controller->SetViewTarget(Camera);
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
        const FTransform Actor=Player->GetActorTransform();
        const FVector Fist=Actor.InverseTransformPosition(Player->GetMesh()->GetSocketLocation(TEXT("hand_r")));
        if(Phase==0)
        {
            if(T<1.2f)return false;
            if(!Rest.IsSet())Rest=Fist;
            Test->TestFalse(TEXT("Stance plays before the punch"),PlayingClip(Anim,TEXT("FightStance")).IsEmpty());
            Releases=Animation->CapturedPunchReleaseCount;Windows=0;MaxReach=0.f;bWasOpen=false;Clip.Reset();
            const ERPGHand Hand=Case==2?ERPGHand::Left:ERPGHand::Right;
            State->ReportSimpleAction(ERPGPlayerAction::AttackChargeStarted,Hand);
            Phase=1;PhaseStart=Now;return false;
        }
        const float P=Now-PhaseStart;
        const FString Seen=PlayingClip(Anim,TEXT("Punch_R_Capture8634"));if(!Seen.IsEmpty())Clip=Seen;
        const bool bOpen=State->bStrikeHitWindowOpen && State->StrikeLimb==ERPGStrikeLimb::RightHand;
        if(bOpen && !bWasOpen){++Windows;OpenedAt=P;}
        bWasOpen=bOpen;
        MaxReach=FMath::Max(MaxReach,Fist.X-Rest.GetValue().X);
        if(Phase==1)
        {
            // Case 0 taps; case 1 holds at the load pose, then releases as a Heavy attack; case 2 is the left hand.
            const float Hold=Case==1?.9f:.05f;
            if(P<Hold)return false;
            if(Case==1)
            {
                Test->TestEqual(TEXT("Held press waits at the load pose (no strike yet)"),Animation->CapturedPunchReleaseCount,Releases);
                Test->TestEqual(TEXT("No strike window while held"),Windows,0);
            }
            ReleasedAt=P;
            State->ReportSimpleAction(Case==1?ERPGPlayerAction::HeavyAttack:ERPGPlayerAction::Attack,Case==2?ERPGHand::Left:ERPGHand::Right,Case==1?.6f:0.f);
            Phase=2;return false;
        }
        if(P<ReleasedAt+1.6f)return false;
        const TCHAR* Label[]={TEXT("tap"),TEXT("held/Heavy"),TEXT("left hand")};
        if(Case<2)
        {
            Test->TestEqual(FString::Printf(TEXT("%s: one punch released"),Label[Case]),Animation->CapturedPunchReleaseCount,Releases+1);
            Test->TestEqual(FString::Printf(TEXT("%s: the filmed clip played"),Label[Case]),Animation->LastCapturedPunch.ToString(),FString(TEXT("RPG_Punch_R_Capture8634")));
            Test->TestEqual(FString::Printf(TEXT("%s: %s presentation"),Label[Case],bFirst?TEXT("first-person"):TEXT("body")),Clip,
                FString(bFirst?TEXT("RPG_FP_Punch_R_Capture8634"):TEXT("RPG_Punch_R_Capture8634")));
            Test->TestEqual(FString::Printf(TEXT("%s: exactly one strike window"),Label[Case]),Windows,1);
            Test->TestTrue(FString::Printf(TEXT("%s: window opens near impact (%.2f s after release)"),Label[Case],OpenedAt-ReleasedAt),OpenedAt-ReleasedAt>.05f && OpenedAt-ReleasedAt<.5f);
            Test->TestTrue(FString::Printf(TEXT("%s: fist travels forward (%.1f cm)"),Label[Case],MaxReach),MaxReach>(bFirst?15.f:25.f));
        }
        else
        {
            Test->TestEqual(TEXT("Left hand: no punch yet"),Animation->CapturedPunchReleaseCount,Releases);
            Test->TestEqual(TEXT("Left hand: no strike window"),Windows,0);
        }
        Test->TestFalse(FString::Printf(TEXT("%s: back in the fighting stance"),Label[Case]),PlayingClip(Anim,TEXT("FightStance")).IsEmpty());
        Test->TestTrue(FString::Printf(TEXT("%s: punch finished"),Label[Case]),PlayingClip(Anim,TEXT("Punch_R_Capture8634")).IsEmpty());
        Test->AddInfo(FString::Printf(TEXT("%s %s: window at %.3f s after release, reach %.1f cm"),bFirst?TEXT("FP"):TEXT("TP"),Label[Case],OpenedAt-ReleasedAt,MaxReach));
        if(++Case<3){Phase=0;Start=Now-1.2f;return false;}
        if(bVisual)FFileHelper::SaveStringToFile(Times,*(Dir/TEXT("captures.csv")));
        return true;
    }
private:
    FAutomationTestBase* Test;bool bFirst,bVisual;bool bReady=false;int32 Wait=0,Phase=0,Case=0,Releases=0,Windows=0,Shot=0;
    float Start=0.f,PhaseStart=0.f,ReleasedAt=0.f,OpenedAt=-1.f,MaxReach=0.f,LastShot=-1.f;bool bWasOpen=false;
    TOptional<FVector> Rest;FString Clip,Dir,Times;
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
