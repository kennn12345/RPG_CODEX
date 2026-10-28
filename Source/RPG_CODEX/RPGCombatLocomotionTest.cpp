#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "RPGPrototypeAnimationComponent.h"
#include "RPGControllerInputComponent.h"
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
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
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
#include "UObject/UnrealType.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealClient.h"

namespace
{
FString LocomotionSelection(UAnimInstance* Anim)
{
    for(TFieldIterator<FStructProperty> It(Anim->GetClass());It;++It)
    {
        if(It->Struct->GetName()!=TEXT("AnimNode_MotionMatching"))continue;
        const auto* State=FindFProperty<FStructProperty>(It->Struct,TEXT("MotionMatchingState"));
        const auto* Result=State?FindFProperty<FStructProperty>(State->Struct,TEXT("SearchResult")):nullptr;
        const auto* Asset=Result?FindFProperty<FObjectPropertyBase>(Result->Struct,TEXT("SelectedAnim")):nullptr;
        if(!Asset)continue;
        const void* Node=It->ContainerPtrToValuePtr<void>(Anim);
        const void* S=State->ContainerPtrToValuePtr<void>(Node);
        const void* R=Result->ContainerPtrToValuePtr<void>(S);
        if(const UObject* Selected=Asset->GetObjectPropertyValue_InContainer(R))return Selected->GetName();
    }
    return TEXT("None");
}
bool PlayingStance(UAnimInstance* Anim)
{
    for(const FAnimMontageInstance* Playing:Anim->MontageInstances)
        if(Playing && Playing->Montage && !Playing->IsStopped() && Playing->GetWeight()>.01f)
            for(const auto& Track:Playing->Montage->SlotAnimTracks)
                for(const auto& Segment:Track.AnimTrack.AnimSegments)
                    if(Segment.GetAnimReference() && (Segment.GetAnimReference()->GetName().Contains(TEXT("CombatStance_Capture8619")) || Segment.GetAnimReference()->GetName().Contains(TEXT("CombatStance_Reference1027"))))return true;
    return false;
}
class FCombatLocomotionReview:public IAutomationLatentCommand
{
public:
    FCombatLocomotionReview(FAutomationTestBase* In,bool Visual,bool First=false):Test(In),bVisual(Visual),bFirst(First){}
    bool Update() override
    {
        UWorld* W=GEditor?GEditor->PlayWorld:nullptr;
        ACharacter* C=W?UGameplayStatics::GetPlayerCharacter(W,0):nullptr;
        auto* PC=C?Cast<APlayerController>(C->GetController()):nullptr;
        auto* Input=PC?PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>():nullptr;
        auto* Controls=C?C->FindComponentByClass<URPGControllerInputComponent>():nullptr;
        auto* Animation=C?C->FindComponentByClass<URPGPrototypeAnimationComponent>():nullptr;
        if(!C || !PC || !Input || !Controls || !Animation)
        {if(++Wait<600)return false;Test->AddError(TEXT("Locomotion review player unavailable"));return true;}
        const float Now=W->GetTimeSeconds();
        auto* Mesh=C->GetMesh();auto* Anim=Mesh->GetAnimInstance();
        if(Start<0.f)
        {
            auto* Floor=W->SpawnActor<AStaticMeshActor>(FVector(20000,20000,-25),FRotator::ZeroRotator);
            Floor->SetMobility(EComponentMobility::Movable);Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
            Floor->SetActorScale3D(FVector(300,100,.5));Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
            C->SetActorLocationAndRotation(FVector(20000,20000,C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2),FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
            C->GetCharacterMovement()->StopMovementImmediately();PC->SetControlRotation(FRotator::ZeroRotator);
            Mesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            Animation->CombatStanceTimeout=40.f;Start=Now;
            if(auto* Mode=C->FindComponentByClass<URPGCameraModeComponent>();Mode && Mode->bIsFirstPerson!=bFirst)Mode->TogglePerspective();
            Move=LoadObject<UInputAction>(nullptr,TEXT("/Game/Input/IA_Move.IA_Move"));
            Dir=FPaths::ProjectSavedDir()/TEXT("CombatLocomotion/Visual");
            if(bVisual)
            {
                IFileManager::Get().MakeDirectory(*Dir,true);Camera=W->SpawnActor<ACameraActor>();
                Camera->GetCameraComponent()->SetFieldOfView(48);Camera->GetCameraComponent()->bConstrainAspectRatio=false;
                Camera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount=true;Camera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount=0;
                PC->bAutoManageActiveCameraTarget=false;PC->SetViewTarget(Camera);
                if(auto* Aim=C->FindComponentByClass<URPGCombatAimComponent>())Aim->SetComponentTickEnabled(false);
            }
            return false;
        }
        const float T=Now-Start;
        const float Amount=T<.5f?0:T<3.f?.35f:T<6.f?1:T<8.f?0:T<10.f?.35f:T<12.f?1:T<16.3f?0:T<18.f?1:0;
        Input->InjectInputForAction(Move,FInputActionValue(FVector2D(0,Amount)),{},{});
        auto Button=[&](const TCHAR* Name,bool Down){if(auto* A=FindObject<UInputAction>(Controls,Name))
        {if(Down)Input->StartContinuousInputInjectionForAction(A,FInputActionValue(1.f),{},{});else Input->StopContinuousInputInjectionForAction(A);}};
        auto Event=[&](int Bit,float At,auto Fn){if(T>=At && !(Events&(1u<<Bit))){Events|=1u<<Bit;Fn();}};
        Event(0,6.3f,[&]{Button(TEXT("IA_RightAttack"),true);Button(TEXT("IA_Defense"),true);});
        Event(1,6.42f,[&]{Button(TEXT("IA_RightAttack"),false);Button(TEXT("IA_Defense"),false);});
        Event(2,13.5f,[&]{Button(TEXT("IA_RightAttack"),true);});Event(3,13.58f,[&]{Button(TEXT("IA_RightAttack"),false);});
        Event(4,14.9f,[&]{Button(TEXT("IA_Defense"),true);});Event(5,14.98f,[&]{Button(TEXT("IA_Defense"),false);});
        Event(6,16.5f,[&]{Button(TEXT("IA_RightAttack"),true);});Event(7,16.58f,[&]{Button(TEXT("IA_RightAttack"),false);});
        const FString Selected=LocomotionSelection(Anim);const bool Stance=PlayingStance(Anim);const float Speed=C->GetVelocity().Size2D();
        if(T>1.5f && T<2.8f){Walk|=Selected.Contains(TEXT("_Walk_"));WalkSpeed=FMath::Max(WalkSpeed,Speed);}
        if(T>4.f && T<5.8f){Run|=Selected.Contains(TEXT("_Run_"));RunSpeed=FMath::Max(RunSpeed,Speed);}
        if(T>7.3f && T<7.9f)Stationary|=Stance;
        if(T>8.7f && T<9.8f){WalkAfterCombat|=Selected.Contains(TEXT("_Walk_"));MovingStance|=Stance;}
        if(T>10.7f && T<11.8f)MovingStance|=Stance;
        if(T>12.9f && T<13.4f)Returned|=Stance;
        const FVector Hand=Mesh->GetSocketTransform(TEXT("hand_r"),RTS_Component).GetLocation();
        if(bHaveHand && ((T>8.f && T<8.7f)||(T>12.f && T<13.3f)))MaxArmStep=FMath::Max(MaxArmStep,float(FVector::Dist(Hand,LastHand)));
        LastHand=Hand;bHaveHand=true;
        if(bFirst)
        {
            const auto* View=C->FindComponentByClass<UCameraComponent>();
            const float Eye=View?C->GetActorTransform().InverseTransformPosition(View->GetComponentLocation()).Z:0.f;
            if(T>1.5f && T<2.8f)WalkEye=Eye;
            if(T>7.3f && T<7.9f)CombatEye=Eye;
            if(T>10.7f && T<11.8f)RunEye=Eye;
            if(bHaveEye && ((T>8.f && T<8.7f)||(T>12.f && T<13.3f)))MaxEyeStep=FMath::Max(MaxEyeStep,FMath::Abs(Eye-LastEye));
            LastEye=Eye;bHaveEye=true;
        }
        CSV+=FString::Printf(TEXT("%.5f,%.3f,%.2f,%d,%s,%.2f,%.2f,%.2f\n"),T,Amount,Speed,Stance,*Selected,Hand.X,Hand.Y,Hand.Z);
        if(bVisual)
        {
            const FVector Focus=C->GetActorLocation()+FVector(0,0,-10);const FVector Eye=Focus+FVector(420,350,95);
            Camera->SetActorLocationAndRotation(Eye,(Focus-Eye).Rotation());
            if(Now-LastShot>=1.f/30.f){Times+=FString::Printf(TEXT("%d,%.6f\n"),Shot,T);FScreenshotRequest::RequestScreenshot(Dir/FString::Printf(TEXT("frame_%05d.png"),Shot++),false,false);LastShot=Now;}
        }
        if(T<19.5f)return false;
        Test->TestTrue(TEXT("Partial stick selects actual GASP walking animation"),Walk);
        Test->TestTrue(TEXT("Full stick selects actual GASP running animation"),Run);
        Test->TestTrue(TEXT("Walking speed is below running speed"),WalkSpeed>20 && WalkSpeed<RunSpeed*.8f);
        Test->TestTrue(TEXT("Combat stance plays while stationary"),Stationary);
        Test->TestTrue(TEXT("Walking remains available after combat"),WalkAfterCombat);
        Test->TestFalse(TEXT("Walking/running arms are not covered by a combat stance montage"),MovingStance);
        Test->TestTrue(TEXT("Stopping returns to combat stance"),Returned);
        Test->TestTrue(TEXT("Arm transitions contain no per-frame pose pop"),MaxArmStep<12.f);
        Test->TestEqual(TEXT("Three standard punches released during the sequence"),Animation->CapturedPunchReleaseCount,3);
        FVector ReferenceEye;
        if(bFirst && Animation->GetReferenceGuardEye(ReferenceEye))
        {
            // The guard stands almost as tall as the standing idle (2026-10-28): no longer a crouch below walking height.
            Test->TestTrue(TEXT("First-person camera follows the reference guard"),CombatEye<WalkEye+3.f && CombatEye>WalkEye-45.f);
            Test->TestTrue(TEXT("Running restores the locomotion eye height"),FMath::Abs(RunEye-WalkEye)<6.f);
            Test->TestTrue(TEXT("First-person stance/movement camera has no per-frame height pop"),MaxEyeStep<8.f);
            Test->AddInfo(FString::Printf(TEXT("FPS eyes walk %.2f, combat %.2f, run %.2f; transition step %.2f cm"),WalkEye,CombatEye,RunEye,MaxEyeStep));
        }
        FFileHelper::SaveStringToFile(CSV,*(FPaths::ProjectSavedDir()/(bFirst?TEXT("CombatLocomotion/movement-first-person.csv"):TEXT("CombatLocomotion/movement.csv"))));
        if(bVisual)FFileHelper::SaveStringToFile(Times,*(Dir/TEXT("captures.csv")));
        Test->AddInfo(FString::Printf(TEXT("Walk %.1f, run %.1f cm/s; largest transition hand step %.2f cm; stance stationary only; three standard releases"),WalkSpeed,RunSpeed,MaxArmStep));
        return true;
    }
private:
    FAutomationTestBase* Test;bool bVisual=false,bFirst=false,Walk=false,Run=false,WalkAfterCombat=false,MovingStance=false,Stationary=false,Returned=false,bHaveHand=false,bHaveEye=false;
    float WalkEye=0,CombatEye=0,RunEye=0,MaxEyeStep=0,LastEye=0;
    float Start=-1,WalkSpeed=0,RunSpeed=0,MaxArmStep=0,LastShot=-1;int32 Wait=0,Shot=0;uint32 Events=0;
    TObjectPtr<UInputAction> Move;TObjectPtr<ACameraActor> Camera;FVector LastHand;
    FString Dir,CSV=TEXT("time,input,speed,stance,selected,hand_x,hand_y,hand_z\n"),Times=TEXT("frame,time\n");
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatLocomotionTest,"RPG.Combat.StationaryStanceLocomotion",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCombatLocomotionTest::RunTest(const FString&)
{ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));ADD_LATENT_AUTOMATION_COMMAND(FCombatLocomotionReview(this,false));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatLocomotionVisual,"Visual.StationaryStanceLocomotion",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCombatLocomotionVisual::RunTest(const FString&)
{ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));ADD_LATENT_AUTOMATION_COMMAND(FCombatLocomotionReview(this,true));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatLocomotionFirst,"RPG.Combat.StationaryStanceLocomotionFirstPerson",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCombatLocomotionFirst::RunTest(const FString&)
{ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));ADD_LATENT_AUTOMATION_COMMAND(FCombatLocomotionReview(this,false,true));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;}
#endif
