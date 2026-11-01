#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "RPGCharacterStatsComponent.h"
#include "RPGCombatStateComponent.h"
#include "RPGCombatAimComponent.h"
#include "RPGCameraModeComponent.h"
#include "RPGAnimInstance.h"
#include "RPGStrikeContactComponent.h"
#include "Animation/AnimMontage.h"
#include "RPGControllerInputComponent.h"
#include "RPGHitOutcomeComponent.h"
#include "RPGMeleeHitComponent.h"
#include "RPGNpcCharacter.h"
#include "RPGPhysicsReactionComponent.h"
#include "RPGPowerArena.h"
#include "RPGPrototypeAnimationComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "InputAction.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "HAL/FileManager.h"
#include "Components/CapsuleComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

namespace
{
struct FPunchCase { const TCHAR* Bone; float Distance; float Strength; bool bMiss; };
const FPunchCase Cases[] = {
    {TEXT("spine_05"), 90.f, 5.f, false},
    {TEXT("spine_05"), 100.f, 12.f, false},
    {TEXT("head"), 105.f, 12.f, false},
    {TEXT("head"), 100.f, 5.f, false},
    {TEXT("spine_05"), 350.f, 5.f, true},
};

// Actual animation/contact/Power/NPC path, including the first quarter-second of body motion.
// Each case gets fresh PIE actors; no synthetic hit or reused post-get-up pose.
class FNpcPunchImpactCommand : public IAutomationLatentCommand
{
public:
    FNpcPunchImpactCommand(FAutomationTestBase* InTest, int32 InCase) : Test(InTest), Index(InCase), CaseData(Cases[InCase]) {}
    FNpcPunchImpactCommand(FAutomationTestBase* InTest, int32 InCase, const FPunchCase& Spec, bool InLeft, bool InFirst, bool InFollow=true, float InFPS=0.f)
        : Test(InTest), Index(InCase), CaseData(Spec), bLeft(InLeft), bFirst(InFirst), bFollow(InFollow), ForcedFPS(InFPS) {}
    ~FNpcPunchImpactCommand() { if(bClockSaved){FApp::SetUseFixedTimeStep(bSavedFixed);FApp::SetFixedDeltaTime(SavedDt);} }
    bool Update() override
    {
        UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
        if(!bClockSaved)
        {
            const float FPS=ForcedFPS>0.f?ForcedFPS:FCString::Atof(*FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_CHIN_TEST_FPS")));
            if(FPS>0){bClockSaved=true;bSavedFixed=FApp::UseFixedTimeStep();SavedDt=FApp::GetFixedDeltaTime();FApp::SetUseFixedTimeStep(true);FApp::SetFixedDeltaTime(1./FPS);}
        }
        // StartPIE/EndPlay are deferred. Never keep setup or an actor from the preceding world while the new
        // instance is still starting (the old actor can remain a valid UObject until garbage collection).
        if (World && FixtureWorld.Get() != World)
        {
            FixtureWorld = World;
            Npc.Reset();
            Phase = 0; Started = 0.f; PeakSpin = 0.f; PeakBodies = 0; bHaveFist=false; Rows.Reset();
        }
        ACharacter* Player = World ? UGameplayStatics::GetPlayerCharacter(World, 0) : nullptr;
        APlayerController* PC = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
        auto* State = Player ? Player->FindComponentByClass<URPGCombatStateComponent>() : nullptr;
        auto* Melee = Player ? Player->FindComponentByClass<URPGMeleeHitComponent>() : nullptr;
        if (!Npc.IsValid() && World) Npc = RPGPowerArena::FindReferenceNpc(World);
        if (!Player || !PC || !PC->PlayerCameraManager || !State || !Melee || !Npc.IsValid())
        { if (++Wait < 600) return false; return Fail(TEXT("missing PIE actors/components")); }
        auto* N = Npc.Get();
        const FPunchCase& C = CaseData;
        const float Now = World->GetTimeSeconds();
        if (Phase == 0)
        {
            if (Now < 1.5f) return false;
            const FVector Base = N->GetActorLocation();
            N->GetCharacterMovement()->StopMovementImmediately();
            N->SetActorRotation(FRotator(0, 180, 0), ETeleportType::ResetPhysics);
            if (FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_CHIN_ALIGN_TARGET_CONTROL")) == TEXT("1"))
                if (AController* NC = N->GetController()) NC->SetControlRotation(N->GetActorRotation());
            Player->SetActorLocationAndRotation(Base - FVector(C.Distance, 0, 0), FRotator::ZeroRotator, false, nullptr, ETeleportType::ResetPhysics);
            Player->GetCharacterMovement()->StopMovementImmediately();
            Player->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            if(auto* Mode=Player->FindComponentByClass<URPGCameraModeComponent>();Mode && Mode->bIsFirstPerson!=bFirst)Mode->TogglePerspective();
            N->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            if (auto* Stats = URPGCharacterStatsComponent::FindStats(Player)) Stats->SetStatBaseByName(TEXT("Strength"), C.Strength);
            N->HitOutcome->TestSeed = 42;
            if(FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_CHIN_TRACE_NO_CONTACT"))==TEXT("1"))Melee->SetComponentTickEnabled(false);
            const FVector A=Player->GetMesh()->GetBoneLocation(bLeft?TEXT("upperarm_l"):TEXT("upperarm_r"));
            const FVector B=Player->GetMesh()->GetBoneLocation(bLeft?TEXT("lowerarm_l"):TEXT("lowerarm_r"));
            const FVector D=Player->GetMesh()->GetBoneLocation(bLeft?TEXT("hand_l"):TEXT("hand_r"));
            ArmUpper=FVector::Dist(A,B);ArmLower=FVector::Dist(B,D);
            CountBefore = Melee->ConfirmedHitCount;
            Started = Now; Phase = 1;
            return false;
        }
        // Keep the reticle on the chin until contact, matching CrosshairAim. A fixed control rotation is not a fixed
        // reticle point when the shoulder camera moves as the body squares up. Fixed-view captures remain available
        // separately; this fixture isolates delivery with the requested body part actually under the reticle.
        if(Phase>0)
        {
            const FVector A=Player->GetMesh()->GetBoneLocation(bLeft?TEXT("upperarm_l"):TEXT("upperarm_r"));
            const FVector B=Player->GetMesh()->GetBoneLocation(bLeft?TEXT("lowerarm_l"):TEXT("lowerarm_r"));
            const FVector D=Player->GetMesh()->GetBoneLocation(bLeft?TEXT("hand_l"):TEXT("hand_r"));
            MaxArmError=FMath::Max(MaxArmError,FMath::Max(FMath::Abs(float(FVector::Dist(A,B))-ArmUpper),FMath::Abs(float(FVector::Dist(B,D))-ArmLower)));
            PeakElbow=FMath::Max(PeakElbow,FMath::RadiansToDegrees(float(FMath::Acos(FMath::Clamp(FVector::DotProduct((B-A).GetSafeNormal(),(D-B).GetSafeNormal()),-1.,1.)))));
        }
        if (Phase < 2 || (Phase==2 && bFollow && FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_CHIN_FIXED_VIEW"))!=TEXT("1")))
        {
            const FVector Aim = N->GetMesh()->GetBoneLocation(C.Bone) + (FName(C.Bone) == TEXT("head") ? FVector(0, 0, -6) : FVector::ZeroVector);
            PC->SetControlRotation((Aim - PC->PlayerCameraManager->GetCameraLocation()).Rotation());
        }
        if (Phase == 1)
        {
            if (Now - Started < .8f) return false;
            Test->AddInfo(FString::Printf(TEXT("PUNCHFIXTURE case=%d NPC actorYaw=%.1f controllerYaw=%.1f head=%s"),Index,N->GetActorRotation().Yaw,N->GetControlRotation().Yaw,*N->GetMesh()->GetBoneLocation(TEXT("head")).ToCompactString()));
            ChinAtPress=N->GetMesh()->GetBoneLocation(TEXT("head"))+FVector(0,0,-6);
            Started = Now; Phase = 2;
        }
        if (Phase == 2)
        {
            Trace(Player, N, PC, State, Melee, Now);
            const FVector Wrist=Player->GetMesh()->GetBoneLocation(bLeft?TEXT("hand_l"):TEXT("hand_r"));
            const FVector Knuckle=Player->GetMesh()->GetBoneLocation(bLeft?TEXT("middle_01_l"):TEXT("middle_01_r"));
            const FVector Center=FMath::Lerp(Wrist,Knuckle,.55f);
            if(bHaveFist && Now-Started>=.05f && Now-Started<=.16f)
                PreloadPeakSpeed=FMath::Max(PreloadPeakSpeed,float(FVector::Dist(Knuckle,LastKnuckle))/World->GetDeltaSeconds());
            if(!bHaveFist){LastCenter=Center;LastKnuckle=Knuckle;bHaveFist=true;}
            auto* Input = Player->FindComponentByClass<URPGControllerInputComponent>();
            auto* Action = Input ? FindObject<UInputAction>(Input, bLeft?TEXT("IA_Defense"):TEXT("IA_RightAttack")) : nullptr;
            auto* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
            if (!Action || !Subsystem) return Fail(TEXT("Enhanced Input action unavailable"));
            if (Now - Started < .08f) Subsystem->InjectInputVectorForAction(Action, FVector(1, 0, 0), {}, {});
            if (Melee->ConfirmedHitCount > CountBefore)
            {
                if (C.bMiss) return Fail(TEXT("out-of-reach punch confirmed a hit"));
                const FRPGMeleeHitInfo& H = Melee->LastHit;
                if (H.HitActor != N) return Fail(TEXT("hit a different actor"));
                const float Into = FVector::DotProduct(H.StrikeDirection, H.ImpactNormal);
                const float SurfaceGap=FMath::Min(FVector::Dist(H.ImpactLocation,FMath::ClosestPointOnSegment(H.ImpactLocation,LastCenter,Center)),
                    FVector::Dist(H.ImpactLocation,FMath::ClosestPointOnSegment(H.ImpactLocation,LastKnuckle,Knuckle)));
                Test->TestTrue(FString::Printf(TEXT("Visible fist reaches the confirmed surface (%.1f cm)"),SurfaceGap),SurfaceGap<7.f);
                Test->TestTrue(TEXT("Contact has incoming motion"), Into < -.05f && H.LimbSpeed > 150.f && H.LimbSpeed < 3000.f);
                Test->TestTrue(TEXT("Standard punch retains standard intent"), !H.bCharged && H.ChargePercent == 0.f);
                if(FName(C.Bone)==TEXT("head"))
                {
                    const FString Bone=H.HitBone.ToString();
                    const float Error=FVector::Dist(H.ImpactLocation,ChinAtPress);
                    Test->TestTrue(FString::Printf(TEXT("Chin aim connects to head/neck within 15 cm (bone %s, error %.1f cm)"),*Bone,Error),
                        (Bone.Contains(TEXT("head")) || Bone.Contains(TEXT("neck"))) && Error<15.f);
                }
                Test->AddInfo(FString::Printf(TEXT("PUNCHIMPACT case=%d bone=%s speed=%.1f normalDot=%.3f reaction=%s"), Index, *H.HitBone.ToString(), H.LimbSpeed, Into, *UEnum::GetDisplayValueAsText(N->LastPowerResult.Reaction).ToString()));
                CountAtHit = Melee->ConfirmedHitCount;
                SaveTrace();
                Started = Now; Phase = 3;
            }
            else if (Now - Started > 1.2f)
            {
                SaveTrace();
                if (C.bMiss) { CheckArm();return true; }
                Test->AddInfo(FString::Printf(TEXT("PUNCHIMPACT miss player=%s target=%s fist=%s aim=%s"),
                    *Player->GetActorLocation().ToCompactString(), *N->GetActorLocation().ToCompactString(),
                    *Player->GetMesh()->GetBoneLocation(bLeft?TEXT("middle_01_l"):TEXT("middle_01_r")).ToCompactString(), *PC->GetControlRotation().ToCompactString()));
                return Fail(TEXT("punch never hit"));
            }
            LastCenter=Center;LastKnuckle=Knuckle;
            return false;
        }
        int32 Simulating = 0;
        for (const FBodyInstance* Body : N->GetMesh()->Bodies)
            if (Body && Body->IsInstanceSimulatingPhysics())
            {
                ++Simulating;
                const float Spin = FMath::RadiansToDegrees(float(Body->GetUnrealWorldAngularVelocityInRadians().Size()));
                if (!FMath::IsFinite(Spin)) return Fail(TEXT("non-finite body velocity"));
                PeakSpin = FMath::Max(PeakSpin, Spin);
            }
        PeakBodies = FMath::Max(PeakBodies, Simulating);
        if (Now - Started < .25f) return false;
        Test->TestEqual(TEXT("One confirmation per target per strike"), Melee->ConfirmedHitCount, CountAtHit);
        const bool bLaunch = C.Strength >= 12.f;
        Test->TestTrue(TEXT("Expected standing or whole-body response executed"), bLaunch ? PeakBodies == 23 : PeakBodies > 0 && PeakBodies < 23);
        // This fixture previously produced 40,000+ deg/s hands. A diagnostic guard for these controlled punches,
        // not a gameplay angular-speed clamp or an upper bound on fantasy Strength.
        Test->TestTrue(TEXT("No explosive transition spin in controlled fixture"), PeakSpin < 6000.f);
        CheckArm();
        Test->AddInfo(FString::Printf(TEXT("PUNCHIMPACT case=%d peakSpin=%.1f deg/s simBodies=%d"), Index, PeakSpin, PeakBodies));
        return true;
    }
private:
    void CheckArm()
    {
        Test->TestTrue(TEXT("No spurious pre-load fist lurch on release"),PreloadPeakSpeed<2500.f);
        Test->AddInfo(FString::Printf(TEXT("PUNCHCONTINUITY case=%d preloadPeak=%.1f cm/s"),Index,PreloadPeakSpeed));
        Test->TestTrue(FString::Printf(TEXT("Arm lengths remain unchanged (%.4f cm)"),MaxArmError),MaxArmError<.05f);
        Test->TestTrue(FString::Printf(TEXT("Elbow remains within anatomical bend (%.1f degrees)"),PeakElbow),PeakElbow<145.5f);
        Test->AddInfo(FString::Printf(TEXT("PUNCHARM case=%d lengthError=%.5f cm maxFlex=%.2f deg"),Index,MaxArmError,PeakElbow));
    }
    void Trace(ACharacter* Player, ARPGNpcCharacter* N, APlayerController* PC, URPGCombatStateComponent* State, URPGMeleeHitComponent* Melee, float Now)
    {
        const FString Dir = FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_CHIN_TRACE_DIR"));
        if (Dir.IsEmpty()) return;
        if (Rows.IsEmpty()) Rows = TEXT("time,phase,window,impact_time,camera_x,camera_y,camera_z,control_pitch,control_yaw,actor_yaw,chin_x,chin_y,chin_z,knuckle_x,knuckle_y,knuckle_z,aim_x,aim_y,aim_z,aim_on_target,raw_yaw,raw_pitch,shown_yaw,shown_pitch,ray_chin_gap,ray_bone,hit_count,hit_bone,hit_x,hit_y,hit_z,montage_time,natural_x,natural_y,natural_z,pose_x,pose_y,pose_z,aim_yaw,aim_pitch,eye_pivot,pivot_x,pivot_y,pivot_z\n");
        const FVector Eye = PC->PlayerCameraManager->GetCameraLocation(), Look = PC->GetControlRotation().Vector();
        const FVector Chin = N->GetMesh()->GetBoneLocation(TEXT("head")) + FVector(0,0,-6);
        const FVector Fist = Player->GetMesh()->GetBoneLocation(bLeft?TEXT("middle_01_l"):TEXT("middle_01_r"));
        FHitResult Under; FCollisionQueryParams Params(SCENE_QUERY_STAT(RPGChinTrace), false, Player);
        for (int32 K=0; K<4; ++K)
        {
            if (!Player->GetWorld()->LineTraceSingleByChannel(Under, Eye, Eye+Look*2000.f, ECC_Visibility, Params)) break;
            if (!Cast<UCapsuleComponent>(Under.GetComponent())) break;
            Params.AddIgnoredComponent(Under.GetComponent());
        }
        const FVector Aim = State->AttackAimPoint;
        const auto& H = Melee->LastHit;
        const auto* Anim=Cast<URPGAnimInstance>(Player->GetMesh()->GetAnimInstance());
        FVector Natural,Unused;
        if(auto* Animation=Player->FindComponentByClass<URPGPrototypeAnimationComponent>())Animation->GetFilmedStrikePoint(Natural,Unused);
        float MontageTime=-1;
        if(Anim)for(const auto* M:Anim->MontageInstances)if(M && M->IsActive() && !M->IsStopped())MontageTime=M->GetPosition();
        const FVector PoseCS=Player->GetMesh()->GetComponentTransform().InverseTransformPosition(Fist);
        const FVector PivotCS=Anim?Anim->PitchPivot:FVector::ZeroVector;
        if(Now-Started>=.18f && Now-Started<=.36f)
        {
            const auto* M=Player->GetMesh();
            const FVector Upper=M->GetBoneLocation(bLeft?TEXT("upperarm_l"):TEXT("upperarm_r"));
            const FVector Lower=M->GetBoneLocation(bLeft?TEXT("lowerarm_l"):TEXT("lowerarm_r"));
            const FVector Wrist=M->GetBoneLocation(bLeft?TEXT("hand_l"):TEXT("hand_r"));
            const FVector Center=FMath::Lerp(Wrist,Fist,.55f);
            const auto* Contact=Player->FindComponentByClass<URPGStrikeContactComponent>();
            UPrimitiveComponent* Target=nullptr;FName Bone;FVector Point,Normal;
            const bool Touch=Contact && Contact->GetContact(State->StrikeLimb,Target,Bone,Point,Normal);
            const FVector Travel=Contact?Contact->GetAnimatedTravel(State->StrikeLimb):FVector::ZeroVector;
            float D2=0;FVector Closest;
            const auto* HeadBody=N->GetMesh()->GetBodyInstance(TEXT("head"));
            const float HeadGap=HeadBody && HeadBody->GetSquaredDistanceToBody(Center,D2,Closest)?FMath::Sqrt(D2)-4.5f:999;
            const FVector Goal=Anim?M->GetComponentTransform().TransformPosition(Anim->FilmedAim.DeliveryPoint):FVector::ZeroVector;
            Test->AddInfo(FString::Printf(TEXT("PUNCHPROBE case=%d t=%.3f window=%d touch=%d bone=%s speed=%.1f into=%.1f knuckleInto=%.1f headGap=%.1f shoulderGoal=%.1f arm=%.1f centerGoal=%.1f corr=%.1f weight=%.2f rootYaw=%.1f fist=%s goal=%s"),Index,Now-Started,State->bStrikeHitWindowOpen,Touch,*Bone.ToString(),Travel.Size()/Player->GetWorld()->GetDeltaSeconds(),Touch?FVector::DotProduct(Travel/Player->GetWorld()->GetDeltaSeconds(),-Normal):0,
                Touch?FVector::DotProduct(Contact->GetAnimatedTravel(State->StrikeLimb,1)/Player->GetWorld()->GetDeltaSeconds(),-Normal):0,
                HeadGap,FVector::Dist(Upper,Goal),FVector::Dist(Upper,Lower)+FVector::Dist(Lower,Wrist),FVector::Dist(Center,Goal),Contact?Contact->GetCorrection(State->StrikeLimb).Size():0,Anim?Anim->FilmedAim.DeliveryWeight:0,M->GetSocketTransform(TEXT("root"),RTS_Component).Rotator().Yaw,*Center.ToCompactString(),*Goal.ToCompactString()));
        }
        Rows += FString::Printf(TEXT("%.5f,%d,%d,%.5f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%s,%d,%s,%.4f,%.4f,%.4f,%.5f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%.4f,%.4f,%.4f\n"),
            Now-Started, int32(State->AttackPhase), State->bStrikeHitWindowOpen, State->StrikeTimeFromImpact,
            Eye.X,Eye.Y,Eye.Z,PC->GetControlRotation().Pitch,PC->GetControlRotation().Yaw,Player->GetActorRotation().Yaw,
            Chin.X,Chin.Y,Chin.Z,Fist.X,Fist.Y,Fist.Z,Aim.X,Aim.Y,Aim.Z,State->bAttackAimOnTarget,
            State->AttackAimYaw,State->AttackAimPitch,State->AttackAimVisualYaw,State->AttackAimVisualPitch,
            FVector::CrossProduct(Chin-Eye,Look).Size(),*Under.BoneName.ToString(),Melee->ConfirmedHitCount,*H.HitBone.ToString(),H.ImpactLocation.X,H.ImpactLocation.Y,H.ImpactLocation.Z,MontageTime,Natural.X,Natural.Y,Natural.Z,PoseCS.X,PoseCS.Y,PoseCS.Z,
            Anim?Anim->AimYaw:0,Anim?Anim->AimPitch:0,Anim&&Anim->bPitchAboutPivot,PivotCS.X,PivotCS.Y,PivotCS.Z);
    }
    void SaveTrace()
    {
        const FString Dir = FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_CHIN_TRACE_DIR"));
        if (Dir.IsEmpty() || Rows.IsEmpty()) return;
        IFileManager::Get().MakeDirectory(*Dir, true);
        FFileHelper::SaveStringToFile(Rows, *(Dir / FString::Printf(TEXT("case_%d.csv"), Index)));
    }
    bool Fail(const TCHAR* Why) { Test->AddError(FString::Printf(TEXT("PUNCHIMPACT case=%d: %s"), Index, Why)); return true; }
    FAutomationTestBase* Test;
    int32 Index, Wait = 0, Phase = 0, CountBefore = 0, CountAtHit = 0, PeakBodies = 0;
    float Started = 0.f, PeakSpin = 0.f,ArmUpper=0,ArmLower=0,MaxArmError=0,PeakElbow=0,PreloadPeakSpeed=0;
    TWeakObjectPtr<ARPGNpcCharacter> Npc;
    TWeakObjectPtr<UWorld> FixtureWorld;
    FString Rows;
    FVector ChinAtPress=FVector::ZeroVector,LastCenter=FVector::ZeroVector,LastKnuckle=FVector::ZeroVector;
    bool bHaveFist=false;
    FPunchCase CaseData;
    bool bLeft=false,bFirst=false,bFollow=true,bClockSaved=false,bSavedFixed=false;
    float ForcedFPS=0.f;
    double SavedDt=0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNpcPunchImpactTest, "RPG.Npc.PunchImpact", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRPGNpcPunchImpactTest::RunTest(const FString& Parameters)
{
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Cases); ++Index)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.1f));
        ADD_LATENT_AUTOMATION_COMMAND(FNpcPunchImpactCommand(this, Index));
        ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNpcChinTargetingTest, "RPG.Npc.ChinTargeting", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRPGNpcChinTargetingTest::RunTest(const FString&)
{
    int32 Index=100;
    for(const bool bFirst:{false,true})
        for(const bool bLeft:{false,true})
            for(const float Distance:{90.f,100.f,105.f,140.f})
            {
                const FString Filter=FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_CHIN_CASE"));
                if(!Filter.IsEmpty() && Index!=FCString::Atoi(*Filter)){++Index;continue;}
                const FPunchCase Spec{TEXT("head"),Distance,5.f,Distance>105.f};
                ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
                ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.1f));
                ADD_LATENT_AUTOMATION_COMMAND(FNpcPunchImpactCommand(this,Index++,Spec,bLeft,bFirst));
                ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
            }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNpcChinReticleHoldTest, "RPG.Npc.ChinReticleHold", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRPGNpcChinReticleHoldTest::RunTest(const FString&)
{
    int32 Index=200;
    for(const bool bFirst:{false,true})
        for(const bool bLeft:{false,true})
            for(const float Distance:{90.f,100.f,105.f,140.f})
            {
                const FPunchCase Spec{TEXT("head"),Distance,5.f,Distance>105.f};
                ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
                ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.1f));
                ADD_LATENT_AUTOMATION_COMMAND(FNpcPunchImpactCommand(this,Index++,Spec,bLeft,bFirst,false));
                ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
            }
    return true;
}

// The close first-person left overhand exposed a rare palm/knuckle velocity mismatch. Repeat fresh real-input
// landings through varied idle poses at the low-frame-rate boundary, alternating held and tracking reticles.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNpcChinCloseRepeatTest, "RPG.Npc.ChinCloseRepeat", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRPGNpcChinCloseRepeatTest::RunTest(const FString&)
{
    const FPunchCase Spec{TEXT("head"),90.f,5.f,false};
    for(int32 Repeat=0;Repeat<24;++Repeat)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.1f));
        ADD_LATENT_AUTOMATION_COMMAND(FNpcPunchImpactCommand(this,300+Repeat,Spec,true,true,Repeat%2==0,30.f));
        ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    }
    return true;
}

#endif
