// Flight-only visual/regression harness. Runs in a temporary PIE world; never saves the map.
// UnrealEditor RPG_CODEX.uproject -ExecCmds="Automation RunTests Visual.FlightReview" -TestExit="Automation Test Queue Empty" -unattended
#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "RPGAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "RPGFlightAnimationComponent.h"
#include "RPGMovementAbilityComponent.h"
#include "RPGCameraModeComponent.h"
#include "RPGCombatStateComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/IConsoleManager.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealClient.h"

namespace
{
class FFlightReviewCommand : public IAutomationLatentCommand
{
public:
    explicit FFlightReviewCommand(FAutomationTestBase* InTest, bool bInCapture=true) : Test(InTest), bWantCapture(bInCapture) {}
    ~FFlightReviewCommand() override
    {
        if (bSavedVisualSettings)
        {
            IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlurQuality"))->Set(SavedBlur);
            IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"))->Set(SavedAA);
        }
    }
    bool Update() override
    {
        UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
        ACharacter* Player = World ? UGameplayStatics::GetPlayerCharacter(World, 0) : nullptr;
        APlayerController* PC = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
        URPGFlightAnimationComponent* Flight = Player ? Player->FindComponentByClass<URPGFlightAnimationComponent>() : nullptr;
        URPGMovementAbilityComponent* Ability = Player ? Player->FindComponentByClass<URPGMovementAbilityComponent>() : nullptr;
        if (!Player || !PC || !Flight || !Ability)
        {
            if (++Waiting < 1200) return false;
            Test->AddError(TEXT("Flight review: player/components unavailable")); return true;
        }
        USkeletalMeshComponent* Mesh = Player->GetMesh();
        UCharacterMovementComponent* Move = Player->GetCharacterMovement();
        URPGAnimInstance* Anim = Cast<URPGAnimInstance>(Mesh->GetAnimInstance());
        if (!Anim) { Test->AddError(TEXT("Flight review requires RPGAnimInstance")); return true; }
        const float Now = World->GetTimeSeconds();
        if (Start < 0.f)
        {
            // Headless PIE has no viewport warmup. Let ground/GASP movement
            // initialize before recording its restoration baseline, too.
            if(!bWantCapture && ++GroundWarmupFrames<30)return false;
            // Let the native PIE viewport settle, and exercise one screenshot
            // before recording. Startup DPI/resize frames must not enter reviews.
            if(bWantCapture && FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_FLIGHT_TRACKONLY")).IsEmpty())
            {
                if(++ViewportWaitFrames>1200) { Test->AddError(TEXT("Flight review viewport did not settle"));return true; }
                const UGameViewportClient* View=World->GetGameViewport();
                const FIntPoint Size=View && View->Viewport?View->Viewport->GetSizeXY():FIntPoint::ZeroValue;
                const double RealNow=FPlatformTime::Seconds();
                if(Size!=StableViewportSize || ViewportStableSince<0.)
                { StableViewportSize=Size;ViewportStableSince=RealNow; }
                if(Size.X<640 || Size.Y<360 || RealNow-ViewportStableSince<0.5) return false;
                if(!bViewportWarmup)
                {
                    FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("FlightReview/ViewportWarmup.png"),false,false);
                    bViewportWarmup=true;ViewportStableSince=RealNow;return false;
                }
            }
            bNormalInput=!FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_FLIGHT_REVIEW_INPUT")).IsEmpty();
            bStrafeInput=!FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_FLIGHT_REVIEW_STRAFE")).IsEmpty();
            bEarlyCancel=!FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_FLIGHT_REVIEW_EARLY_CANCEL")).IsEmpty();
            bBoostCycle=!FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_FLIGHT_REVIEW_BOOST_CYCLE")).IsEmpty();
            bMovingEntry=!FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_FLIGHT_REVIEW_MOVING_ENTRY")).IsEmpty();
            bGroundEntry=!FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_FLIGHT_REVIEW_GROUND_ENTRY")).IsEmpty();
            bVerticalPause=!FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_FLIGHT_REVIEW_VERTICAL_PAUSE")).IsEmpty();
            bVerticalRise=bVerticalPause || !FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_FLIGHT_REVIEW_VERTICAL_RISE")).IsEmpty();
            bQuickBrake=!FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_FLIGHT_REVIEW_QUICK_BRAKE")).IsEmpty();
            bShortBankYield=!FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_FLIGHT_REVIEW_SHORT_BANK_YIELD")).IsEmpty();
            bBankYield=bShortBankYield || !FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_FLIGHT_REVIEW_BANK_YIELD")).IsEmpty();
            bRepeatBankReentry=!FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_FLIGHT_REVIEW_BANK_REENTRY_REPEAT")).IsEmpty();
            bBankReentry=bRepeatBankReentry || !FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_FLIGHT_REVIEW_BANK_REENTRY")).IsEmpty();
            bSpiral=!FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_FLIGHT_REVIEW_SPIRAL")).IsEmpty();
            if(bNormalInput)
            {
                MoveAction=LoadObject<UInputAction>(nullptr,TEXT("/Game/Input/IA_Move.IA_Move"));
                Input=PC->GetLocalPlayer()?PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>():nullptr;
                if(!Input.IsValid() || !MoveAction.IsValid()) { Test->AddError(TEXT("Flight review input action/subsystem unavailable"));return true; }
            }
            InitialLocation = Player->GetActorLocation();
            GroundZ = InitialLocation.Z;
            InitialAcceleration=Move->MaxAcceleration;
            for(const FTickPrerequisite& Entry:Move->PrimaryComponentTick.GetPrerequisites())
                InitialMovementDependsOnAbility |= Entry.PrerequisiteObject.Get()==Ability;
            if(UCameraComponent* Lens=Player->FindComponentByClass<UCameraComponent>())InitialFOV=Lens->FieldOfView;
            if(USpringArmComponent* Arm=Player->FindComponentByClass<USpringArmComponent>())
            {
                InitialArmLength=Arm->TargetArmLength;
                InitialCameraLag=Arm->bEnableCameraLag;
                InitialCameraRotationLag=Arm->bEnableCameraRotationLag;
                // Reset the camera's lag history for this test-only teleport.
                Arm->bEnableCameraLag=Arm->bEnableCameraRotationLag=false;
            }
            // A temporary continuation of the test floor catches the long flight path.
            // It exists only in PIE; real collision/landing runs without any teleport.
            if(UStaticMesh* FloorMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")))
            {
                AStaticMeshActor* Floor=World->SpawnActor<AStaticMeshActor>();
                Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
                Floor->GetStaticMeshComponent()->SetStaticMesh(FloorMesh);
                Floor->SetActorScale3D(FVector(1000,1000,0.1f));
                Floor->SetActorLocation(InitialLocation-FVector(0,0,Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+10.f));
                Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
                bFloorCreated=true;
            }
            if(!bGroundEntry)Player->SetActorLocation(InitialLocation + FVector(0,0,1800), false, nullptr, ETeleportType::ResetPhysics);
            Player->SetActorRotation(FRotator::ZeroRotator);
            if(bMovingEntry)Move->Velocity=FVector(400,0,0);
            if(!bNormalInput)
            {
                Move->bOrientRotationToMovement = false;
                Player->bUseControllerRotationYaw = false;
            }
            Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            if (URPGCameraModeComponent* Mode = Player->FindComponentByClass<URPGCameraModeComponent>(); Mode && Mode->bIsFirstPerson) Mode->TogglePerspective();
            Camera = World->SpawnActor<ACameraActor>();
            Camera->GetCameraComponent()->FieldOfView = 50.f;
            Camera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount = true;
            Camera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount = 0.f;
            SavedBlur=IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlurQuality"))->GetInt();
            SavedAA=IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"))->GetInt();
            bSavedVisualSettings=true;
            const FString Angle=FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_FLIGHT_REVIEW_ANGLE"));
            if(!Angle.IsEmpty())ReviewAngle=FCString::Atof(*Angle);
            const FString Rate=FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_FLIGHT_REVIEW_FPS"));
            if(!Rate.IsEmpty())CaptureFPS=FMath::Clamp(FCString::Atof(*Rate),10.f,60.f);
            if(!FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_FLIGHT_REVIEW_EXTENDED")).IsEmpty())CaptureEnd=39.8f;
            MirrorSign=FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_FLIGHT_REVIEW_MIRROR")).IsEmpty()?1.f:-1.f;
            const FString HeadingOffset=FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_FLIGHT_REVIEW_HEADING"));
            if(!HeadingOffset.IsEmpty())ReviewHeading=FCString::Atof(*HeadingOffset);
            // Offset the initial facing too, so a heading-wrap review does not
            // begin with an unrelated turn-in-place from the map's spawn yaw.
            const FRotator InitialHeading(0,ReviewHeading*MirrorSign,0);
            Player->SetActorRotation(InitialHeading);
            PC->SetControlRotation(InitialHeading);
            if(bMovingEntry)Move->Velocity=InitialHeading.Vector()*400.f;
            const FString BoostDistance=FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_FLIGHT_REVIEW_BOOST_DISTANCE"));
            if(!BoostDistance.IsEmpty())
                if(URPGCameraModeComponent* Mode=Player->FindComponentByClass<URPGCameraModeComponent>())
                    Mode->FlightBoostDistance=FMath::Max(0.f,FCString::Atof(*BoostDistance));
            IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlurQuality"))->Set(0);
            IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"))->Set(1);
            if(!bNormalInput)Ability->bFlightFollowsLookPitch = false; // scripted input already contains its pitch
            bGameplayCamera=!FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_FLIGHT_GAMEPLAY_CAMERA")).IsEmpty();
            UE_LOG(LogTemp,Display,TEXT("FLIGHTVIEW camera manager=%s gameplay=%d"),PC->PlayerCameraManager?*PC->PlayerCameraManager->GetClass()->GetName():TEXT("none"),bGameplayCamera);
            TInlineComponentArray<UCameraComponent*> CameraComponents(Player);
            for(const UCameraComponent* Lens:CameraComponents)
                UE_LOG(LogTemp,Display,TEXT("FLIGHTVIEW camera=%s active=%d parent=%s"),*Lens->GetName(),Lens->IsActive(),Lens->GetAttachParent()?*Lens->GetAttachParent()->GetName():TEXT("none"));
            if(bGameplayCamera)PC->SetViewTarget(Player);else PC->SetViewTarget(Camera.Get());
            RestLocation = Mesh->GetRelativeLocation(); RestRotation = Mesh->GetRelativeRotation().Quaternion();
            Test->TestTrue(TEXT("Flight starts"), Ability->StartFlight());
            Dir = FPaths::ProjectSavedDir() / TEXT("FlightReview/Frames");
            const FString Override = FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_FLIGHT_REVIEW_DIR"));
            if (!Override.IsEmpty()) Dir = Override;
            IFileManager::Get().MakeDirectory(*Dir, true);
            bCapture = bWantCapture && FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_FLIGHT_TRACKONLY")).IsEmpty();
            Start = Now;
            LastRealTime = FPlatformTime::Seconds();
            return false;
        }
        const float T = Now - Start;
        // The pause scenario resumes cruise later; leave the full ordinary
        // acceleration action before asking for its independent boost action.
        const float ReviewBoostTime=bVerticalPause?4.5f:4.f;
        if(!bCameraLagRestored && T>=0.05f)
        {
            if(USpringArmComponent* Arm=Player->FindComponentByClass<USpringArmComponent>())
            {
                Arm->bEnableCameraLag=InitialCameraLag;
                Arm->bEnableCameraRotationLag=InitialCameraRotationLag;
            }
            bCameraLagRestored=true;
        }
        SawTakeoff |= Anim->FlightActionIndex == RPGFlight::Takeoff;
        SawAcceleration |= Anim->FlightActionIndex == RPGFlight::Accelerate;
        SawBoostAction |= Anim->FlightActionIndex == RPGFlight::Boost;
        SawRelease |= Anim->FlightActionIndex == RPGFlight::Release;
        SawBank |= FMath::Abs(Anim->FlightTurn)>0.3f;
        SawSlowBank |= FMath::Abs(Anim->FlightTurn)>0.3f && Anim->FlightBlend.Y<0.1f;
        if(bMovingEntry && T>=0.95f && T<1.4f && Anim->FlightBlend.X>0.95f)
            SawDelayedMovingAcceleration |= Anim->FlightActionIndex==RPGFlight::Accelerate;
        const float Dt = World->GetDeltaSeconds();
        if(bSpiral)
        {
            const FRotator Lean=(Mesh->GetRelativeRotation().Quaternion()*RestRotation.Inverse()).Rotator();
            if(T>=10.9f && T<11.8f)SawSpiralClimb |= Lean.Pitch>15.f && Lean.Roll*MirrorSign>15.f && Anim->FlightTurn*MirrorSign>0.3f;
            if(T>=13.f && T<13.8f)SawSpiralDive |= Lean.Pitch<-15.f && Lean.Roll*MirrorSign<-15.f && Anim->FlightTurn*MirrorSign<-0.3f;
        }
        if(bVerticalRise && T>=2.3f && T<2.6f && Move->Velocity.Size2D()<1.f && Move->Velocity.Z>500.f)
        {
            MaxVerticalTurn=FMath::Max(MaxVerticalTurn,FMath::Abs(Anim->FlightTurn));++VerticalSamples;
            const FRotator Lean=(Mesh->GetRelativeRotation().Quaternion()*RestRotation.Inverse()).Rotator();
            MaxVerticalFacingError=FMath::Max(MaxVerticalFacingError,FMath::Abs(FMath::FindDeltaAngleDegrees(
                float(PC->GetControlRotation().Yaw),float(Player->GetActorRotation().Yaw+Lean.Yaw))));
        }
        if(bVerticalPause && T>=3.12f && T<3.3f && Move->Velocity.Size()<1.f)
        {
            ++VerticalPauseSamples;
            const FRotator Lean=(Mesh->GetRelativeRotation().Quaternion()*RestRotation.Inverse()).Rotator();
            MaxVerticalPauseFacingError=FMath::Max(MaxVerticalPauseFacingError,FMath::Abs(FMath::FindDeltaAngleDegrees(
                float(PC->GetControlRotation().Yaw),float(Player->GetActorRotation().Yaw+Lean.Yaw))));
        }
        if(T>21.f && T<23.5f)SawNaturalLanding |= Move->IsMovingOnGround();
        if(T>=2.3f && T<2.45f)
            SawFlightAcceleration |= Player->GetVelocity().Size()>500.f && Move->GetCurrentAcceleration().Size()>Ability->FlightAcceleration*0.9f;
        if(T>=ReviewBoostTime+0.5f && T<ReviewBoostTime+0.75f)
            SawBoostAcceleration |= Player->GetVelocity().Size()>1500.f && Move->GetCurrentAcceleration().Size()>Ability->FastFlightAcceleration*0.9f;
        if(T>5.f && T<6.f)
        {
            const UCameraComponent* Lens=Player->FindComponentByClass<UCameraComponent>();
            const USpringArmComponent* Arm=Player->FindComponentByClass<USpringArmComponent>();
            SawSpeedCamera |= Lens && Arm && Lens->FieldOfView>InitialFOV+3.f && Arm->TargetArmLength>InitialArmLength+50.f;
            if(bGameplayCamera && PC->PlayerCameraManager)
                SawActiveSpeedCamera |= PC->PlayerCameraManager->GetFOVAngle()>InitialViewFOV+3.f
                    && FVector::Dist(PC->PlayerCameraManager->GetCameraLocation(),Player->GetActorLocation())>InitialViewDistance+50.f;
        }
        if(bGameplayCamera && PC->PlayerCameraManager && T>=1.2f && T<1.5f)
        {
            InitialViewFOV=PC->PlayerCameraManager->GetFOVAngle();
            InitialViewDistance=FVector::Dist(PC->PlayerCameraManager->GetCameraLocation(),Player->GetActorLocation());
        }
        // Actual CharacterMovement acceleration, braking and flight modes; no direct animation weights.
        const bool bInput = (T >= (bMovingEntry?0.f:2.f) && T < (bEarlyCancel?6.8f:14.f)) || (!bEarlyCancel && T >= 17.f && T < 19.f);
        const bool bRising=bVerticalRise && T>=2.f && T<2.6f;
        Ability->SetFlightRiseHeld(bRising);
        const bool bVerticalPaused=bVerticalPause && T>=2.6f && T<3.4f;
        const bool bApplyInput=bInput && !bRising && !bVerticalPaused && !(bQuickBrake && T>=4.6f && T<5.4f);
        float Yaw = 0.f, Pitch = 0.f;
        if (T >= 2.8f && T < 3.4f) Yaw = (T-2.8f)*50.f;
        else if (T >= 3.4f && T < 4.f) Yaw = 30.f-(T-3.4f)*50.f;
        if (T >= 6.f && T < 8.f) Yaw = (T-6.f)*45.f;
        else if (T >= 8.f && T < 10.f) Yaw = 90.f-(T-8.f)*67.5f;
        else if (T >= 10.f) Yaw = -45.f;
        if(bSpiral && T>=10.f && T<12.f)Yaw=-45.f+(T-10.f)*60.f;
        else if(bSpiral && T>=12.f && T<14.f)Yaw=75.f-(T-12.f)*60.f;
        // Offset the complete path to exercise turns across the +/-180 yaw seam.
        // Hold look during the vertical pause, then resume a continuous arc.
        // Jumping into the middle of the standard arc would snap the test camera.
        if(bVerticalPause && T<3.4f)Yaw=0.f;
        else if(bVerticalPause && T<3.9f)Yaw=(T-3.4f)*50.f;
        else if(bVerticalPause && T<4.4f)Yaw=25.f-(T-3.9f)*50.f;
        Yaw=(Yaw+ReviewHeading)*MirrorSign;
        if (T >= 10.f && T < 12.f) Pitch = 32.f;
        else if (T >= 12.f && T < 14.f) Pitch = -32.f;
        if(!bNormalInput)Player->SetActorRotation(FRotator(0,Yaw,0));
        PC->SetControlRotation(FRotator(Pitch-(bGameplayCamera && !bNormalInput?12.f:0.f),Yaw,0));
        if(bNormalInput)
        {
            const FVector2D Axis=!bApplyInput?FVector2D::ZeroVector:(bStrafeInput?FVector2D(MirrorSign,0):FVector2D(0,1));
            Input->InjectInputForAction(MoveAction.Get(),FInputActionValue(Axis),{},{});
        }
        else if (bApplyInput)
        {
            const FVector Direction = FRotator(Pitch,Yaw,0).Vector();
            Player->AddMovementInput(Direction);
        }
        if (!bBoosted && T >= ReviewBoostTime) { Ability->ToggleFastFlight(); bBoosted = true; }
        if(bBoostCycle && !bCycleDown && T>=6.6f) { Ability->ToggleFastFlight();bCycleDown=true; }
        if(bBoostCycle && !bCycleUp && T>=7.6f) { Ability->ToggleFastFlight();bCycleUp=true; }
        if(bBoostCycle && T>=7.2f && T<7.5f)SawCycleNormal |= Player->GetVelocity().Size()<700.f;
        if(bBoostCycle && T>=8.2f && T<8.5f)SawCycleBoost |= Player->GetVelocity().Size()>1500.f;
        if (!bCancelled && T >= (bEarlyCancel?6.8f:19.f)) { Ability->StopFlight(); bCancelled = true; }
        if(bBankReentry && !bBankReentryStopped && T>=7.f){Ability->StopFlight();bBankReentryStopped=true;}
        if(bBankReentry && !bBankReentryStarted && T>=7.12f)
        {
            const bool bStarted=Ability->StartFlight();Test->TestTrue(TEXT("Banked rapid reentry starts"),bStarted);
            if(bStarted)Ability->ToggleFastFlight();bBankReentryStarted=true;
        }
        if(bRepeatBankReentry && !bSecondBankStop && T>=7.22f){Ability->StopFlight();bSecondBankStop=true;}
        if(bRepeatBankReentry && !bSecondBankStart && T>=7.34f)
        {
            const bool bStarted=Ability->StartFlight();Test->TestTrue(TEXT("Repeated banked reentry starts"),bStarted);
            if(bStarted)Ability->ToggleFastFlight();bSecondBankStart=true;
        }
        if(bBankReentry && T>=7.08f && T<7.85f)
        {
            const FVector Axis=(Mesh->GetBoneLocation(TEXT("spine_05"))-Mesh->GetBoneLocation(TEXT("pelvis"))).GetSafeNormal();
            const FVector Hip=Player->GetActorTransform().InverseTransformPosition(Mesh->GetBoneLocation(TEXT("pelvis")));
            if(bHadBankReentryAxis && T>=7.12f)
            {
                MaxBankReentryTorsoRate=FMath::Max(MaxBankReentryTorsoRate,
                    float(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(PreviousBankReentryAxis,Axis),-1.,1.))))/FMath::Max(Dt,0.001f));
                MaxBankReentryHipSpeed=FMath::Max(MaxBankReentryHipSpeed,float(FVector::Dist(Hip,PreviousBankReentryHip))/FMath::Max(Dt,0.001f));
            }
            PreviousBankReentryAxis=Axis;PreviousBankReentryHip=Hip;bHadBankReentryAxis=true;
        }
        if(bBankReentry && T>=7.9f && T<8.1f)SawBankReentryFinish |= Anim->FlightWeight>0.99f && Anim->FlightBlend.Y>0.9f
            && Player->GetVelocity().Size()>1500.f && Anim->FlightActionIndex!=RPGFlight::Takeoff;
        // Ground return at the original floor, while retaining the horizontal movement graph.
        if (!bFloorCreated && !bLanding && T >= 21.f)
        {
            Ability->StopFlight();
            Player->SetActorLocation(InitialLocation + FVector(0,0,80), false, nullptr, ETeleportType::ResetPhysics);
            Move->Velocity = FVector(350,0,-100); bLanding = true;
        }
        URPGCombatStateComponent* Combat = Player->FindComponentByClass<URPGCombatStateComponent>();
        const float BankYieldEnd=bShortBankYield?7.06f:7.6f;
        if(bBankYield && Combat && T>=7.f && T<BankYieldEnd)Combat->bIsBlocking=true;
        if(bBankYield && Combat && T>=BankYieldEnd && T<8.f)Combat->bIsBlocking=false;
        if(bBankYield && !bShortBankYield && T>=7.3f && T<7.5f)
        {
            ++BankYieldSamples;
            MaxBankYieldPoseWeight=FMath::Max(MaxBankYieldPoseWeight,Anim->FlightWeight);
            bBankYieldFlowClear &= Anim->FlowBones.Num()==0;
            MaxBankYieldBodyAngle=FMath::Max(MaxBankYieldBodyAngle,float(FMath::RadiansToDegrees(
                RestRotation.AngularDistance(Mesh->GetRelativeRotation().Quaternion()))));
        }
        if(bBankYield && T>=(bShortBankYield?7.03f:7.55f) && T<(bShortBankYield?7.35f:7.9f))
        {
            const FQuat Current=Mesh->GetComponentQuat();
            if(bHadBankResumeRotation && (!bShortBankYield || T>=BankYieldEnd))MaxBankResumeAngularRate=FMath::Max(MaxBankResumeAngularRate,
                float(FMath::RadiansToDegrees(PreviousBankResumeRotation.AngularDistance(Current)))/FMath::Max(Dt,0.001f));
            PreviousBankResumeRotation=Current;bHadBankResumeRotation=true;
        }
        if(bBankYield && T>=(bShortBankYield?7.4f:8.2f) && T<(bShortBankYield?7.6f:8.5f))SawBankYieldResume |= Anim->FlightWeight>0.99f && FMath::Abs(Anim->FlightTurn)>0.1f;
        if (!bRestarted && T >= 24.f)
        {
            Player->SetActorLocation(Player->GetActorLocation()+FVector(0,0,500),false,nullptr,ETeleportType::ResetPhysics);
            Ability->StartFlight(); bRestarted=true;
        }
        if (Combat && T >= 25.f && T < 25.6f) Combat->bIsBlocking=true;
        if (Combat && T >= 25.6f) Combat->bIsBlocking=false;
        if (T >= 25.3f && T < 25.6f) SawCombatYield |= Anim->FlightWeight < 0.01f && Anim->FlowBones.Num()==0;
        if (T >= 26.5f && T < 27.f) SawCombatResume |= Anim->FlightWeight > 0.99f;
        if (!bRapidStop && T >= 27.f) { Ability->StopFlight();bRapidStop=true; }
        if (!bRapidStart && T >= 27.1f) { Ability->StartFlight();bRapidStart=true; }
        if (!bFinalStop && T >= 28.f) { Ability->StopFlight();bFinalStop=true; }
        if (!bRootStarted && T >= 29.f)
        {
            Ability->StartFlight();
            UAnimSequence* Fixture = DuplicateObject<UAnimSequence>(Anim->FlightClips[0],GetTransientPackage());
            Fixture->bEnableRootMotion = true;
            RootMontage = Anim->PlaySlotAnimationAsDynamicMontage(Fixture,TEXT("DefaultSlot"),0.05f,0.1f);
            bRootStarted=true;
        }
        if (T >= 29.4f && T < 30.5f) SawTraversalYield |= Player->IsPlayingRootMotion() && Anim->FlightWeight<0.01f && Anim->FlowBones.Num()==0;
        if (!bRootStopped && T >= 31.f) { Anim->Montage_Stop(0.1f,RootMontage.Get());bRootStopped=true; }
        if (T >= 31.8f && T < 32.f) SawTraversalResume |= Anim->FlightWeight>0.99f;
        if (!bRootExit && T >= 32.f) { Ability->StopFlight(); bRootExit=true; }
        URPGCameraModeComponent* CameraMode=Player->FindComponentByClass<URPGCameraModeComponent>();
        if (!bFirstPersonStarted && T>=34.f && CameraMode)
        {
            Ability->StartFlight(); CameraMode->TogglePerspective();bFirstPersonStarted=true;
        }
        if (T>=34.8f && T<35.2f)
        {
            const USkeletalMeshComponent* Shadow=FindObject<USkeletalMeshComponent>(Player,TEXT("FirstPersonShadowBody"));
            const URPGAnimInstance* ShadowAnim=Shadow?Cast<URPGAnimInstance>(Shadow->GetAnimInstance()):nullptr;
            SawShadow |= ShadowAnim && ShadowAnim->FlightWeight>0.99f
                && FMath::Abs(ShadowAnim->FlightPhase-Anim->FlightPhase)<0.01f && ShadowAnim->FlightClips.Num()==RPGFlight::NumClips;
        }
        if (!bFirstPersonStopped && T>=35.2f) { Ability->StopFlight();bFirstPersonStopped=true; }
        if (!bPerspectiveRestored && T>=36.4f && CameraMode)
        {
            if(CameraMode->bIsFirstPerson)CameraMode->TogglePerspective();bPerspectiveRestored=true;
        }
        if (!bLowFlightStarted && T>=37.f)
        {
            Player->SetActorLocation(FVector(Player->GetActorLocation().X,Player->GetActorLocation().Y,GroundZ+80.f),false,nullptr,ETeleportType::ResetPhysics);
            Ability->StartFlight();Move->Velocity=FVector(400,0,0);bLowFlightStarted=true;
        }
        if(T>=37.f && T<38.f)Player->AddMovementInput(FVector::ForwardVector);
        if(!bLowFlightStopped && T>=38.f){Ability->StopFlight();bLowFlightStopped=true;}
        if(T>=38.7f && T<39.5f)SawGroundHandoff |= Move->IsMovingOnGround() && Anim->FlightWeight<0.01f;
        const FVector Center = Mesh->GetBoneLocation(TEXT("pelvis"));
        const FVector Back = FRotator(0,Yaw-ReviewAngle*MirrorSign,0).Vector();
        const FVector CamLocation = Center - Back*520.f + FVector(0,0,95);
        Camera->SetActorLocationAndRotation(CamLocation, (Center+FVector(0,0,15)-CamLocation).Rotation());
        // Log actual evaluated body/limb motion; compare smoothness with stalls excluded.
        const FVector Head = Mesh->GetBoneLocation(TEXT("head"));
        const FVector Hand = Mesh->GetBoneLocation(TEXT("hand_l"));
        const FVector LocalHand = Mesh->GetComponentTransform().InverseTransformPosition(Hand);
        const FTransform ActorTransform=Player->GetActorTransform();
        if(T>=6.8f && T<7.8f)
        {
            const FVector L=ActorTransform.InverseTransformPosition(Mesh->GetBoneLocation(TEXT("upperarm_l")));
            const FVector R=ActorTransform.InverseTransformPosition(Mesh->GetBoneLocation(TEXT("upperarm_r")));
            SawInsideBank |= (L.Z-R.Z)*MirrorSign>2.f;
        }
        if(T>=16.5f && T<17.f)
        {
            const FVector L=ActorTransform.InverseTransformPosition(Mesh->GetBoneLocation(TEXT("upperarm_l")));
            const FVector R=ActorTransform.InverseTransformPosition(Mesh->GetBoneLocation(TEXT("upperarm_r")));
            SawLevelReturn |= FMath::Abs(L.Z-R.Z)<0.15f && Anim->FlightWeight>0.99f;
        }
        if(T>=0.7f && T<1.5f)
        {
            const FVector Left=ActorTransform.InverseTransformPosition(Mesh->GetBoneLocation(TEXT("upperarm_l")));
            const FVector Right=ActorTransform.InverseTransformPosition(Mesh->GetBoneLocation(TEXT("upperarm_r")));
            const FVector Chest=ActorTransform.InverseTransformPosition(Mesh->GetBoneLocation(TEXT("spine_05")));
            const FVector Hips=ActorTransform.InverseTransformPosition(Mesh->GetBoneLocation(TEXT("pelvis")));
            MaxNeutralShoulderGap=FMath::Max(MaxNeutralShoulderGap,float(FMath::Abs(Left.Z-Right.Z)));
            for(const TCHAR* Stem:{TEXT("upperarm"),TEXT("lowerarm"),TEXT("hand"),TEXT("thigh"),TEXT("calf"),TEXT("foot")})
            {
                FVector L=ActorTransform.InverseTransformPosition(Mesh->GetBoneLocation(FName(FString(Stem)+TEXT("_l"))));
                const FVector R=ActorTransform.InverseTransformPosition(Mesh->GetBoneLocation(FName(FString(Stem)+TEXT("_r"))));
                L.Y=-L.Y;MaxNeutralMirrorError=FMath::Max(MaxNeutralMirrorError,float(FVector::Dist(L,R)));
            }
            const FVector Axis=Chest-Hips;
            const float EntryPitch=FMath::Abs(FMath::RadiansToDegrees(FMath::Atan2(float(Axis.Z),float(Axis.Size2D()))));
            MaxEntryPitch=FMath::Max(MaxEntryPitch,EntryPitch);
            MinEntryPitch=FMath::Min(MinEntryPitch,EntryPitch);
            ++BalanceSamples;
        }
        if(!bEarlyCancel && T>=17.65f && T<17.8f && Anim->FlightWeight>0.99f && Anim->FlightBlend.Y>0.7f)
        {
            const FVector Forward=(Mesh->GetBoneLocation(TEXT("spine_05"))-Mesh->GetBoneLocation(TEXT("pelvis"))).GetSafeNormal();
            for(const TCHAR* Side:{TEXT("l"),TEXT("r")})
            {
                const FVector Arm=(Mesh->GetBoneLocation(FName(FString(TEXT("hand_"))+Side))-Mesh->GetBoneLocation(FName(FString(TEXT("upperarm_"))+Side))).GetSafeNormal();
                MaxFastPickupArmAngle=FMath::Max(MaxFastPickupArmAngle,FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(float(-FVector::DotProduct(Forward,Arm)),-1.f,1.f))));
            }
            ++FastPickupSamples;
        }
        TestFinite &= !Head.ContainsNaN() && !Hand.ContainsNaN() && !Flight->PoseBlend.ContainsNaN();
        if(bQuickBrake && T>=4.75f && T<4.9f && Anim->FlightBlend.Z>0.25f && Anim->FlightActionWeight>0.5f)
        {
            const FVector Axis=Mesh->GetBoneLocation(TEXT("spine_05"))-Mesh->GetBoneLocation(TEXT("pelvis"));
            const float Tilt=FMath::RadiansToDegrees(FMath::Atan2(float(Axis.Z),float(Axis.Size2D())));
            SawQuickBrakeResponse |= Tilt>6.f;
        }
        for(const FVector& Rotation:Anim->FlowRotations)TestFinite &= !Rotation.ContainsNaN() && Rotation.Size()<=FMath::DegreesToRadians(8.1f);
        if (T >= 0 && PrevT >= 0 && Dt < 0.1f)
        {
            MaxHandSpeed = FMath::Max(MaxHandSpeed, float(FVector::Dist(LocalHand, LastHand) / FMath::Max(Dt, 0.001f)));
            const float WeightStep = FMath::Abs(Anim->FlightWeight - LastWeight);
            MaxWeightStep = FMath::Max(MaxWeightStep, WeightStep);
            MaxWeightRate = FMath::Max(MaxWeightRate, WeightStep / Dt);
        }
        if (T >= 1.f && T < 1.5f) SawHover |= Anim->FlightWeight > 0.99f && Flight->PoseBlend.X < 0.1f;
        if (T >= 5.f && T < 6.f) SawFast |= Flight->PoseBlend.Y > 0.6f;
        if (T >= 14.f && T < 16.f) SawBrake |= Flight->PoseBlend.Z > 0.1f;
        if (T >= 14.1f && T < 15.5f)
        {
            MaxStopBrace=FMath::Max(MaxStopBrace,float(Flight->PoseBlend.Z));
            const FVector Axis=Mesh->GetBoneLocation(TEXT("spine_05"))-Mesh->GetBoneLocation(TEXT("pelvis"));
            const float Tilt=FMath::RadiansToDegrees(FMath::Atan2(float(Axis.Z),float(Axis.Size2D())));
            SawUprightBrace |= Flight->PoseBlend.Z>0.7f && Tilt>45.f;
        }
        if(!bVerticalRise && T>=2.f && T<3.5f && Anim->FlightWeight>0.99f)
        {
            const FVector Axis=ActorTransform.InverseTransformVectorNoScale(Mesh->GetBoneLocation(TEXT("spine_05"))-Mesh->GetBoneLocation(TEXT("pelvis"))).GetSafeNormal();
            if(bHadHoverEntryAxis)MaxHoverEntryRate=FMath::Max(MaxHoverEntryRate,FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(float(FVector::DotProduct(Axis,PreviousHoverEntryAxis)),-1.f,1.f)))/FMath::Max(Dt,0.001f));
            PreviousHoverEntryAxis=Axis;bHadHoverEntryAxis=true;
        }
        if(T>=16.5f && T<17.f && Anim->FlightWeight>0.99f)
        {
            const FVector Axis=Mesh->GetBoneLocation(TEXT("spine_05"))-Mesh->GetBoneLocation(TEXT("pelvis"));
            MinSettledHoverPitch=FMath::Min(MinSettledHoverPitch,FMath::RadiansToDegrees(FMath::Atan2(float(Axis.Z),float(Axis.Size2D()))));
        }
        if (T > 20.f) SawExit |= Anim->FlightWeight < 0.001f;
        if (T >= 0.f && T <= CaptureEnd && T >= NextShot)
        {
            static const TCHAR* BalanceBones[]={TEXT("pelvis"),TEXT("spine_05"),TEXT("head"),TEXT("upperarm_l"),TEXT("upperarm_r"),TEXT("lowerarm_l"),TEXT("lowerarm_r"),TEXT("hand_l"),TEXT("hand_r"),TEXT("thigh_l"),TEXT("thigh_r"),TEXT("calf_l"),TEXT("calf_r"),TEXT("foot_l"),TEXT("foot_r")};
            if(BalanceRows.IsEmpty())
            {
                FString Header=TEXT("t,turn,phase,weight,boost,action,pitch,yaw,roll,speed,maxaccel,fov,arm_length,camera_distance,camera_lag,view_fov,view_distance,brake,action_weight,velocity_x,velocity_y,velocity_z,actor_yaw,control_yaw,rear_x,rear_y,rear_z,knee_flow");
                for(const TCHAR* Bone:BalanceBones)Header+=FString::Printf(TEXT(",%s_x,%s_y,%s_z"),Bone,Bone,Bone);
                BalanceRows.Add(Header);
            }
            const FRotator Lean=(Mesh->GetRelativeRotation().Quaternion()*RestRotation.Inverse()).Rotator();
            FString Row=FString::Printf(TEXT("%.6f,%.6f,%.6f,%.6f,%.6f,%d,%.6f,%.6f,%.6f"),T,Anim->FlightTurn,Anim->FlightPhase,Anim->FlightWeight,float(Anim->FlightBlend.Y),Anim->FlightActionIndex,Lean.Pitch,Lean.Yaw,Lean.Roll);
            const UCameraComponent* Lens=Player->FindComponentByClass<UCameraComponent>();
            const USpringArmComponent* Arm=Player->FindComponentByClass<USpringArmComponent>();
            Row+=FString::Printf(TEXT(",%.6f,%.6f,%.6f,%.6f,%.6f,%.6f"),Player->GetVelocity().Size(),Move->MaxAcceleration,
                Lens?Lens->FieldOfView:0.f,Arm?Arm->TargetArmLength:0.f,
                Lens?FVector::Dist(Lens->GetComponentLocation(),Player->GetActorLocation()):0.f,Arm?Arm->CameraLagSpeed:0.f);
            Row+=FString::Printf(TEXT(",%.6f,%.6f"),PC->PlayerCameraManager?PC->PlayerCameraManager->GetFOVAngle():0.f,
                PC->PlayerCameraManager?FVector::Dist(PC->PlayerCameraManager->GetCameraLocation(),Player->GetActorLocation()):0.f);
            Row+=FString::Printf(TEXT(",%.6f,%.6f"),float(Anim->FlightBlend.Z),Anim->FlightActionWeight);
            const FVector ActualVelocity=Player->GetVelocity();
            Row+=FString::Printf(TEXT(",%.6f,%.6f,%.6f,%.6f,%.6f"),ActualVelocity.X,ActualVelocity.Y,ActualVelocity.Z,Player->GetActorRotation().Yaw,PC->GetControlRotation().Yaw);
            Row+=FString::Printf(TEXT(",%.6f,%.6f,%.6f,%.6f"),Anim->RearSway.WaistRotationCS.X,Anim->RearSway.WaistRotationCS.Y,Anim->RearSway.WaistRotationCS.Z,Anim->RearSway.KneeFlexion);
            for(const TCHAR* Bone:BalanceBones)
            {
                const FVector P=ActorTransform.InverseTransformPosition(Mesh->GetBoneLocation(FName(Bone)));
                Row+=FString::Printf(TEXT(",%.6f,%.6f,%.6f"),P.X,P.Y,P.Z);
            }
            BalanceRows.Add(Row);
            if (bCapture) FScreenshotRequest::RequestScreenshot(Dir / FString::Printf(TEXT("flight_%05d.png"), Shot), false, false);
            UE_LOG(LogTemp, Display, TEXT("FLIGHTREVIEW %05d t=%.3f dt=%.4f speed=%.1f flight=%.3f blends=%s hand=%s action=%d at=%.3f turn=%.3f maxaccel=%.1f accel=%.1f"),
                Shot, T, Dt, Player->GetVelocity().Size(), Anim->FlightWeight, *Flight->PoseBlend.ToString(), *LocalHand.ToString(),Anim->FlightActionIndex,Anim->FlightActionTime,Anim->FlightTurn,Move->MaxAcceleration,Move->GetCurrentAcceleration().Size());
            if(T>=0.7f && T<1.1f)
                for(int32 Limb=0;Limb<2;++Limb)
                {
                    const FRPGLimbContactResult Contact=Anim->GetContactResult(Limb);
                    UE_LOG(LogTemp,Display,TEXT("FLIGHTCONTACT t=%.3f limb=%d planes=%d applied=%s moved=%s simulated=%d"),T,Limb,Anim->ContactSets[Limb].Num,*Contact.Applied.ToString(),*Contact.Moved.ToString(),Mesh->IsSimulatingPhysics(Limb==0?TEXT("upperarm_r"):TEXT("upperarm_l")));
                }
            ++Shot; NextShot += 1.f/CaptureFPS;
        }
        LastHand = LocalHand; LastWeight = Anim->FlightWeight; PrevT = T;
        if (FPlatformTime::Seconds() - LastRealTime > 600.) { Test->AddError(TEXT("Flight review exceeded real-time timeout")); return true; }
        if (T < 40.f) return false;
        Test->TestTrue(TEXT("Neutral shoulders stay level"),BalanceSamples>0 && MaxNeutralShoulderGap<0.15f);
        Test->TestTrue(TEXT("Neutral flight limbs stay mirrored"),BalanceSamples>0 && MaxNeutralMirrorError<0.2f);
        if(bMovingEntry)Test->TestTrue(TEXT("Moving entry retains the flat flight silhouette"),BalanceSamples>0 && MaxEntryPitch<4.f);
        else Test->TestTrue(TEXT("Stationary hover is upright and distinct from flying"),BalanceSamples>0 && MinEntryPitch>75.f);
        Test->TestTrue(TEXT("Flight acceleration reaches actual movement physics"),SawFlightAcceleration);
        Test->TestTrue(TEXT("Boost acceleration reaches actual movement physics"),SawBoostAcceleration);
        if(bMovingEntry)Test->TestFalse(TEXT("Moving entry does not replay acceleration after reaching cruise"),SawDelayedMovingAcceleration);
        if(bBoostCycle)Test->TestTrue(TEXT("Boost can disengage and recover during a bank"),SawCycleNormal && SawCycleBoost);
        Test->TestTrue(TEXT("Body banks into the turn"),SawInsideBank);
        if(!bEarlyCancel)Test->TestTrue(TEXT("Shoulders return level after the turn"),SawLevelReturn);
        bool MovementDependsOnAbility=false;
        for(const FTickPrerequisite& Entry:Move->PrimaryComponentTick.GetPrerequisites())
            MovementDependsOnAbility |= Entry.PrerequisiteObject.Get()==Ability;
        Test->TestTrue(TEXT("Flight movement tick order restores on exit"),MovementDependsOnAbility==InitialMovementDependsOnAbility);
        Test->TestTrue(TEXT("Flight balance telemetry saved"),FFileHelper::SaveStringArrayToFile(BalanceRows,*(Dir/TEXT("flight_balance.csv"))));
        Test->AddInfo(FString::Printf(TEXT("Flight balance: neutral shoulder gap %.6f cm; entry pitch %.4f degrees; mirror sign %.0f"),MaxNeutralShoulderGap,MaxEntryPitch,MirrorSign));
        Test->TestTrue(TEXT("Flight poses remain finite"), TestFinite);
        Test->TestTrue(TEXT("Natural flight landing reaches the floor"),SawNaturalLanding);
        Test->TestTrue(TEXT("Low-altitude cancellation yields promptly to ground locomotion"),SawGroundHandoff);
        Test->TestTrue(TEXT("Speed camera widens and pulls back during boost"),SawSpeedCamera);
        if(bGameplayCamera)
        {
            Test->TestTrue(TEXT("Flight camera reaches the active player view"),SawActiveSpeedCamera);
            Test->TestTrue(TEXT("Active player FOV restores after flight"),PC->PlayerCameraManager && FMath::IsNearlyEqual(PC->PlayerCameraManager->GetFOVAngle(),InitialViewFOV,0.02f));
        }
        Test->TestTrue(TEXT("Ground acceleration restores after flight"),FMath::IsNearlyEqual(Move->MaxAcceleration,InitialAcceleration,0.01f));
        Test->AddInfo(FString::Printf(TEXT("Ground acceleration baseline %.2f; restored %.2f"),InitialAcceleration,Move->MaxAcceleration));
        if(const UCameraComponent* Lens=Player->FindComponentByClass<UCameraComponent>())Test->TestTrue(TEXT("Camera FOV restores after flight"),FMath::IsNearlyEqual(Lens->FieldOfView,InitialFOV,0.02f));
        if(const USpringArmComponent* Arm=Player->FindComponentByClass<USpringArmComponent>())Test->TestTrue(TEXT("Camera distance restores after flight and perspective switches"),FMath::IsNearlyEqual(Arm->TargetArmLength,InitialArmLength,0.05f));
        Test->TestTrue(TEXT("Authored takeoff transition plays"),SawTakeoff);
        Test->TestTrue(TEXT("Authored acceleration transition plays"),SawAcceleration);
        Test->TestTrue(TEXT("Authored boost transition plays"),SawBoostAction);
        Test->TestTrue(TEXT("Authored release transition plays"),SawRelease);
        Test->TestTrue(TEXT("Authored bank poses respond to turns"),SawBank);
        Test->TestTrue(TEXT("Ordinary-speed flight also banks"),SawSlowBank);
        Test->TestTrue(TEXT("First-person shadow mirrors flight pose and phase"), SawShadow);
        Test->TestTrue(TEXT("Root-motion traversal owns the pose"), SawTraversalYield);
        Test->TestTrue(TEXT("Flight resumes after root-motion traversal"), SawTraversalResume);
        Test->TestTrue(TEXT("Flight yields to combat and clears limb offsets"), SawCombatYield);
        Test->TestTrue(TEXT("Flight resumes after combat"), SawCombatResume);
        Test->TestTrue(TEXT("Rapid reentry settles and final exit clears the flight layer"), Anim->FlightWeight<0.001f && Anim->FlowBones.Num()==0);
        if(!bMovingEntry)Test->TestTrue(TEXT("Hover reached"), SawHover);
        Test->TestTrue(TEXT("Fast pose reached"), SawFast);
        if(!bEarlyCancel)Test->TestTrue(TEXT("Fast pickup streamlines without a delayed ordinary acceleration pose"),FastPickupSamples>0 && MaxFastPickupArmAngle<17.f);
        if(!bEarlyCancel)Test->TestTrue(TEXT("Braking pose reached"), SawBrake);
        if(!bEarlyCancel)
        {
            Test->TestTrue(TEXT("Releasing movement visibly braces against momentum"),MaxStopBrace>0.85f && SawUprightBrace);
            Test->TestTrue(TEXT("Braking settles into upright hover"),MinSettledHoverPitch>75.f && MinSettledHoverPitch<90.1f);
        }
        if(!bVerticalRise)Test->TestTrue(TEXT("Hover leans smoothly into forward flight"),bHadHoverEntryAxis && MaxHoverEntryRate<500.f);
        Test->AddInfo(FString::Printf(TEXT("Hover/stop: entry peak %.2f deg/s; brace %.3f; settled hover %.2f deg"),MaxHoverEntryRate,MaxStopBrace,MinSettledHoverPitch));
        if(bQuickBrake)Test->TestTrue(TEXT("Braking responds while the boost action is still playing"),SawQuickBrakeResponse);
        if(bVerticalRise)Test->TestTrue(TEXT("Straight vertical flight does not activate a bank pose"),VerticalSamples>0 && MaxVerticalTurn<0.02f);
        if(bVerticalRise)Test->TestTrue(TEXT("Vertical flight keeps the visible body facing the held look direction"),VerticalSamples>0 && MaxVerticalFacingError<3.f);
        if(bVerticalPause)Test->TestTrue(TEXT("Stopping vertical flight preserves the held visible heading"),VerticalPauseSamples>0 && MaxVerticalPauseFacingError<3.f);
        if(bBankYield && !bShortBankYield)
        {
            Test->TestTrue(TEXT("Bank interruption releases the flight pose and flow"),BankYieldSamples>0 && MaxBankYieldPoseWeight<0.01f && bBankYieldFlowClear);
            Test->TestTrue(TEXT("Bank interruption releases whole-mesh lean with the flight pose"),BankYieldSamples>0 && MaxBankYieldBodyAngle<3.f);
            Test->TestTrue(TEXT("Short bank interruption resumes without a mesh rotation snap"),bHadBankResumeRotation && MaxBankResumeAngularRate<360.f);
            Test->TestTrue(TEXT("Flight resumes after a banked interruption"),SawBankYieldResume);
            Test->AddInfo(FString::Printf(TEXT("Bank yield: residual mesh %.3f deg, resume peak %.1f deg/s"),MaxBankYieldBodyAngle,MaxBankResumeAngularRate));
        }
        if(bShortBankYield)
        {
            // The brief release itself can rotate faster than a full release;
            // this bound still rejects a one-frame restoration of stale lean.
            Test->TestTrue(TEXT("Partial bank interruption resumes without a mesh rotation snap"),bHadBankResumeRotation && MaxBankResumeAngularRate<600.f);
            Test->TestTrue(TEXT("Flight resumes after a partial bank interruption"),SawBankYieldResume);
            Test->AddInfo(FString::Printf(TEXT("Short bank yield: resume peak %.1f deg/s"),MaxBankResumeAngularRate));
        }
        if(bBankReentry)
        {
            Test->TestTrue(TEXT("Rapid banked reentry keeps the evaluated torso continuous"),bHadBankReentryAxis && MaxBankReentryTorsoRate<500.f);
            Test->TestTrue(TEXT("Rapid banked reentry keeps the hip trajectory continuous"),bHadBankReentryAxis && MaxBankReentryHipSpeed<100.f);
            Test->TestTrue(TEXT("Moving bank reentry returns to fast flight without replaying full takeoff"),SawBankReentryFinish);
            Test->AddInfo(FString::Printf(TEXT("Bank reentry: torso peak %.1f deg/s, hip peak %.1f cm/s"),MaxBankReentryTorsoRate,MaxBankReentryHipSpeed));
        }
        if(bSpiral)
        {
            Test->TestTrue(TEXT("Climbing turn combines upward pitch with the matching bank"),SawSpiralClimb);
            Test->TestTrue(TEXT("Diving turn combines downward pitch with the matching bank"),SawSpiralDive);
        }
        Test->TestTrue(TEXT("Flight exits to locomotion"), SawExit);
        Test->TestTrue(TEXT("Flight blend changes continuously"), MaxWeightRate < 15.f);
        Test->TestTrue(TEXT("Body restores after flight"), Mesh->GetRelativeLocation().Equals(RestLocation,0.1f) && Mesh->GetRelativeRotation().Quaternion().Equals(RestRotation,0.002f));
        Test->TestFalse(TEXT("Physics follow-through stays disabled"), Flight->bFollowThrough);
        Test->TestTrue(TEXT("Rear sway clears when flight ends"),Anim->RearSway.WaistRotationCS.IsNearlyZero(.0001f) && Anim->RearSway.KneeFlexion<.0001f);
        Test->AddInfo(FString::Printf(TEXT("Flight samples %d; max hand speed %.1f cm/s, max blend step %.3f, rate %.3f/s; %s"),Shot,MaxHandSpeed,MaxWeightStep,MaxWeightRate,*Dir));
        return true;
    }
