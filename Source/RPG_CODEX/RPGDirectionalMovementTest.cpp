// Real Enhanced Input + GASP/CMC evaluation in a disposable PIE world; never saves a level.
#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "RPGAnimInstance.h"
#include "RPGDirectionalHoverComponent.h"
#include "RPGMovementAbilityComponent.h"
#include "RPGFlightAnimationComponent.h"
#include "RPGGASPInputBridgeComponent.h"
#include "RPGControllerInputComponent.h"
#include "RPGEnergySpellComponent.h"
#include "RPGCameraModeComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"
#include "Editor.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealClient.h"

namespace
{
FString SelectedGASPAnimation(UAnimInstance* Anim)
{
    // Inspect the actual evaluated motion-matching selection, not the available filenames.
    for(TFieldIterator<FStructProperty> It(Anim->GetClass());It;++It)
    {
        if(It->Struct->GetName()!=TEXT("AnimNode_MotionMatching"))continue;
        const auto* State=FindFProperty<FStructProperty>(It->Struct,TEXT("MotionMatchingState"));
        const auto* Result=State?FindFProperty<FStructProperty>(State->Struct,TEXT("SearchResult")):nullptr;
        const auto* Asset=Result?FindFProperty<FObjectPropertyBase>(Result->Struct,TEXT("SelectedAnim")):nullptr;
        if(!Asset)continue;
        const void* N=It->ContainerPtrToValuePtr<void>(Anim);
        const void* S=State->ContainerPtrToValuePtr<void>(N);
        const void* R=Result->ContainerPtrToValuePtr<void>(S);
        if(auto* Selected=Asset->GetObjectPropertyValue_InContainer(R))return Selected->GetName();
    }
    return TEXT("None");
}
class FDirectionalReviewCommand : public IAutomationLatentCommand
{
public:
    FDirectionalReviewCommand(FAutomationTestBase* In,bool Visual):Test(In),bVisual(Visual){}
    bool Update() override
    {
        UWorld* W=GEditor?GEditor->PlayWorld:nullptr;
        auto* C=W?UGameplayStatics::GetPlayerCharacter(W,0):nullptr;
        auto* PC=C?Cast<APlayerController>(C->GetController()):nullptr;
        auto* H=C?C->FindComponentByClass<URPGDirectionalHoverComponent>():nullptr;
        auto* A=C?C->FindComponentByClass<URPGMovementAbilityComponent>():nullptr;
        auto* Anim=C?Cast<URPGAnimInstance>(C->GetMesh()->GetAnimInstance()):nullptr;
        if(!C || !PC || !H || !A || !Anim)
        { if(++Wait<1200)return false;Test->AddError(TEXT("Directional player/components unavailable"));return true; }
        auto* M=C->GetCharacterMovement();auto* Mesh=C->GetMesh();
        if(Start<0.f)
        {
            if(++Warmup<90)return false;
            const FString TuckOverride=FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_REVIEW_FOOT_TUCK"));
            if(!TuckOverride.IsEmpty())H->SideFootTuck=FCString::Atof(*TuckOverride);
            Start=W->GetTimeSeconds();Home=C->GetActorLocation()+FVector(5000,5000,0);
            InitialFlySpeed=M->MaxFlySpeed;
            auto* Floor=W->SpawnActor<AStaticMeshActor>();Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
            Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
            Floor->SetActorScale3D(FVector(200,200,0.1));Floor->SetActorLocation(Home-FVector(0,0,C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+5));
            Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
            C->SetActorLocation(Home);C->SetActorRotation(FRotator::ZeroRotator);M->StopMovementImmediately();
            Mesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            if(auto* Mode=C->FindComponentByClass<URPGCameraModeComponent>();Mode && Mode->bIsFirstPerson)Mode->TogglePerspective();
            PC->SetViewTarget(C);PC->SetControlRotation(FRotator::ZeroRotator);
            Input=PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
            MoveAction=LoadObject<UInputAction>(nullptr,TEXT("/Game/Input/IA_Move.IA_Move"));
            auto* Controls=C->FindComponentByClass<URPGControllerInputComponent>();
            SpellAction=FindObject<UInputAction>(Controls,TEXT("IA_LeftSpell"));
            Test->TestTrue(TEXT("Real movement and spell actions found"),Input.IsValid() && MoveAction.IsValid() && SpellAction.IsValid());
            Dir=FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_DIRECTION_REVIEW_DIR"));
            if(Dir.IsEmpty())Dir=FPaths::ProjectSavedDir()/TEXT("DirectionalReview/Default");
            IFileManager::Get().MakeDirectory(*Dir,true);
            CSV=TEXT("time,axis_right,axis_forward,speed,yaw,body_yaw,hover,weight,forward,right,bob_weight,bob,pelvis_z,world_z,selected,knee_l_z,knee_r_z,foot_l_z,foot_r_z,out_l,out_r,knee_l_out,knee_r_out,turn,arm_side,mean_arm_side_cm,knee_l_heading,arm_l_angle,leg_l_angle,hand_l_out,elbow_l_out,foot_l_out,foot_tuck_l,knee_r_heading,arm_r_angle,leg_r_angle,hand_r_out,elbow_r_out,foot_r_out,foot_tuck_r\n");
        }
        const float T=W->GetTimeSeconds()-Start;
        FVector2D Axis=FVector2D::ZeroVector;
        if(T<2.5f)Axis={0,-1};else if(T<4.5f)Axis={1,0};else if(T<6.5f)Axis={0,1};else if(T<8.5f)Axis={-1,0};
        else if(T<10.5f)Axis={0.707,-0.707};else if(T<12.5f)Axis={-0.707,-0.707};
        if(T>=14.f && !bStartedHover)
        { C->SetActorLocation(Home+FVector(0,0,400));M->StopMovementImmediately();A->StartFlight();HoverZ=C->GetActorLocation().Z;bStartedHover=true; }
        if(T>=19.f && T<21.f)Axis={0,-1};else if(T>=21.f && T<23.f)Axis={1,0};else if(T>=23.f && T<25.f)Axis={-0.707,-0.707};
        else if(T>=25.f && T<27.f)Axis={-1,0};else if(T>=27.f && T<29.f)Axis={0,0.4};
        else if(T>=31.f && T<33.f)Axis={0,-1};else if(T>=33.f && T<35.5f)Axis={0,1};
        if(T>=38.f && !bStoppedHover) { A->StopFlight();bStoppedHover=true; }
        if(T>=29.2f && !bFastOn) { A->ToggleFastFlight();bFastOn=true; }
        if(T>=29.8f && !bFastOff) { A->ToggleFastFlight();bFastOff=true; }
        if(T>29.5f && T<29.7f)SawHoverFast|=A->IsFastFlight() && H->IsHovering() && M->MaxFlySpeed<=H->HoverSpeed+0.1f;
        const float Yaw=T>=10.5f && T<12.5f?(T-10.5f)*30.f:T>=12.5f && T<13.5f?60.f*(13.5f-T):T>=31.f && T<33.f?(T-31.f)*25.f:0.f;
        PC->SetControlRotation(FRotator(T>=31.f && T<33.f?35.f:0.f,Yaw,0));
        Input->InjectInputForAction(MoveAction.Get(),FInputActionValue(Axis),{},{});
        Input->InjectInputForAction(SpellAction.Get(),FInputActionValue(T>=31.1f && T<32.8f),{},{});
        const FVector Shoulder=Mesh->GetBoneLocation(TEXT("upperarm_r"))-Mesh->GetBoneLocation(TEXT("upperarm_l"));
        const FVector BodyForward=FVector::CrossProduct(Shoulder.GetSafeNormal2D(),FVector::UpVector).GetSafeNormal2D();
        const float FacingError=FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw,BodyForward.Rotation().Yaw));
        if(T>1.2f && T<12.4f)MaxGroundActorYaw=FMath::Max(MaxGroundActorYaw,float(FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw,C->GetActorRotation().Yaw))));
        const FString Selected=SelectedGASPAnimation(Anim);
        if(T>1.2f && T<2.4f)
        {
            SawBackVelocity|=M->Velocity.X<-150.f;MaxGroundFacing=FMath::Max(MaxGroundFacing,FacingError);
            SawBackClip|=Selected.Contains(TEXT("Run")) && Selected.Contains(TEXT("_B"));
            GroundSelections.Add(Selected);
        }
        if((T>3.4f && T<4.4f)||(T>7.4f && T<8.4f)||(T>9.4f && T<10.4f)||(T>11.4f && T<12.4f))
            MaxGroundFacing=FMath::Max(MaxGroundFacing,FacingError);
        if(T>16.f && T<19.f)
        {
            MinBob=FMath::Min(MinBob,Anim->DirectionalHover.Bob);MaxBob=FMath::Max(MaxBob,Anim->DirectionalHover.Bob);
            MaxIdleDrift=FMath::Max(MaxIdleDrift,float(FMath::Abs(C->GetActorLocation().Z-HoverZ)));
            SawBob|=H->BobWeight>0.95f;
        }
        if((T>19.8f && T<20.9f)||(T>21.8f && T<22.9f)||(T>23.8f && T<24.9f)||(T>25.8f && T<26.9f)||(T>27.8f && T<28.9f))
        {
            MaxHoverFacing=FMath::Max(MaxHoverFacing,FacingError);MaxMovingBob=FMath::Max(MaxMovingBob,H->BobWeight);
            const FVector BodyUp=(Mesh->GetBoneLocation(TEXT("head"))-Mesh->GetBoneLocation(TEXT("pelvis"))).GetSafeNormal();
            MinUpright=FMath::Min(MinUpright,float(FVector::DotProduct(BodyUp,FVector::UpVector)));
            SawHoverDirections|=(H->DirectionBlend.X<-.5f?1:0)|(H->DirectionBlend.Y>.5f?2:0)|(H->DirectionBlend.Y<-.5f?4:0)|(H->DirectionBlend.X>.3f?8:0);
            if(bHavePosition)MaxPhysicalHoverSpeed=FMath::Max(MaxPhysicalHoverSpeed,float(FVector::Dist2D(C->GetActorLocation(),LastPosition))/W->GetDeltaSeconds());
        }
        if(T>31.8f && T<32.7f)
        {
            const auto* S=C->FindComponentByClass<URPGEnergySpellComponent>();
            SawMovingSpell|=S && S->Left.bHeld && S->Left.Charge>0.4f && M->Velocity.Size2D()>150.f;
            MaxAimHeightError=FMath::Max(MaxAimHeightError,float(FMath::Abs(C->GetActorLocation().Z-HoverZ)));
        }
        if(T>34.6f && T<35.4f)SawFlight|=Anim->FlightBlend.X>.9f && Anim->DirectionalHover.Weight<.05f && M->Velocity.Size()>500.f;
        const float KneeL=Mesh->GetBoneLocation(TEXT("calf_l")).Z,KneeR=Mesh->GetBoneLocation(TEXT("calf_r")).Z;
        if(T>21.8f && T<22.9f)RightKneeLead=FMath::Max(RightKneeLead,KneeR-KneeL);
        if(T>25.8f && T<26.9f)LeftKneeLead=FMath::Max(LeftKneeLead,KneeL-KneeR);
        if(T>40.f && M->IsMovingOnGround())SawGroundReturn=true;
        LastPosition=C->GetActorLocation();bHavePosition=true;
        Test->TestFalse(TEXT("Finite directional pose"),Mesh->GetBoneLocation(TEXT("head")).ContainsNaN());
        if(T>=NextSample)
        {
            FString Row=FString::Printf(TEXT("%.4f,%.3f,%.3f,%.2f,%.2f,%.2f,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f"),T,Axis.X,Axis.Y,M->Velocity.Size(),C->GetActorRotation().Yaw,BodyForward.Rotation().Yaw,H->IsHovering(),H->PoseWeight,H->DirectionBlend.X,H->DirectionBlend.Y,H->BobWeight,Anim->DirectionalHover.Bob,Mesh->GetBoneLocation(TEXT("pelvis")).Z,C->GetActorLocation().Z,*Selected,KneeL,KneeR,Mesh->GetBoneLocation(TEXT("foot_l")).Z,Mesh->GetBoneLocation(TEXT("foot_r")).Z,
                Anim->DirectionalHover.KneeOut.X,Anim->DirectionalHover.KneeOut.Y,
                -FVector::DotProduct(Mesh->GetBoneLocation(TEXT("calf_l"))-Mesh->GetBoneLocation(TEXT("thigh_l")),C->GetActorRightVector()),
                FVector::DotProduct(Mesh->GetBoneLocation(TEXT("calf_r"))-Mesh->GetBoneLocation(TEXT("thigh_r")),C->GetActorRightVector()),Anim->DirectionalHover.Turn,Anim->DirectionalHover.ArmSide,
                .5*FVector::DotProduct(Mesh->GetBoneLocation(TEXT("hand_l"))-Mesh->GetBoneLocation(TEXT("upperarm_l"))+Mesh->GetBoneLocation(TEXT("hand_r"))-Mesh->GetBoneLocation(TEXT("upperarm_r")),C->GetActorRightVector()));
            for(int32 Side:{-1,1})
            {
                const FString S=Side<0?TEXT("l"):TEXT("r");
                const FVector Knee=Mesh->GetBoneLocation(*("calf_"+S))-Mesh->GetBoneLocation(*("thigh_"+S));
                const FVector Leg=Mesh->GetBoneLocation(*("foot_"+S))-Mesh->GetBoneLocation(*("thigh_"+S));
                const FVector Arm=Mesh->GetBoneLocation(*("hand_"+S))-Mesh->GetBoneLocation(*("upperarm_"+S));
                auto Angle=[&](const FVector& V){return FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(V,C->GetActorRightVector()),-V.Z));};
                const FVector Chest=Mesh->GetBoneLocation(TEXT("spine_03"));
                Row+=FString::Printf(TEXT(",%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f"),FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Knee,C->GetActorRightVector()),FVector::DotProduct(Knee,C->GetActorForwardVector()))),Angle(Arm),Angle(Leg),
                    Side*FVector::DotProduct(Mesh->GetBoneLocation(*("hand_"+S))-Chest,C->GetActorRightVector()),Side*FVector::DotProduct(Mesh->GetBoneLocation(*("lowerarm_"+S))-Chest,C->GetActorRightVector()),
                    Side*FVector::DotProduct(Mesh->GetBoneLocation(*("foot_"+S))-Mesh->GetBoneLocation(TEXT("pelvis")),C->GetActorRightVector()),Anim->DirectionalHover.FootTuck[Side<0?0:1]);
            }
            CSV+=Row+TEXT("\n");
            if(bVisual && T<41.f)FScreenshotRequest::RequestScreenshot(Dir/FString::Printf(TEXT("direction_%05d.png"),Shot++),false,false);
            NextSample+=1.f/30.f;
        }
        if(T<42.f)return false;
        Test->TestTrue(TEXT("Actual GASP backward run selected with backward velocity"),SawBackVelocity && SawBackClip);
        Test->TestTrue(TEXT("Ground stays view-facing while retaining authored shoulder twists"),MaxGroundActorYaw<5.f && MaxGroundFacing<60.f);
        Test->TestTrue(TEXT("All four hover direction poses observed"),SawHoverDirections==15);
        Test->TestTrue(TEXT("Directional hover remains upright and view-facing"),MinUpright>.80f && MaxHoverFacing<20.f);
        Test->TestTrue(TEXT("Sideways hover raises the knee on the moving side"),RightKneeLead>2.f && LeftKneeLead>2.f);
        Test->TestTrue(TEXT("Stationary hover gently bobs without capsule drift"),SawBob && MaxBob-MinBob>2.f && MaxIdleDrift<.5f);
        Test->TestTrue(TEXT("Idle bob fades out during movement"),MaxMovingBob<.02f);
        Test->TestTrue(TEXT("Hover displacement obeys the speed limit before physics"),MaxPhysicalHoverSpeed<=H->HoverSpeed+5.f);
        Test->TestTrue(TEXT("Fast toggle in hover preserves and restores travel speed"),SawHoverFast && !A->IsFastFlight() && FMath::IsNearlyEqual(M->MaxFlySpeed,InitialFlySpeed,0.1f));
        Test->TestTrue(TEXT("Backward hover supports spell charging and level travel while looking up"),SawMovingSpell && MaxAimHeightError<2.f);
        Test->TestTrue(TEXT("Full forward input restores existing travel flight"),SawFlight);
        Test->TestTrue(TEXT("Landing restores ground movement and clears hover"),SawGroundReturn && Anim->DirectionalHover.Weight==0.f);
        FString Report=FString::Printf(TEXT("DIRECTIONAL groundFacing=%.2f groundActorYaw=%.2f hoverFacing=%.2f upright=%.3f bobSpan=%.3f idleDrift=%.3f movingBob=%.4f aimHeight=%.3f directions=%d backClip=%d movingSpell=%d flight=%d ground=%d\n"),MaxGroundFacing,MaxGroundActorYaw,MaxHoverFacing,MinUpright,MaxBob-MinBob,MaxIdleDrift,MaxMovingBob,MaxAimHeightError,SawHoverDirections,SawBackClip,SawMovingSpell,SawFlight,SawGroundReturn);
        for(const auto& Name:GroundSelections)Report+=Name+TEXT("\n");
        Report+=FString::Printf(TEXT("PhysicalHoverSpeed=%.3f restoredFlySpeed=%.3f initialFlySpeed=%.3f hoverFastToggle=%d\n"),MaxPhysicalHoverSpeed,M->MaxFlySpeed,InitialFlySpeed,SawHoverFast);
        Report+=FString::Printf(TEXT("LeadingKneeLift right=%.3f left=%.3f cm\n"),RightKneeLead,LeftKneeLead);
        Test->AddInfo(Report);FFileHelper::SaveStringToFile(Report,*(Dir/TEXT("result.txt")));FFileHelper::SaveStringToFile(CSV,*(Dir/TEXT("samples.csv")));return true;
    }
