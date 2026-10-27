// Read-only visual audit of candidate source animations; every actor exists only in PIE.
#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Animation/AnimSequence.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/SkeletalMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UnrealClient.h"

namespace
{
class FReferenceSourceAudit:public IAutomationLatentCommand
{
public:
    explicit FReferenceSourceAudit(FAutomationTestBase* In):Test(In){}
    bool Update() override
    {
        UWorld* W=GEditor?GEditor->PlayWorld:nullptr;
        auto* PC=W?UGameplayStatics::GetPlayerController(W,0):nullptr;
        if(!PC){if(++Wait<600)return false;Test->AddError(TEXT("Reference audit has no player controller"));return true;}
        if(!Body)
        {
            Origin=FVector(50000,50000,0);
            auto* Floor=W->SpawnActor<AStaticMeshActor>(Origin-FVector(0,0,5),FRotator::ZeroRotator);
            Floor->SetMobility(EComponentMobility::Movable);Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
            Floor->SetActorScale3D(FVector(30,30,.1));Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
            Body=W->SpawnActor<ASkeletalMeshActor>(Origin,FRotator::ZeroRotator);
            bTarget=!FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_REFERENCE_AUDIT_TARGET")).IsEmpty();
            auto* Mesh=LoadObject<USkeletalMesh>(nullptr,bTarget?
                TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin"):
                TEXT("/Game/RamsterZ_FreeAnims_Volume1/Demo/Mannequin/Character/Mesh/SK_Mannequin.SK_Mannequin"));
            if(!Mesh){Test->AddError(TEXT("Native source mesh unavailable"));return true;}
            Body->GetSkeletalMeshComponent()->SetSkeletalMeshAsset(Mesh);
            Body->GetSkeletalMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Body->GetSkeletalMeshComponent()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            Camera=W->SpawnActor<ACameraActor>();Camera->GetCameraComponent()->SetFieldOfView(45);
            Camera->GetCameraComponent()->bConstrainAspectRatio=false;
            Camera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount=true;Camera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount=0;
            PC->bAutoManageActiveCameraTarget=false;PC->SetViewTarget(Camera);
            Dir=FPaths::ProjectSavedDir()/(bTarget?TEXT("ReferenceCombat1027/SourceAuditUEFN"):TEXT("ReferenceCombat1027/SourceAuditNative"));IFileManager::Get().MakeDirectory(*Dir,true);
            CSV=TEXT("frame,clip,view,time\n");
            return false;
        }
        // Hold this exact pose until its screenshot is rendered and saved; fixed tick counts can drop frames offscreen.
        if(!PendingFile.IsEmpty())
        {
            if(FScreenshotRequest::IsScreenshotRequested() || !IFileManager::Get().FileExists(*PendingFile))return false;
            PendingFile.Empty();
        }
        const TCHAR* Names[]={TEXT("H2H_Idle"),TEXT("H2H_PunchCombo01"),TEXT("H2H_PunchCombo02"),TEXT("H2H_PunchCombo03")};
        if(ClipIndex==4)
        {
            FFileHelper::SaveStringToFile(CSV,*(Dir/TEXT("captures.csv")));
            Test->AddInfo(TEXT("Native UE4 stance and three punch combos audited in two reference views; no assets or levels saved"));return true;
        }
        auto* Mesh=Body->GetSkeletalMeshComponent();
        if(Stage==0)
        {
            const FString Name=FString(Names[ClipIndex])+(bTarget?TEXT("_UEFNReference1027"):TEXT(""));
            const FString Path=(bTarget?TEXT("/Game/RPG/Animations/Retarget/Reference1027Work/"):TEXT("/Game/RamsterZ_FreeAnims_Volume1/AnimationSequence/H2H/"))+Name+TEXT(".")+Name;
            auto* Clip=LoadObject<UAnimSequence>(nullptr,*Path);
            if(!Clip || Clip->GetSkeleton()!=Mesh->GetSkeletalMeshAsset()->GetSkeleton()){Test->AddError(TEXT("Source animation/skeleton mismatch"));return true;}
            Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);Mesh->PlayAnimation(Clip,false);Mesh->SetPlayRate(0);
            const float Time=ClipIndex==0?Shot*.5f:Shot/30.f;
            Mesh->SetPosition(Time,false);Mesh->TickAnimation(0,false);Mesh->RefreshBoneTransforms();
            const FVector Root=Mesh->GetBoneLocation(TEXT("root"));
            const FVector Focus(Root.X,Root.Y,Origin.Z+85);
            const FVector Eye=Focus+(View==0?FVector(-260,-360,155):FVector(260,-360,155));
            Camera->SetActorLocationAndRotation(Eye,(Focus-Eye).Rotation());
            Stage=1;return false;
        }
        if(Stage++<3)return false;
        const float Time=ClipIndex==0?Shot*.5f:Shot/30.f;
        const FString File=FString::Printf(TEXT("frame_%05d.png"),Frame);
        PendingFile=Dir/File;
        FScreenshotRequest::RequestScreenshot(PendingFile,false,false);
        CSV+=FString::Printf(TEXT("%d,%s,%d,%.6f\n"),Frame++,Names[ClipIndex],View,Time);
        Stage=0;
        if(++Shot>=(ClipIndex==0?6:36))
        {Shot=0;if(++View==2){View=0;++ClipIndex;}}
        return false;
    }
private:
    FAutomationTestBase* Test;TObjectPtr<ASkeletalMeshActor> Body;TObjectPtr<ACameraActor> Camera;
    FVector Origin;int32 Wait=0,ClipIndex=0,View=0,Shot=0,Stage=0,Frame=0;FString Dir,CSV,PendingFile;bool bTarget=false;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReferenceAuditTest,"Visual.ReferenceCombat1027Audit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FReferenceAuditTest::RunTest(const FString&)
{
    auto* Play=NewObject<ULevelEditorPlaySettings>();Play->NewWindowWidth=1280;Play->NewWindowHeight=720;
    Play->CenterNewWindow=false;Play->LastExecutedPlayModeType=PlayMode_InEditorFloating;
    FRequestPlaySessionParams Session;Session.WorldType=EPlaySessionWorldType::PlayInEditor;Session.EditorPlaySettings=Play;
    GEditor->RequestPlaySession(Session);
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));ADD_LATENT_AUTOMATION_COMMAND(FReferenceSourceAudit(this));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