private:
    FAutomationTestBase* Test;
    TWeakObjectPtr<ACameraActor> Camera;
    TWeakObjectPtr<UAnimMontage> RootMontage;
    TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> Input;
    TWeakObjectPtr<UInputAction> MoveAction;
    FString Dir;
    TArray<FString> BalanceRows;
    FVector InitialLocation,RestLocation,LastHand;
    FQuat RestRotation;
    float GroundZ=0,ReviewAngle=65.f,CaptureFPS=30.f,CaptureEnd=23.f,Start=-1,PrevT=-1,NextShot=0,LastWeight=0,MaxWeightStep=0,MaxWeightRate=0,MaxHandSpeed=0;
    FIntPoint StableViewportSize=FIntPoint::ZeroValue;
    double ViewportStableSince=-1.;
    int32 ViewportWaitFrames=0;
    bool bViewportWarmup=false;
    double LastRealTime=0;
    int32 Waiting=0,Shot=0,SavedBlur=0,SavedAA=0;
    int32 GroundWarmupFrames=0;
    float InitialAcceleration=0.f,InitialFOV=0.f,InitialArmLength=0.f;
    float InitialViewFOV=0.f,InitialViewDistance=0.f;
    float MirrorSign=1.f,ReviewHeading=0.f,MaxNeutralShoulderGap=0.f,MaxEntryPitch=0.f,MaxNeutralMirrorError=0.f;
    int32 BalanceSamples=0;
    float MinEntryPitch=180.f,MaxStopBrace=0.f,MinSettledHoverPitch=180.f;
    bool SawUprightBrace=false;
    bool bHadHoverEntryAxis=false;
    FVector PreviousHoverEntryAxis=FVector::ZeroVector;
    float MaxHoverEntryRate=0.f;
    int32 FastPickupSamples=0;
    float MaxFastPickupArmAngle=0.f;
    int32 VerticalSamples=0;
    float MaxVerticalTurn=0.f;
    float MaxVerticalFacingError=0.f;
    int32 VerticalPauseSamples=0;
    float MaxVerticalPauseFacingError=0.f;
    bool bBankYield=false,bShortBankYield=false,bBankYieldFlowClear=true,bHadBankResumeRotation=false,SawBankYieldResume=false;
    int32 BankYieldSamples=0;
    float MaxBankYieldPoseWeight=0.f,MaxBankYieldBodyAngle=0.f,MaxBankResumeAngularRate=0.f;
    FQuat PreviousBankResumeRotation=FQuat::Identity;
    bool bBankReentry=false,bRepeatBankReentry=false,bBankReentryStopped=false,bBankReentryStarted=false,bSecondBankStop=false,bSecondBankStart=false,bHadBankReentryAxis=false,SawBankReentryFinish=false;
    FVector PreviousBankReentryAxis=FVector::ZeroVector;
    float MaxBankReentryTorsoRate=0.f;
    FVector PreviousBankReentryHip=FVector::ZeroVector;
    float MaxBankReentryHipSpeed=0.f;
    bool bSpiral=false,SawSpiralClimb=false,SawSpiralDive=false;
    bool bGameplayCamera=false,bFloorCreated=false,SawNaturalLanding=false,SawSpeedCamera=false,SawActiveSpeedCamera=false;
    bool bNormalInput=false,bStrafeInput=false,bEarlyCancel=false;
    bool bBoostCycle=false,bCycleDown=false,bCycleUp=false,SawCycleNormal=false,SawCycleBoost=false;
    bool bMovingEntry=false,SawDelayedMovingAcceleration=false,bQuickBrake=false,SawQuickBrakeResponse=false,bGroundEntry=false,bVerticalRise=false,bVerticalPause=false;
    bool InitialCameraLag=false,InitialCameraRotationLag=false,bCameraLagRestored=false,SawSlowBank=false;
    bool bSavedVisualSettings=false;
    bool SawTakeoff=false,SawAcceleration=false,SawBoostAction=false,SawRelease=false,SawBank=false;
    bool bBoosted=false,bCancelled=false,bLanding=false,bCapture=false,bWantCapture=true;
    bool bRestarted=false,bRapidStop=false,bRapidStart=false,bFinalStop=false,SawCombatYield=false,SawCombatResume=false;
    bool bFirstPersonStarted=false,bFirstPersonStopped=false,bPerspectiveRestored=false,SawShadow=false;
    bool bLowFlightStarted=false,bLowFlightStopped=false,SawGroundHandoff=false;
    bool SawFlightAcceleration=false,SawBoostAcceleration=false;
    bool InitialMovementDependsOnAbility=false,SawInsideBank=false,SawLevelReturn=false;
    bool bRootStarted=false,bRootStopped=false,bRootExit=false,SawTraversalYield=false,SawTraversalResume=false;
    bool TestFinite=true,SawHover=false,SawFast=false,SawBrake=false,SawExit=false;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightReviewTest,"Visual.FlightReview",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FFlightReviewTest::RunTest(const FString& Parameters)
{
    ULevelEditorPlaySettings* Play=NewObject<ULevelEditorPlaySettings>();
    Play->NewWindowWidth=1280;Play->NewWindowHeight=752;
    Play->LastExecutedPlayModeType=PlayMode_InEditorFloating;
    FRequestPlaySessionParams Session;
    Session.WorldType=EPlaySessionWorldType::PlayInEditor;
    Session.EditorPlaySettings=Play;
    GEditor->RequestPlaySession(Session);
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
    ADD_LATENT_AUTOMATION_COMMAND(FFlightReviewCommand(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightTransitionTest,"RPG.Flight.Transitions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FFlightTransitionTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
    ADD_LATENT_AUTOMATION_COMMAND(FFlightReviewCommand(this,false));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

#endif
