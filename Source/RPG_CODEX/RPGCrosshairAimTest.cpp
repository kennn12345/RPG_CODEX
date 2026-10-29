#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "RPGCameraModeComponent.h"
#include "RPGCharacterStatsComponent.h"
#include "RPGCombatStateComponent.h"
#include "RPGCombatTestDummy.h"
#include "RPGEnergyOrb.h"
#include "RPGEnergySpellComponent.h"
#include "RPGPrototypeAnimationComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealClient.h"

namespace
{
/** One shot at a dummy: the crosshair is held on Bone (+ BoneOffset) of a dummy standing at Distance / Bearing from the
 *  player. Bone null: a punch at empty air, the view turned Bearing right and BoneOffset.Y degrees up (no dummy ahead). */
struct FAimCase
{
    const TCHAR* Name;
    bool bSpell;
    bool bLeft;
    float Distance;   // cm from the player
    float Bearing;    // degrees right of the player's start facing
    const TCHAR* Bone;
    FVector BoneOffset;
};

const FAimCase AimCases[] = {
    {TEXT("punch R chin ahead"),       false,false, 90.f,  0.f,TEXT("head"),    FVector(0,0,-6)},
    {TEXT("punch L chin ahead"),       false,true,  90.f,  0.f,TEXT("head"),    FVector(0,0,-6)},
    {TEXT("punch R chin 25 right"),    false,false, 90.f, 25.f,TEXT("head"),    FVector(0,0,-6)},
    {TEXT("punch L chin 25 left"),     false,true,  90.f,-25.f,TEXT("head"),    FVector(0,0,-6)},
    {TEXT("punch R chest ahead"),      false,false, 90.f,  0.f,TEXT("spine_04"),FVector::ZeroVector},
    {TEXT("punch L stomach ahead"),    false,true,  90.f,  0.f,TEXT("spine_02"),FVector::ZeroVector},
    {TEXT("punch R chin 70 right"),    false,false, 90.f, 70.f,TEXT("head"),    FVector(0,0,-6)},
    {TEXT("punch R chin far 105"),     false,false,105.f,  0.f,TEXT("head"),    FVector(0,0,-6)},
    {TEXT("punch R air ahead"),        false,false,  0.f,  0.f,nullptr,FVector(0,0,0)},
    {TEXT("punch L air ahead"),        false,true,   0.f,  0.f,nullptr,FVector(0,0,0)},
    {TEXT("punch R air look down 12"), false,false,  0.f,  0.f,nullptr,FVector(0,-12,0)},
    {TEXT("punch L air look 30 right"),false,true,   0.f, 30.f,nullptr,FVector(0,0,0)},
    {TEXT("spell R chest 150 near"),   true, false,150.f, 10.f,TEXT("spine_04"),FVector::ZeroVector},
    {TEXT("spell L head 300"),         true, true, 300.f,-20.f,TEXT("head"),    FVector::ZeroVector},
    {TEXT("spell R chest 300"),        true, false,300.f,  0.f,TEXT("spine_04"),FVector::ZeroVector},
    {TEXT("spell R pelvis 800 left"),  true, false,800.f,-10.f,TEXT("pelvis"),  FVector::ZeroVector},
    {TEXT("spell L head 1500"),        true, true,1500.f,  0.f,TEXT("head"),    FVector::ZeroVector},
    {TEXT("spell R chest 1500 right"), true, false,1500.f,15.f,TEXT("spine_04"),FVector::ZeroVector},
};

/**
 * Crosshair accuracy with the real gameplay camera: for each case a dummy stands at a distance / bearing, the control
 * rotation keeps the screen centre (the reticle) on one of its bones, and a punch or a spell is thrown. Reports where it
 * lands against the point under the reticle: the fist's closest pass during the strike window (and the struck bone), or
 * the orb's line of flight. Visual: a screenshot with the HUD from the player's camera at each landing.
 */
class FCrosshairAimReview:public IAutomationLatentCommand
{
public:
    FCrosshairAimReview(FAutomationTestBase* InTest,bool InFirst,bool InVisual):Test(InTest),bFirst(InFirst),bVisual(InVisual){}
    bool Update() override
    {
        UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
        ACharacter* Player=World?UGameplayStatics::GetPlayerCharacter(World,0):nullptr;
        APlayerController* Controller=Player?Cast<APlayerController>(Player->GetController()):nullptr;
        URPGPrototypeAnimationComponent* Animation=Player?Player->FindComponentByClass<URPGPrototypeAnimationComponent>():nullptr;
        URPGCombatStateComponent* State=Player?Player->FindComponentByClass<URPGCombatStateComponent>():nullptr;
        URPGEnergySpellComponent* Spells=Player?Player->FindComponentByClass<URPGEnergySpellComponent>():nullptr;
        if(!Player || !Controller || !Controller->PlayerCameraManager || !Animation || !State || !Spells)
        {if(++Wait<600)return false;Test->AddError(TEXT("Crosshair aim: player components unavailable"));return true;}
        const float Now=World->GetTimeSeconds();
        if(!bReady)
        {
            bReady=true;
            Home=Player->GetActorLocation();HomeYaw=Player->GetActorRotation().Yaw;
            Player->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            if(URPGCameraModeComponent* Mode=Player->FindComponentByClass<URPGCameraModeComponent>();Mode && Mode->bIsFirstPerson!=bFirst)Mode->TogglePerspective();
            DefaultStanceTimeout=Animation->CombatStanceTimeout;
            Dir=FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_AIM_REVIEW_DIR"));
            if(Dir.IsEmpty())Dir=FPaths::ProjectSavedDir()/TEXT("AimReview")/(bFirst?TEXT("FirstPerson"):TEXT("ThirdPerson"));
            if(bVisual){IFileManager::Get().DeleteDirectory(*Dir,false,true);IFileManager::Get().MakeDirectory(*Dir,true);}
            StartCase(World,Player,Animation,State,Now);
            return false;
        }
        const FAimCase& Case=AimCases[CaseIndex];
        const float T=Now-PhaseStart;
        if(!Dummy.IsValid()){Test->AddError(FString::Printf(TEXT("%s: dummy lost"),Case.Name));return true;}

        // The point under the reticle: the rendered view's centre ray, first thing it meets (bodies, not capsules).
        const FVector Eye=Controller->PlayerCameraManager->GetCameraLocation();
        const FVector Look=Controller->PlayerCameraManager->GetCameraRotation().Vector();
        FHitResult Under;
        const bool bAir=Case.Bone==nullptr;
        const bool bOnDummy=bAir || (ARPGEnergyOrb::Trace(World,Eye,Eye+Look*5000.f,0.f,Player,Under) && Under.GetActor()==Dummy.Get());
        // Hold the reticle on the aimed bone (until the attack lands: the dummy reacts afterwards), or on empty air.
        // Spells: aim, press, then leave the view alone, so the shot also checks the casting camera keeps the reticle on
        // the spot it was on while it eases over the shoulder.
        if(bAir)Controller->SetControlRotation(FRotator(Case.BoneOffset.Y,HomeYaw+Case.Bearing,0));
        else if(!(Case.bSpell && Phase>=1))
        {
            const FVector Target=Dummy->Mesh->GetBoneLocation(Case.Bone)+Dummy->GetActorRotation().RotateVector(Case.BoneOffset);
            Controller->SetControlRotation((Target-Eye).Rotation());
        }

        if(Phase==0)
        {
            // Stance (punches) or exploring (spells); the body squares up underneath the reticle.
            if(Case.bSpell && Animation->IsInCombat() && T<12.f)return false;
            if(T<1.6f)return false;   // (the casting camera has eased back after the last spell)
            if(!bOnDummy)
            {
                if(T<4.f)return false;
                Test->AddError(FString::Printf(TEXT("%s: reticle never settled on the dummy"),Case.Name));NextCase(World,Player,Animation,State,Now);return CaseIndex>=UE_ARRAY_COUNT(AimCases);
            }
            Crosshair=Under.ImpactPoint;CrosshairBone=Under.BoneName;
            Shot(TEXT("aim"));
            const ERPGHand Hand=Case.bLeft?ERPGHand::Left:ERPGHand::Right;
            HitsBefore=Case.bSpell?Dummy->EnergyHitCount:Dummy->HitCount;
            ShotsBefore=Spells->Right.ShotsFired+Spells->Left.ShotsFired;
            if(Case.bSpell){Spells->Press(Hand);Spells->Release(Hand);}
            else
            {
                State->ReportSimpleAction(ERPGPlayerAction::AttackChargeStarted,Hand);
                State->ReportSimpleAction(ERPGPlayerAction::Attack,Hand);
            }
            MinFist=1e9f;bLanded=bAirShot=false;Phase=1;PhaseStart=Now;
            return false;
        }
        if(Phase==1)
        {
            // Keep the crosshair point current while the reticle tracks (the camera eases to the combat shoulder).
            // (until the strike or the orb lands: the dummy reacts after that)
            if(bOnDummy && !bLanded && !Case.bSpell && !State->bStrikeHitWindowOpen)
            {Crosshair=Under.ImpactPoint;CrosshairBone=Under.BoneName;}
            if(!Case.bSpell && State->bStrikeHitWindowOpen)
            {
                const USkeletalMeshComponent* Mesh=Player->GetMesh();
                const FName Wrist=Case.bLeft?TEXT("hand_l"):TEXT("hand_r"),Knuckle=Case.bLeft?TEXT("middle_01_l"):TEXT("middle_01_r");
                const FVector A=Mesh->GetBoneLocation(Wrist),B=Mesh->GetBoneLocation(Knuckle);
                for(const FVector& Point:{FMath::Lerp(A,B,.55f),B})
                    if(bAir)
                    {
                        // Empty air: how far the fist is from the reticle's line, i.e. how far off it looks on screen.
                        const float Off=FVector::CrossProduct(Point-Eye,Look).Size();
                        if(Off<MinFist){MinFist=Off;FistOffset=Player->GetActorRotation().UnrotateVector(Point-(Eye+Look*FVector::DotProduct(Point-Eye,Look)));}
                        else if(Point==B && bLanded && !bAirShot){bAirShot=true;Shot(TEXT("land"));}   // just past the closest pass
                        bLanded=true;
                    }
                    else if(FVector::Dist(Point,Crosshair)<MinFist)
                    {
                        MinFist=FVector::Dist(Point,Crosshair);
                        FistOffset=Player->GetActorRotation().UnrotateVector(Point-Crosshair);
                    }
            }
            const int32 Hits=Case.bSpell?Dummy->EnergyHitCount:Dummy->HitCount;
            if(Hits>HitsBefore && !bLanded)
            {
                bLanded=true;
                if(Case.bSpell)
                {
                    const FHitResult& Hit=Dummy->LastEnergyHit.Hit;
                    const FVector D=Dummy->LastEnergyHit.Direction.GetSafeNormal();
                    Miss=FVector::CrossProduct(Crosshair-Hit.TraceStart,D).Size();
                    LandedBone=Hit.BoneName;
                }
                else LandedBone=Dummy->LastHit.HitBone;
                Shot(TEXT("land"));
            }
            if(T<(Case.bSpell?1.5f:.9f))return false;
            Report(Case);
            NextCase(World,Player,Animation,State,Now);
            return CaseIndex>=UE_ARRAY_COUNT(AimCases);
        }
        return false;
    }
private:
    void Shot(const TCHAR* Label)
    {
        if(!bVisual)return;
        const FString Name=FString::Printf(TEXT("%02d_%s_%s.png"),CaseIndex,Label,*FString(AimCases[CaseIndex].Name).Replace(TEXT(" "),TEXT("_")));
        FScreenshotRequest::RequestScreenshot(Dir/Name,true,false);
    }
    void Report(const FAimCase& Case)
    {
        const TCHAR* View=bFirst?TEXT("FP"):TEXT("TP");
        if(Case.bSpell)
        {
            Test->AddInfo(FString::Printf(TEXT("%s %s: reticle on %s, %s; orb line misses the reticle point by %.1f cm, hit %s"),View,Case.Name,
                *CrosshairBone.ToString(),bLanded?TEXT("landed"):TEXT("NO HIT"),bLanded?Miss:-1.f,*LandedBone.ToString()));
            Test->TestTrue(FString::Printf(TEXT("%s %s: orb lands on the reticle (%.1f cm)"),View,Case.Name,Miss),bLanded && Miss<12.f);
        }
        else
        {
            Test->AddInfo(FString::Printf(TEXT("%s %s: reticle on %s at %.0f cm high; fist passes %.1f cm from it (forward %.0f, right %.0f, up %.0f), %s %s"),View,Case.Name,
                *CrosshairBone.ToString(),Crosshair.Z-Home.Z,MinFist,FistOffset.X,FistOffset.Y,FistOffset.Z,bLanded?TEXT("hit"):TEXT("NO HIT"),*LandedBone.ToString()));
            Test->TestTrue(FString::Printf(TEXT("%s %s: punch lands on the reticle (%.1f cm)"),View,Case.Name,MinFist),bLanded && MinFist<15.f);   // (chest shots can stop on the dummy's raised guard arm, a few cm in front)
        }
        Results+=FString::Printf(TEXT("%s,%s,%d,%.2f,%s,%s\n"),View,Case.Name,bLanded?1:0,Case.bSpell?Miss:MinFist,*CrosshairBone.ToString(),*LandedBone.ToString());
    }
    void StartCase(UWorld* World,ACharacter* Player,URPGPrototypeAnimationComponent* Animation,URPGCombatStateComponent* State,float Now)
    {
        const FAimCase& Case=AimCases[CaseIndex];
        if(Dummy.IsValid())Dummy->Destroy();
        Player->GetCharacterMovement()->StopMovementImmediately();
        Player->SetActorLocationAndRotation(Home,FRotator(0,HomeYaw,0),false,nullptr,ETeleportType::ResetPhysics);
        const FRotator Bearing(0,HomeYaw+Case.Bearing,0);
        // (empty air: the dummy stands well behind, out of the way)
        const FVector At=Case.Bone?Home+Bearing.Vector()*Case.Distance:Home-FRotator(0,HomeYaw,0).Vector()*600.f;
        FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Dummy=World->SpawnActor<ARPGCombatTestDummy>(At,(Home-At).Rotation(),Spawn);
        Dummy->bWander=false;Dummy->bShowHitText=false;Dummy->StopWanderingAtHome();
        Dummy->Stats->SetStatBaseByName(TEXT("MaxHealth"),5000.f);Dummy->Stats->SetStatBaseByName(TEXT("Health"),5000.f);
        if(Case.bSpell)Animation->CombatStanceTimeout=DefaultStanceTimeout;
        else
        {
            Animation->CombatStanceTimeout=60.f;
            State->ReportSimpleAction(ERPGPlayerAction::UnarmedGuard,ERPGHand::None);
            State->ReportSimpleAction(ERPGPlayerAction::DefenseReleased,ERPGHand::None);
        }
        Phase=0;PhaseStart=Now;Crosshair=FVector::ZeroVector;CrosshairBone=NAME_None;LandedBone=NAME_None;Miss=-1.f;
    }
    void NextCase(UWorld* World,ACharacter* Player,URPGPrototypeAnimationComponent* Animation,URPGCombatStateComponent* State,float Now)
    {
        if(++CaseIndex<UE_ARRAY_COUNT(AimCases)){StartCase(World,Player,Animation,State,Now);return;}
        if(Dummy.IsValid())Dummy->Destroy();
        Animation->CombatStanceTimeout=DefaultStanceTimeout;
        // Camera framing, for the record.
        if(const USpringArmComponent* Boom=Player->FindComponentByClass<USpringArmComponent>())
            Test->AddInfo(FString::Printf(TEXT("%s camera: arm %.0f cm, socket offset (%.0f, %.0f, %.0f), FOV %.0f, lag %d (%.0f)"),bFirst?TEXT("FP"):TEXT("TP"),
                Boom->TargetArmLength,Boom->SocketOffset.X,Boom->SocketOffset.Y,Boom->SocketOffset.Z,
                Player->FindComponentByClass<UCameraComponent>()?Player->FindComponentByClass<UCameraComponent>()->FieldOfView:0.f,
                Boom->bEnableCameraLag?1:0,Boom->CameraLagSpeed));
        FFileHelper::SaveStringToFile(Results,*(FPaths::ProjectSavedDir()/TEXT("AimReview")/(bFirst?TEXT("results-fp.csv"):TEXT("results-tp.csv"))));
    }
    FAutomationTestBase* Test;bool bFirst,bVisual;bool bReady=false,bLanded=false,bAirShot=false;int32 Wait=0,Phase=0,HitsBefore=0,ShotsBefore=0;int32 CaseIndex=0;
    float PhaseStart=0.f,HomeYaw=0.f,MinFist=0.f,Miss=0.f,DefaultStanceTimeout=8.f;
    FVector Home,Crosshair,FistOffset=FVector::ZeroVector;FName CrosshairBone,LandedBone;FString Dir,Results;TWeakObjectPtr<ARPGCombatTestDummy> Dummy;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrosshairAimTest,"RPG.Combat.CrosshairAim",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCrosshairAimTest::RunTest(const FString&)
{ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));ADD_LATENT_AUTOMATION_COMMAND(FCrosshairAimReview(this,false,false));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrosshairAimFirst,"RPG.Combat.CrosshairAimFirstPerson",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCrosshairAimFirst::RunTest(const FString&)
{ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));ADD_LATENT_AUTOMATION_COMMAND(FCrosshairAimReview(this,true,false));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrosshairAimVisual,"Visual.CrosshairAim",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCrosshairAimVisual::RunTest(const FString&)
{ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));ADD_LATENT_AUTOMATION_COMMAND(FCrosshairAimReview(this,false,true));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrosshairAimVisualFirst,"Visual.CrosshairAimFirstPerson",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCrosshairAimVisualFirst::RunTest(const FString&)
{ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));ADD_LATENT_AUTOMATION_COMMAND(FCrosshairAimReview(this,true,true));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;}
#endif
