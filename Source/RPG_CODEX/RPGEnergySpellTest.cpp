// Spell integration and visual review in temporary PIE only. Never saves the level.
#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "RPGEnergySpellComponent.h"
#include "RPGEnergyOrb.h"
#include "RPGAnimInstance.h"
#include "RPGCombatTestDummy.h"
#include "RPGCombatStateComponent.h"
#include "RPGControllerInputComponent.h"
#include "RPGMovementAbilityComponent.h"
#include "RPGPhysicsReactionComponent.h"
#include "RPGCharacterStatsComponent.h"
#include "RPGCameraModeComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Editor.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UnrealClient.h"

namespace
{
class FEnergyReview : public IAutomationLatentCommand
{
public:
    FEnergyReview(FAutomationTestBase* InTest,bool Visual):Test(InTest),bVisual(Visual) {}
    bool Update() override
    {
        UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
        ACharacter* Player=World?UGameplayStatics::GetPlayerCharacter(World,0):nullptr;
        auto* PC=Player?Cast<APlayerController>(Player->GetController()):nullptr;
        auto* Spells=Player?Player->FindComponentByClass<URPGEnergySpellComponent>():nullptr;
        auto* State=Player?Player->FindComponentByClass<URPGCombatStateComponent>():nullptr;
        auto* Flight=Player?Player->FindComponentByClass<URPGMovementAbilityComponent>():nullptr;
        auto* Anim=Player?Cast<URPGAnimInstance>(Player->GetMesh()->GetAnimInstance()):nullptr;
        if(!PC || !Spells || !State || !Anim || !Flight)
        { if(++Waiting<1200)return false; Test->AddError(TEXT("Energy spell player setup unavailable"));return true; }
        if(Start<0.f)
        {
            if(++Warmup<90)return false;
            Start=World->GetTimeSeconds(); Home=Player->GetActorLocation();
            Player->SetActorRotation(FRotator(0,90,0)); // Deliberately start sideways; production facing must turn us.
            Player->GetCharacterMovement()->bOrientRotationToMovement=false;
            Player->bUseControllerRotationYaw=false;
            Player->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Dummy=World->SpawnActor<ARPGCombatTestDummy>(Home+FVector(550,0,0),FRotator(0,180,0),Spawn);
            Dummy->bWander=false; Dummy->bShowHitText=false; Dummy->StopWanderingAtHome();
            Dummy->Stats->SetStatBaseByName(TEXT("MaxHealth"),500.f);
            Dummy->Stats->SetStatBaseByName(TEXT("Health"),500.f);
            // A clean floor and target lane exist only in this temporary world.
            auto* Floor=World->SpawnActor<AStaticMeshActor>();
            Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
            Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
            Floor->SetActorScale3D(FVector(30,30,0.1)); Floor->SetActorLocation(Home-FVector(0,0,Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+5));
            Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
            Camera=World->SpawnActor<ACameraActor>(); Camera->GetCameraComponent()->FieldOfView=75.f;
            Camera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount=true;
            Camera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount=0.f;
            Camera->SetActorLocation(Home+FVector(-220,-430,140));
            Camera->SetActorRotation((Home+FVector(140,0,15)-Camera->GetActorLocation()).Rotation());
            PC->SetViewTarget(Camera.Get()); PC->bShowMouseCursor=false;
            bGameplayCamera=!FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_SPELL_GAMEPLAY_CAMERA")).IsEmpty();
            if(bGameplayCamera)
            {
                if(auto* Mode=Player->FindComponentByClass<URPGCameraModeComponent>();Mode && Mode->bIsFirstPerson)Mode->TogglePerspective();
                PC->SetViewTarget(Player);
                Player->SetActorRotation(FRotator::ZeroRotator);
            }
            Input=PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
            auto* Controls=Player->FindComponentByClass<URPGControllerInputComponent>();
            Left=FindObject<UInputAction>(Controls,TEXT("IA_LeftSpell")); Right=FindObject<UInputAction>(Controls,TEXT("IA_RightSpell"));
            Punch=FindObject<UInputAction>(Controls,TEXT("IA_RightAttack"));
            Test->TestTrue(TEXT("Existing LB/RB spell actions and RT punch retained"),Left.IsValid() && Right.IsValid() && Punch.IsValid());
            Dir=FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_SPELL_REVIEW_DIR"));
            if(Dir.IsEmpty())Dir=FPaths::ProjectSavedDir()/TEXT("SpellReview/Default");
            IFileManager::Get().MakeDirectory(*Dir,true);
            Test->TestEqual(TEXT("Tap orb radius"),Spells->GetRadius(0.f),10.f);
            Test->TestEqual(TEXT("Full orb doubles diameter"),Spells->GetRadius(1.f),20.f);
        }
        const float T=World->GetTimeSeconds()-Start,Dt=World->GetDeltaSeconds();
        Camera->SetActorLocation(Home+FVector(-420,-650,160)+FVector(0,0,Player->GetActorLocation().Z-Home.Z));
        const FVector Target=Dummy->Mesh->GetBoneLocation(TEXT("spine_04"));
        Camera->SetActorRotation((Target-Camera->GetActorLocation()).Rotation());
        PC->SetControlRotation((Target-Camera->GetActorLocation()).Rotation());
        if(bGameplayCamera)
        {
            FVector Eye;FRotator Rotation;PC->GetPlayerViewPoint(Eye,Rotation);
            PC->SetControlRotation((Target-Eye).Rotation());
        }
        if(T>=18.2f && !bAimHover)
        {
            Player->SetActorLocation(Home+FVector(0,0,550)); Player->GetCharacterMovement()->Velocity=FVector::ZeroVector;
            Test->TestTrue(TEXT("Aim-only hover starts"),Flight->StartFlight()); HoverHome=Player->GetActorLocation();bAimHover=true;
        }
        if(bAimHover)
        {
            const float A=FMath::Clamp((T-19.f)/1.4f,0.f,1.f),B=FMath::Clamp((T-20.4f)/1.6f,0.f,1.f),C=FMath::Clamp((T-22.f)/1.6f,0.f,1.f);
            const float Yaw=75.f*A-150.f*B+75.f*C,Pitch=45.f*A-85.f*B+40.f*C;
            const FRotator Look(Pitch,Yaw,0);const FVector Focus=Player->GetActorLocation()+FVector(0,0,45);
            Camera->SetActorLocation(Focus-Look.Vector()*430.f-FRotator(0,Yaw,0).RotateVector(FVector(0,225,0)));
            Camera->SetActorRotation(Look);PC->SetControlRotation(Look);
            if(T>19.5f && T<24.f)
            {
                MaxHoverDrift=FMath::Max(MaxHoverDrift,float(FVector::Distance(Player->GetActorLocation(),HoverHome)));
                SawHoverUp |= Spells->HoverPitch>4.f;SawHoverDown |= Spells->HoverPitch<-3.f;
                SawHoverRight |= Player->GetActorRotation().Yaw>40.f;SawHoverLeft |= Player->GetActorRotation().Yaw<-40.f;
                SawHoverBank |= FMath::Abs(Spells->HoverBank)>1.f;
                const FVector Axis=(Player->GetMesh()->GetBoneLocation(TEXT("head"))-Player->GetMesh()->GetBoneLocation(TEXT("pelvis"))).GetSafeNormal();
                const float Tilt=FMath::RadiansToDegrees(FMath::Atan2(-FVector::DotProduct(Axis,Player->GetActorForwardVector()),FVector::DotProduct(Axis,FVector::UpVector)));
                SawBodyUp |= Tilt>5.f;SawBodyDown |= Tilt<-5.f;
            }
        }
        const bool L=(T>=0.5f && T<2.2f)||(T>=4.f && T<5.7f)||(T>=19.f && T<24.2f);
        const bool R=(T>=3.f && T<3.12f)||(T>=4.f && T<6.1f)||(T>=8.5f && T<10.2f)||(T>=19.f && T<24.2f);
        if(Input.IsValid())
        {
            Input->InjectInputForAction(Left.Get(),FInputActionValue(L),{},{});
            Input->InjectInputForAction(Right.Get(),FInputActionValue(R),{},{});
            Input->InjectInputForAction(Punch.Get(),FInputActionValue(T>=12.3f && T<12.5f),{},{});
        }
        if(T>=1.95f && T<2.1f) SawFull |= Spells->Left.Charge>0.95f && Spells->Left.bHeld && !Spells->Right.bHeld;
        if(T>=4.4f && T<5.4f) SawBoth |= Spells->Left.bHeld && Spells->Right.bHeld;
        if(T>=5.9f && T<6.f) SawIndependent |= !Spells->Left.bHeld && Spells->Right.bHeld;
        if(T>=7.2f && !bHover)
        {
            Player->SetActorLocation(Home+FVector(0,0,120));
            Test->TestTrue(TEXT("Hover starts"),Flight->StartFlight()); bHover=true;
        }
        if(T>=9.5f && T<10.f) SawHover |= State->bIsFlying && Spells->Right.bHeld && Anim->SpellPose[1].Weight>0.99f;
        if(T>=11.4f && !bGround)
        { Flight->StopFlight(); Player->SetActorLocation(Home); Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking); bGround=true; }
        if(T>=12.3f && T<13.8f) SawPunch |= State->AttackPhase!=ERPGAttackPhase::None;
        if(T>=14.f && !bCancelStarted)
        {
            Spells->Press(ERPGHand::Left); CancelShots=Spells->Left.ShotsFired; bCancelStarted=true;
        }
        if(T>=14.2f && T<14.3f) SawAfterPunch |= Spells->Left.bHeld && Spells->Left.PoseWeight>0.8f;
        if(T>=14.35f && !bCancelled) { Spells->CancelAll(); bCancelled=true; }
        if(T>=24.1f && !bAimCancelled) { Spells->CancelAll();bAimCancelled=true; }
        if(T>=14.5f && !bTraceChecks)
        {
            Test->TestTrue(TEXT("Button-fired projectiles reach the target"),Dummy->EnergyHitCount>=4);
            FHitResult Hit;
            const FVector Head=Dummy->Mesh->GetBoneLocation(TEXT("head"));
            const bool Found=ARPGEnergyOrb::Trace(World,Head+FVector(0,-150,0),Head+FVector(0,30,0),2.f,Player,Hit);
            Test->TestTrue(TEXT("Trace skips dummy capsule and finds a bone"),Found && Hit.GetComponent()==Dummy->Mesh && Hit.BoneName!=NAME_None);
            auto* Wall=World->SpawnActor<AStaticMeshActor>();
            Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
            Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
            Wall->SetActorLocation(Head+FVector(0,-75,0));Wall->SetActorScale3D(FVector(0.4,0.1,0.4));
            Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
            Test->TestTrue(TEXT("World wall blocks target trace"),ARPGEnergyOrb::Trace(World,Head+FVector(0,-150,0),Head,5.f,Player,Hit) && Hit.GetActor()==Wall);
            Wall->Destroy();
            // Fire at the same arm twice, resetting only this test target between shots.
            Dummy->ResetDummy(); Dummy->bWander=false;
            bTraceChecks=true;
        }
        if(T>=15.f && !bTapHit) { DirectShot(World,Player,0.f);bTapHit=true; }
        if(T>=15.45f && !bTapRead)
        {
            Test->TestEqual(TEXT("Tap projectile damages once"),Dummy->EnergyHitCount,1);
            Test->TestTrue(TEXT("Tap has a localized body hit"),Dummy->LastEnergyHit.Hit.BoneName!=NAME_None);
            TapBone=Dummy->LastEnergyHit.Hit.BoneName;
            TapImpulse=Dummy->PhysicsReaction->LastReaction.ImpulseMagnitude;
            Test->TestTrue(TEXT("Tap impulse reaches physics"),Dummy->PhysicsReaction->LastReaction.bImpulseApplied);
            Dummy->ResetDummy(); Dummy->bWander=false; bTapRead=true;
        }
        if(T>=16.2f && !bChargedHit) { DirectShot(World,Player,1.f);bChargedHit=true; }
        if(T>=16.65f && !bChargedRead)
        {
            Test->TestEqual(TEXT("Charged projectile damages once"),Dummy->EnergyHitCount,1);
            Test->TestEqual(TEXT("Same targeted body region"),Dummy->LastEnergyHit.Hit.BoneName,TapBone);
            Test->TestTrue(TEXT("Charged local impulse exceeds tap"),Dummy->PhysicsReaction->LastReaction.ImpulseMagnitude>TapImpulse*1.2f);
            Test->TestTrue(TEXT("Charged staggers instead of launching"),Dummy->PhysicsReaction->GetReactionState()==ERPGPhysicsReactionState::Powered);
            bChargedRead=true;
        }
        const FVector HandLocal=Player->GetActorTransform().InverseTransformPosition(Player->GetMesh()->GetBoneLocation(TEXT("hand_r")));
        if(LastHandValid && ((T>0.5f && T<6.8f)||(T>8.5f && T<10.8f)))
        {
            const float Speed=float(FVector::Distance(HandLocal,LastHand))/FMath::Max(Dt,0.001f);
            if(Speed>MaxHandSpeed) { MaxHandSpeed=Speed; MaxHandAt=T; }
        }
        Test->TestFalse(TEXT("Finite evaluated spell hand"),HandLocal.ContainsNaN());
        if((L || R) && FMath::Min(Spells->Left.PoseWeight,Spells->Right.PoseWeight)>0.99f)
        {
            const FVector BodyRight=(Player->GetMesh()->GetBoneLocation(TEXT("upperarm_r"))-Player->GetMesh()->GetBoneLocation(TEXT("upperarm_l"))).GetSafeNormal2D();
            const FVector BodyForward=FVector::CrossProduct(BodyRight,FVector::UpVector).GetSafeNormal2D();
            const auto Angle=[](const FVector& A,const FVector& B) {return float(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(A.GetSafeNormal2D(),B.GetSafeNormal2D()),-1.,1.))));};
            MaxBodyYaw=FMath::Max(MaxBodyYaw,Angle(BodyForward,Player->GetActorForwardVector()));
            for(const TCHAR* Side:{TEXT("l"),TEXT("r")})
            {
                const FVector Arm=Player->GetMesh()->GetBoneLocation(*FString::Printf(TEXT("hand_%s"),Side))-Player->GetMesh()->GetBoneLocation(*FString::Printf(TEXT("upperarm_%s"),Side));
                MaxArmYaw=FMath::Max(MaxArmYaw,Angle(Arm,BodyForward));
            }
            ++ForwardSamples;
        }
        LastHand=HandLocal; LastHandValid=true;
        if(bVisual && T>=NextShot && T<25.5f)
        { FScreenshotRequest::RequestScreenshot(Dir/FString::Printf(TEXT("energy_%05d.png"),Shot++),false,false);NextShot+=1.f/30.f; }
        if(T<25.8f)return false;
        Test->TestTrue(TEXT("Rendered shoulders face forward during two-hand casting"),ForwardSamples>20 && MaxBodyYaw<15.f);
        Test->TestTrue(TEXT("Both arms remain in front of the rendered torso"),ForwardSamples>20 && MaxArmYaw<24.f);
        Test->TestTrue(TEXT("Hover aims up/down and turns both ways"),SawHoverUp && SawHoverDown && SawHoverLeft && SawHoverRight && SawHoverBank);
        Test->TestTrue(TEXT("Evaluated hover body follows up/down aim"),SawBodyUp && SawBodyDown);
        Test->TestTrue(TEXT("Aim-only hover holds world position"),MaxHoverDrift<0.5f);
        Test->TestTrue(TEXT("Left reaches full charge at 1.5 seconds"),SawFull);
        Test->TestTrue(TEXT("Both hands charge simultaneously"),SawBoth);
        Test->TestTrue(TEXT("Releasing left preserves right charge"),SawIndependent);
        Test->TestTrue(TEXT("Hover spell pose active"),SawHover);
        Test->TestTrue(TEXT("Punch action still animates"),SawPunch);
        Test->TestTrue(TEXT("Spell resumes during the melee stance timeout"),SawAfterPunch);
        Test->TestTrue(TEXT("Cancelled charge never fires"),Spells->Left.ShotsFired==CancelShots);
        Test->TestEqual(TEXT("One left projectile per release"),Spells->Left.ShotsFired,2);
        Test->TestEqual(TEXT("One right projectile per release"),Spells->Right.ShotsFired,3);
        Test->TestEqual(TEXT("Spell overlay returns exactly to zero"),Anim->SpellPose[0].Weight+Anim->SpellPose[1].Weight,0.f);
        FString Report=FString::Printf(TEXT("Energy integration completed. left=%d right=%d both=%d independent=%d hover=%d punch=%d tapImpulse=%.1f chargedImpulse=%.1f hitBone=%s maxSpellHandSpeed=%.1f at %.3f\n"),Spells->Left.ShotsFired,Spells->Right.ShotsFired,SawBoth,SawIndependent,SawHover,SawPunch,TapImpulse,Dummy->PhysicsReaction->LastReaction.ImpulseMagnitude,*TapBone.ToString(),MaxHandSpeed,MaxHandAt);
        FFileHelper::SaveStringToFile(Report,*(Dir/TEXT("result.txt")));Test->AddInfo(Report);
        Test->AddInfo(FString::Printf(TEXT("FORWARD AIM samples=%d bodyYaw=%.2f armYaw=%.2f hoverDrift=%.3f up=%d down=%d left=%d right=%d bank=%d"),ForwardSamples,MaxBodyYaw,MaxArmYaw,MaxHoverDrift,SawHoverUp,SawHoverDown,SawHoverLeft,SawHoverRight,SawHoverBank));
        return true;
    }
