#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "RPGCameraModeComponent.h"
#include "RPGCharacterStatsComponent.h"
#include "RPGCombatStateComponent.h"
#include "RPGControllerInputComponent.h"
#include "RPGHitOutcomeComponent.h"
#include "RPGMeleeHitComponent.h"
#include "RPGNpcCharacter.h"
#include "RPGPowerArena.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "InputAction.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

namespace
{
// A second render view records the actual game pose without replacing the player's aiming camera.
// Fixed 30 Hz game time keeps PNG export stalls out of the animation timeline. Review-only, not performance data.
class FPunchMomentumReview : public IAutomationLatentCommand
{
public:
    FPunchMomentumReview(FAutomationTestBase* InTest,int32 InCase):Test(InTest),Index(InCase) {}
    ~FPunchMomentumReview() { if (bClockSaved) { FApp::SetUseFixedTimeStep(bSavedFixed); FApp::SetFixedDeltaTime(SavedDt); } }
    bool Update() override
    {
        UWorld* W=GEditor?GEditor->PlayWorld:nullptr;
        if (!W) return ++Wait>600?Fail(TEXT("no PIE world")):false;
        if(FixtureWorld.Get()!=W){FixtureWorld=W;Npc.Reset();Phase=0;Frame=0;Capture=nullptr;}
        if (!bClockSaved) { bClockSaved=true;bSavedFixed=FApp::UseFixedTimeStep();SavedDt=FApp::GetFixedDeltaTime();FApp::SetUseFixedTimeStep(true);FApp::SetFixedDeltaTime(1./30.); }
        ACharacter* P=UGameplayStatics::GetPlayerCharacter(W,0);
        auto* PC=P?Cast<APlayerController>(P->GetController()):nullptr;
        auto* Melee=P?P->FindComponentByClass<URPGMeleeHitComponent>():nullptr;
        if (!Npc.IsValid()) Npc=RPGPowerArena::FindReferenceNpc(W);
        if (!P || !PC || !PC->PlayerCameraManager || !Melee || !Npc.IsValid()) return ++Wait>600?Fail(TEXT("missing actors")):false;
        ARPGNpcCharacter* N=Npc.Get();const float Now=W->GetTimeSeconds();
        const bool bFirst=Index>=4,bLeft=Index%4>=2,bStrong=Index%2==1;
        const FString Name=FString::Printf(TEXT("%s_%s_strength%d"),bFirst?TEXT("FP"):TEXT("TP"),bLeft?TEXT("L"):TEXT("R"),bStrong?12:5);
        if (Phase==0)
        {
            if(Now<1.5f)return false;
            const FVector Base=N->GetActorLocation();
            N->GetCharacterMovement()->StopMovementImmediately();
            N->SetActorRotation(FRotator(0,180,0),ETeleportType::ResetPhysics);
            P->SetActorLocationAndRotation(Base-FVector(100,0,0),FRotator::ZeroRotator,false,nullptr,ETeleportType::ResetPhysics);
            P->GetCharacterMovement()->StopMovementImmediately();
            P->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            N->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            if(auto* Stats=URPGCharacterStatsComponent::FindStats(P))Stats->SetStatBaseByName(TEXT("Strength"),bStrong?12.f:5.f);
            N->HitOutcome->TestSeed=42;
            if(FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_MOMENTUM_FROM_STANCE"))==TEXT("1"))
                if(auto* S=P->FindComponentByClass<URPGCombatStateComponent>())
                {S->ReportSimpleAction(ERPGPlayerAction::UnarmedGuard,ERPGHand::None);S->ReportSimpleAction(ERPGPlayerAction::DefenseReleased,ERPGHand::None);}
            if(auto* Mode=P->FindComponentByClass<URPGCameraModeComponent>();Mode && Mode->bIsFirstPerson!=bFirst)Mode->TogglePerspective();
            Dir=FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_MOMENTUM_REVIEW_DIR"));
            if(Dir.IsEmpty())Dir=FPaths::ProjectSavedDir()/TEXT("PunchMomentumReview");
            Dir/=Name;IFileManager::Get().MakeDirectory(*Dir,true);
            Capture=W->SpawnActor<ASceneCapture2D>();
            auto* C=Capture->GetCaptureComponent2D();C->bCaptureEveryFrame=false;C->bCaptureOnMovement=false;
            C->CaptureSource=ESceneCaptureSource::SCS_FinalColorLDR;
            C->TextureTarget=UKismetRenderingLibrary::CreateRenderTarget2D(W,960,540,RTF_RGBA8,FLinearColor::Black);
            C->ShowFlags.SetMotionBlur(false);C->ShowFlags.SetTemporalAA(false);
            Rows=TEXT("frame,attack_time,hit_count,hit_bone,hit_x,hit_y,hit_z,reaction,damage,limb_speed,player_pelvis_x,player_pelvis_y,player_pelvis_z,player_chest_x,player_chest_y,player_chest_z,player_fist_x,player_fist_y,player_fist_z,npc_pelvis_x,npc_pelvis_y,npc_pelvis_z,npc_head_x,npc_head_y,npc_head_z,npc_peak_spin_deg_s\n");
            CountBefore=Melee->ConfirmedHitCount;Start=Now;Phase=1;return false;
        }
        if(Phase==1)
        {
            const FVector Target=N->GetMesh()->GetBoneLocation(bStrong?TEXT("head"):TEXT("spine_05"))+FVector(0,0,bStrong?-6:0);
            PC->SetControlRotation((Target-PC->PlayerCameraManager->GetCameraLocation()).Rotation());
            if(Now-Start<.8f)return false;
            Start=Now;Phase=2;
        }
        const float T=Now-Start;
        auto* Input=P->FindComponentByClass<URPGControllerInputComponent>();
        auto* Action=Input?FindObject<UInputAction>(Input,bLeft?TEXT("IA_Defense"):TEXT("IA_RightAttack")):nullptr;
        auto* Sub=ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
        if(!Action || !Sub)return Fail(TEXT("missing input action"));
        if(T<.08f)Sub->InjectInputVectorForAction(Action,FVector(1,0,0),{},{});
        auto* C=Capture->GetCaptureComponent2D();
        if(bFirst)
        {
            Capture->SetActorLocationAndRotation(PC->PlayerCameraManager->GetCameraLocation(),PC->PlayerCameraManager->GetCameraRotation());
            C->FOVAngle=PC->PlayerCameraManager->GetFOVAngle();
        }
        else
        {
            const FVector A=P->GetMesh()->GetBoneLocation(TEXT("pelvis")),B=N->GetMesh()->GetBoneLocation(TEXT("pelvis"));
            const FVector Look=(A+B)*.5f+FVector(0,0,15);
            const float Separation=float(FVector::Dist2D(A,B));
            // Supplemental stance views keep the attacker and a launched NPC in frame through recovery.
            const bool bWide=FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_MOMENTUM_WIDE_REVIEW"))==TEXT("1");
            const float Distance=bWide?FMath::Max(500.f,300.f+1.35f*Separation):460.f+Separation*.45f;
            const FVector Eye=Look+FVector(45,bLeft?-Distance:Distance,95);
            Capture->SetActorLocationAndRotation(Eye,(Look-Eye).Rotation());C->FOVAngle=45.f;
        }
        C->CaptureScene();
        UKismetRenderingLibrary::ExportRenderTarget(W,C->TextureTarget,Dir,FString::Printf(TEXT("frame_%04d.png"),Frame));
        const auto& H=Melee->LastHit;
        Rows+=FString::Printf(TEXT("%d,%.6f,%d,%s,%.4f,%.4f,%.4f,%s,%.4f,%.4f"),Frame++,T,Melee->ConfirmedHitCount,*H.HitBone.ToString(),H.ImpactLocation.X,H.ImpactLocation.Y,H.ImpactLocation.Z,
            *UEnum::GetDisplayValueAsText(N->LastPowerResult.Reaction).ToString(),N->LastPowerResult.Damage,H.LimbSpeed);
        auto AppendVec=[&](FVector V){Rows+=FString::Printf(TEXT(",%.4f,%.4f,%.4f"),V.X,V.Y,V.Z);};
        for(const TCHAR* Bone:{TEXT("pelvis"),TEXT("spine_05"),bLeft?TEXT("middle_01_l"):TEXT("middle_01_r")})AppendVec(P->GetMesh()->GetBoneLocation(Bone));
        AppendVec(N->GetMesh()->GetBoneLocation(TEXT("pelvis")));AppendVec(N->GetMesh()->GetBoneLocation(TEXT("head")));
        float Spin=0;
        for(const auto* Body:N->GetMesh()->Bodies)if(Body && Body->IsInstanceSimulatingPhysics())Spin=FMath::Max(Spin,FMath::RadiansToDegrees(float(Body->GetUnrealWorldAngularVelocityInRadians().Size())));
        Rows+=FString::Printf(TEXT(",%.4f\n"),Spin);
        if(T<1.f)return false;
        FFileHelper::SaveStringToFile(Rows,*(Dir/TEXT("frames.csv")));
        Test->TestEqual(Name+TEXT(" one actual input/contact confirmation"),Melee->ConfirmedHitCount,CountBefore+1);
        Test->AddInfo(FString::Printf(TEXT("MOMENTUMREVIEW %s frames=%d bone=%s dir=%s"),*Name,Frame,*H.HitBone.ToString(),*Dir));
        return true;
    }
private:
    bool Fail(const TCHAR* Why){Test->AddError(Why);return true;}
    FAutomationTestBase* Test;int32 Index,Wait=0,Phase=0,Frame=0,CountBefore=0;float Start=0;
    bool bClockSaved=false,bSavedFixed=false;double SavedDt=0;FString Dir,Rows;
    TWeakObjectPtr<UWorld> FixtureWorld;TWeakObjectPtr<ARPGNpcCharacter> Npc;ASceneCapture2D* Capture=nullptr;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPunchMomentumReviewTest,"Visual.PunchMomentumReview",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FPunchMomentumReviewTest::RunTest(const FString&)
{
    for(int32 Index=0;Index<8;++Index)
    {
        if(FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_MOMENTUM_TP_ONLY"))==TEXT("1") && Index>=4)continue;
        if(FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_MOMENTUM_REVIEW_LEFT_ONLY"))==TEXT("1") && Index%4<2)continue;
        ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.1f));
        ADD_LATENT_AUTOMATION_COMMAND(FPunchMomentumReview(this,Index));
        ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    }
    return true;
}
#endif