private:
    FAutomationTestBase* Test;bool bVisual;int32 Wait=0,Warmup=0,Shot=0,SawHoverDirections=0;
    float Start=-1,NextSample=0,HoverZ=0,MinBob=100,MaxBob=-100,MaxIdleDrift=0,MaxMovingBob=0,MinUpright=1,MaxAimHeightError=0,MaxGroundFacing=0,MaxHoverFacing=0;
    float MaxGroundActorYaw=0.f;
    float RightKneeLead=0.f,LeftKneeLead=0.f;
    float MaxPhysicalHoverSpeed=0.f,InitialFlySpeed=0.f;FVector LastPosition;
    bool bHavePosition=false,bFastOn=false,bFastOff=false,SawHoverFast=false;
    bool bStartedHover=false,bStoppedHover=false,SawBackVelocity=false,SawBackClip=false,SawBob=false,SawMovingSpell=false,SawFlight=false,SawGroundReturn=false;
    FVector Home;FString Dir,CSV;TSet<FString> GroundSelections;
    TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> Input;TWeakObjectPtr<UInputAction> MoveAction,SpellAction;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectionalMovementTest,"RPG.Movement.Directional",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FDirectionalMovementTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FDirectionalReviewCommand(this,false));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectionalMovementVisual,"Visual.DirectionalMovement",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FDirectionalMovementVisual::RunTest(const FString&)
{
    auto* Play=NewObject<ULevelEditorPlaySettings>();Play->NewWindowWidth=1280;Play->NewWindowHeight=752;Play->LastExecutedPlayModeType=PlayMode_InEditorFloating;
    FRequestPlaySessionParams Session;Session.WorldType=EPlaySessionWorldType::PlayInEditor;Session.EditorPlaySettings=Play;GEditor->RequestPlaySession(Session);
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));ADD_LATENT_AUTOMATION_COMMAND(FDirectionalReviewCommand(this,true));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
