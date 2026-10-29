#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "RPGCameraModeComponent.h"
#include "RPGCombatStateComponent.h"
#include "RPGControllerInputComponent.h"
#include "RPGPrototypeAnimationComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealClient.h"

namespace
{
bool StancePlaying(UAnimInstance* Anim)
{
    for(const FAnimMontageInstance* Playing:Anim->MontageInstances)
        if(Playing && Playing->Montage && !Playing->IsStopped() && Playing->GetWeight()>.5f)
            for(const auto& Track:Playing->Montage->SlotAnimTracks)
                for(const auto& Segment:Track.AnimTrack.AnimSegments)
                    if(Segment.GetAnimReference() && Segment.GetAnimReference()->GetName().Contains(TEXT("FightStance")))return true;
    return false;
}

/**
 * D-Pad Left (IA_DrawSheathe) toggles the fighting stance, unarmed and armed: on, it stays up past the stance timeout;
 * off, it drops at once. A punch with the stance down still raises it only for the timeout. Visual: a screenshot from
 * the player's camera with the stance down and up.
 */
class FStanceToggleReview:public IAutomationLatentCommand
{
public:
    FStanceToggleReview(FAutomationTestBase* InTest,bool InFirst,bool InVisual):Test(InTest),bFirst(InFirst),bVisual(InVisual){}
    bool Update() override
    {
        UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
        ACharacter* Player=World?UGameplayStatics::GetPlayerCharacter(World,0):nullptr;
        APlayerController* Controller=Player?Cast<APlayerController>(Player->GetController()):nullptr;
        URPGPrototypeAnimationComponent* Animation=Player?Player->FindComponentByClass<URPGPrototypeAnimationComponent>():nullptr;
        URPGCombatStateComponent* State=Player?Player->FindComponentByClass<URPGCombatStateComponent>():nullptr;
        URPGControllerInputComponent* Controls=Player?Player->FindComponentByClass<URPGControllerInputComponent>():nullptr;
        UAnimInstance* Anim=Player?Player->GetMesh()->GetAnimInstance():nullptr;
        UEnhancedInputLocalPlayerSubsystem* Input=Controller && Controller->GetLocalPlayer()?Controller->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>():nullptr;
        UInputAction* DPadLeft=Controls?FindObject<UInputAction>(Controls,TEXT("IA_DrawSheathe")):nullptr;
        if(!Player || !Animation || !State || !Anim || !Input || !DPadLeft)
        {if(++Wait<600)return false;Test->AddError(TEXT("Stance toggle: player, input or IA_DrawSheathe unavailable"));return true;}
        const float Now=World->GetTimeSeconds();
        if(!bReady)
        {
            bReady=true;Start=Now;
            Player->GetCharacterMovement()->StopMovementImmediately();
            Player->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            if(URPGCameraModeComponent* Mode=Player->FindComponentByClass<URPGCameraModeComponent>();Mode && Mode->bIsFirstPerson!=bFirst)Mode->TogglePerspective();
            SavedTimeout=Animation->CombatStanceTimeout;Animation->CombatStanceTimeout=2.f;
            Dir=FPaths::ProjectSavedDir()/TEXT("StanceToggle")/(bFirst?TEXT("FirstPerson"):TEXT("ThirdPerson"));
            if(bVisual){IFileManager::Get().DeleteDirectory(*Dir,false,true);IFileManager::Get().MakeDirectory(*Dir,true);}
            return false;
        }
        const float T=Now-Start;
        const TCHAR* View=bFirst?TEXT("FP"):TEXT("TP");
        auto Press=[&]{Input->InjectInputForAction(DPadLeft,FInputActionValue(true),{},{});};
        auto Check=[&](const TCHAR* What,bool bWantStance)
        {
            Test->TestEqual(FString::Printf(TEXT("%s %s: in combat"),View,What),Animation->IsInCombat(),bWantStance);
            Test->TestEqual(FString::Printf(TEXT("%s %s: fighting stance playing"),View,What),StancePlaying(Anim),bWantStance);
        };
        auto Shot=[&](const TCHAR* Name){if(bVisual)FScreenshotRequest::RequestScreenshot(Dir/FString::Printf(TEXT("%s.png"),Name),true,false);};
        // Each step runs once when its time comes.
        const float Steps[]={1.f,1.1f,2.5f,5.5f,5.6f,7.f,7.1f,8.5f,12.f,12.1f,13.5f,13.6f,15.f};
        if(Step>=UE_ARRAY_COUNT(Steps)){Animation->CombatStanceTimeout=SavedTimeout;State->SetEquippedHandTypes(ERPGHandItemType::Empty,ERPGHandItemType::Empty);return true;}
        if(T<Steps[Step])return false;
        switch(Step++)
        {
        case 0: Check(TEXT("start"),false);Shot(TEXT("1_relaxed"));Test->TestFalse(TEXT("Weapons start put away"),State->bWeaponsDrawn);break;
        case 1: Press();break;
        case 2: Check(TEXT("D-Pad Left on"),true);Test->TestTrue(TEXT("D-Pad Left draws"),State->bWeaponsDrawn);Shot(TEXT("2_stance_on"));break;
        case 3: Check(TEXT("held past the 2 s timeout"),true);break;
        case 4: Press();break;
        case 5: Check(TEXT("D-Pad Left off"),false);Test->TestFalse(TEXT("D-Pad Left puts away"),State->bWeaponsDrawn);Shot(TEXT("3_stance_off"));break;
        case 6: State->ReportSimpleAction(ERPGPlayerAction::AttackChargeStarted,ERPGHand::Right);State->ReportSimpleAction(ERPGPlayerAction::Attack,ERPGHand::Right);break;
        case 7: Check(TEXT("punch with the stance down raises it"),true);break;
        case 8: Check(TEXT("punch's stance times out"),false);State->SetEquippedHandTypes(ERPGHandItemType::OneHandedWeapon,ERPGHandItemType::Empty);break;
        case 9: Press();break;
        case 10: Check(TEXT("armed, D-Pad Left on"),true);break;
        case 11: Press();break;
        case 12: Check(TEXT("armed, D-Pad Left off"),false);break;
        }
        return false;
    }
private:
    FAutomationTestBase* Test;bool bFirst,bVisual;bool bReady=false;int32 Wait=0,Step=0;float Start=0.f,SavedTimeout=8.f;FString Dir;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStanceToggleTest,"RPG.Combat.StanceToggle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FStanceToggleTest::RunTest(const FString&)
{ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));ADD_LATENT_AUTOMATION_COMMAND(FStanceToggleReview(this,false,false));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStanceToggleFirst,"RPG.Combat.StanceToggleFirstPerson",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FStanceToggleFirst::RunTest(const FString&)
{ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));ADD_LATENT_AUTOMATION_COMMAND(FStanceToggleReview(this,true,false));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStanceToggleVisual,"Visual.StanceToggle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FStanceToggleVisual::RunTest(const FString&)
{ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));ADD_LATENT_AUTOMATION_COMMAND(FStanceToggleReview(this,false,true));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;}
#endif
