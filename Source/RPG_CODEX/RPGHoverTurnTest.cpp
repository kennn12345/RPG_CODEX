// Flight-to-hover handoff and view-facing turns in a disposable PIE world.
#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "RPGAnimInstance.h"
#include "RPGDirectionalHoverComponent.h"
#include "RPGMovementAbilityComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMeshActor.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Editor.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealClient.h"

namespace
{
class FHoverTurnReview : public IAutomationLatentCommand
{
public:
    FHoverTurnReview(FAutomationTestBase* In,bool Visual):Test(In),bVisual(Visual){}
    bool Update() override
    {
        UWorld* W=GEditor?GEditor->PlayWorld:nullptr;
        auto* C=W?UGameplayStatics::GetPlayerCharacter(W,0):nullptr;
        auto* PC=C?Cast<APlayerController>(C->GetController()):nullptr;
        auto* H=C?C->FindComponentByClass<URPGDirectionalHoverComponent>():nullptr;
        auto* A=C?C->FindComponentByClass<URPGMovementAbilityComponent>():nullptr;
        auto* Anim=C?Cast<URPGAnimInstance>(C->GetMesh()->GetAnimInstance()):nullptr;
        if(!PC || !H || !A || !Anim){if(++Wait<1200)return false;Test->AddError(TEXT("Hover turn setup missing"));return true;}
        auto* Mesh=C->GetMesh();auto* M=C->GetCharacterMovement();
        if(Start<0.f)
        {
            if(++Warmup<90)return false;Start=W->GetTimeSeconds();Home=C->GetActorLocation();
            auto* Floor=W->SpawnActor<AStaticMeshActor>();Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
            Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
            Floor->SetActorScale3D(FVector(200,200,.1));Floor->SetActorLocation(Home-FVector(0,0,C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+5));
            Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
            C->SetActorLocation(Home+FVector(0,0,400));C->SetActorRotation(FRotator::ZeroRotator);M->StopMovementImmediately();
            Mesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            Input=PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
            MoveAction=LoadObject<UInputAction>(nullptr,TEXT("/Game/Input/IA_Move.IA_Move"));
            Camera=W->SpawnActor<ACameraActor>();Camera->GetCameraComponent()->FieldOfView=55.f;PC->SetViewTarget(Camera.Get());
            PC->SetControlRotation(FRotator::ZeroRotator);A->StartFlight();
            Dir=FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_HOVER_REVIEW_DIR"));if(Dir.IsEmpty())Dir=FPaths::ProjectSavedDir()/TEXT("HoverTurnReview/Default");
            IFileManager::Get().MakeDirectory(*Dir,true);
            CSV=TEXT("t,control_yaw,actor_yaw,body_yaw,body_rate,speed,hover,pose_weight,turn,world_x,world_y,world_z\n");return false;
        }
        const float T=W->GetTimeSeconds()-Start,Dt=W->GetDeltaSeconds();
        const FVector2D Axis=T>=1.4f && T<3.4f?FVector2D(0,1):T>=3.7f && T<3.95f?FVector2D(-.3,0):FVector2D::ZeroVector;
        Input->InjectInputForAction(MoveAction.Get(),FInputActionValue(Axis),{},{});
        const float Yaw=T<5.4f?0.f:T<8.4f?90.f:T<10.4f?170.f:T<12.4f?-170.f:T<15.4f?-60.f:-150.f;
        PC->SetControlRotation(FRotator(0,Yaw,0));
        Camera->SetActorLocation(C->GetActorLocation()+FVector(-350,-450,110));
        Camera->SetActorRotation((C->GetActorLocation()+FVector(0,0,-10)-Camera->GetActorLocation()).Rotation());
        const FVector Right=(Mesh->GetBoneLocation(TEXT("upperarm_r"))-Mesh->GetBoneLocation(TEXT("upperarm_l"))).GetSafeNormal2D();
        const float BodyYaw=FVector::CrossProduct(Right,FVector::UpVector).Rotation().Yaw;
        const float Rate=bHaveYaw?FMath::Abs(FMath::FindDeltaAngleDegrees(LastYaw,BodyYaw))/FMath::Max(Dt,.001f):0.f;
        LastYaw=BodyYaw;bHaveYaw=true;
        if(T>2.6f && T<3.2f)SawFlight|=M->Velocity.Size()>650.f && Anim->FlightBlend.X>.9f;
        if(T>3.2f && T<5.1f)MaxHandoffRate=FMath::Max(MaxHandoffRate,Rate);
        if(T>5.4f)
        {
            MaxTurnRate=FMath::Max(MaxTurnRate,Rate);
            MaxTurnPose=FMath::Max(MaxTurnPose,Anim->DirectionalHover.Turn);MinTurnPose=FMath::Min(MinTurnPose,Anim->DirectionalHover.Turn);
            if(!bHaveHoverHome){HoverHome=C->GetActorLocation();bHaveHoverHome=true;}
            MaxDrift=FMath::Max(MaxDrift,float(FVector::Distance(C->GetActorLocation(),HoverHome)));
        }
        if((T>7.8f && T<8.3f)||(T>13.9f && T<15.3f)||(T>17.4f && T<18.3f))
            MaxSettledError=FMath::Max(MaxSettledError,FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw,BodyYaw)));
        if(T>=NextSample)
        {
            CSV+=FString::Printf(TEXT("%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%.3f,%.3f,%.3f,%.3f,%.3f\n"),T,Yaw,C->GetActorRotation().Yaw,BodyYaw,Rate,M->Velocity.Size(),H->IsHovering(),Anim->DirectionalHover.Weight,Anim->DirectionalHover.Turn,C->GetActorLocation().X,C->GetActorLocation().Y,C->GetActorLocation().Z);
            if(bVisual && T<18.4f)FScreenshotRequest::RequestScreenshot(Dir/FString::Printf(TEXT("hoverturn_%05d.png"),Shot++),false,false);
            NextSample+=1.f/30.f;
        }
        if(T<18.6f)return false;
        Test->TestTrue(TEXT("Travel flight returns to a stationary hover"),SawFlight && H->IsHovering() && Anim->DirectionalHover.Weight>.99f);
        Test->TestTrue(TEXT("Stationary turns hold position"),MaxDrift<.5f);
        Test->TestTrue(TEXT("Flight-to-hover handoff avoids a direction snap"),MaxHandoffRate<420.f);
        Test->TestTrue(TEXT("Hover turns ease through left right and the yaw seam"),MaxTurnRate<240.f && MaxSettledError<3.f);
        Test->TestTrue(TEXT("Hover turns animate in both directions"),MaxTurnPose>.2f && MinTurnPose<-.2f);
        FString Report=FString::Printf(TEXT("Hover handoff peakRate=%.3f turnPeakRate=%.3f settledError=%.3f drift=%.3f turnPose=[%.3f,%.3f] flight=%d\n"),MaxHandoffRate,MaxTurnRate,MaxSettledError,MaxDrift,MinTurnPose,MaxTurnPose,SawFlight);
        FFileHelper::SaveStringToFile(Report,*(Dir/TEXT("result.txt")));FFileHelper::SaveStringToFile(CSV,*(Dir/TEXT("samples.csv")));Test->AddInfo(Report);return true;
    }