private:
    void DirectShot(UWorld* World,AActor* Player,float Charge)
    {
        const FVector Point=Dummy->Mesh->GetBoneLocation(TEXT("lowerarm_r"));
        const FVector Out=Dummy->GetActorRightVector();
        FActorSpawnParameters Spawn;Spawn.Owner=Player;
        ARPGEnergyOrb* Orb=World->SpawnActor<ARPGEnergyOrb>(Point+Out*120.f,FRotator::ZeroRotator,Spawn);
        const auto* Spell=Player->FindComponentByClass<URPGEnergySpellComponent>();
        Orb->Launch(-Out,Charge,Spell->GetRadius(Charge),Spell->ProjectileSpeed,Spell->Range,
            FMath::Lerp(Spell->TapDamage,Spell->ChargedDamage,Charge),FMath::Lerp(Spell->TapImpulse,Spell->ChargedImpulse,Charge),Spell->Color);
    }
    FAutomationTestBase* Test; bool bVisual;
    bool bGameplayCamera=false;
    int32 Waiting=0,Warmup=0,Shot=0,CancelShots=0;
    float Start=-1,NextShot=0,TapImpulse=0,MaxHandSpeed=0,MaxHandAt=0;
    float MaxBodyYaw=0,MaxArmYaw=0,MaxHoverDrift=0;int32 ForwardSamples=0;
    bool bAimHover=false,bAimCancelled=false,SawHoverUp=false,SawHoverDown=false,SawHoverLeft=false,SawHoverRight=false,SawHoverBank=false,SawBodyUp=false,SawBodyDown=false;
    bool SawFull=false,SawBoth=false,SawIndependent=false,SawHover=false,SawPunch=false,SawAfterPunch=false,bHover=false,bGround=false,bCancelled=false,bCancelStarted=false,bTraceChecks=false;
    bool bTapHit=false,bTapRead=false,bChargedHit=false,bChargedRead=false,LastHandValid=false;
    FVector Home,LastHand,HoverHome; FName TapBone; FString Dir;
    TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> Input;
    TWeakObjectPtr<UInputAction> Left,Right,Punch;
    TWeakObjectPtr<ARPGCombatTestDummy> Dummy;
    TWeakObjectPtr<ACameraActor> Camera;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnergyTest,"RPG.Spells.Energy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FEnergyTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FEnergyReview(this,false));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand()); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnergyVisual,"Visual.EnergySpell",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FEnergyVisual::RunTest(const FString&)
{
    ULevelEditorPlaySettings* Play=NewObject<ULevelEditorPlaySettings>();
    Play->NewWindowWidth=1280;Play->NewWindowHeight=752;Play->LastExecutedPlayModeType=PlayMode_InEditorFloating;
    FRequestPlaySessionParams Session;Session.WorldType=EPlaySessionWorldType::PlayInEditor;Session.EditorPlaySettings=Play;
    GEditor->RequestPlaySession(Session);
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
    ADD_LATENT_AUTOMATION_COMMAND(FEnergyReview(this,true));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand()); return true;
}
#endif