private:
    FAutomationTestBase* Test;bool bVisual,bHaveYaw=false,bHaveHoverHome=false,SawFlight=false;
    int32 Wait=0,Warmup=0,Shot=0;float Start=-1,NextSample=0,LastYaw=0,MaxHandoffRate=0,MaxTurnRate=0,MaxSettledError=0,MaxDrift=0,MaxTurnPose=0,MinTurnPose=0;
    FVector Home,HoverHome;FString Dir,CSV;TWeakObjectPtr<ACameraActor> Camera;
    TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> Input;TWeakObjectPtr<UInputAction> MoveAction;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoverTurnTest,"RPG.Hover.Handoff",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHoverTurnTest::RunTest(const FString&){ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FHoverTurnReview(this,false));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoverTurnVisual,"Visual.HoverHandoff",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHoverTurnVisual::RunTest(const FString&)
{
    auto* Play=NewObject<ULevelEditorPlaySettings>();Play->NewWindowWidth=1280;Play->NewWindowHeight=752;Play->LastExecutedPlayModeType=PlayMode_InEditorFloating;
    FRequestPlaySessionParams Session;Session.WorldType=EPlaySessionWorldType::PlayInEditor;Session.EditorPlaySettings=Play;GEditor->RequestPlaySession(Session);
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));ADD_LATENT_AUTOMATION_COMMAND(FHoverTurnReview(this,true));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
