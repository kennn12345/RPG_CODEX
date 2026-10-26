// Swept leg blasts and their recovery, in temporary PIE. No level or actor assets are saved.
#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "RPGCombatTestDummy.h"
#include "RPGEnergyOrb.h"
#include "RPGEnergySpellComponent.h"
#include "RPGPhysicsReactionComponent.h"
#include "RPGCharacterStatsComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Engine/StaticMeshActor.h"
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
class FEnergyLegBalanceReview : public IAutomationLatentCommand
{
public:
    FEnergyLegBalanceReview(FAutomationTestBase* InTest,bool Visual):Test(InTest),bVisual(Visual) {}
    bool Update() override
    {
        UWorld* W=GEditor?GEditor->PlayWorld:nullptr;
        auto* P=W?UGameplayStatics::GetPlayerCharacter(W,0):nullptr;
        auto* PC=P?Cast<APlayerController>(P->GetController()):nullptr;
        auto* S=P?P->FindComponentByClass<URPGEnergySpellComponent>():nullptr;
        if(!PC || !S) { if(++Wait<1200)return false;Test->AddError(TEXT("Leg balance player missing"));return true; }
        if(Start<0.f)
        {
            if(++Warmup<90)return false;
            Start=W->GetTimeSeconds();
            // Match the dummy's taller capsule to the player's floor, including its usual 1 cm clearance.
            Home=P->GetActorLocation()+FVector(450,0,91.f-P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
            FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            D=W->SpawnActor<ARPGCombatTestDummy>(Home,FRotator(0,180,0),Spawn);
            D->bWander=false;D->bShowHitText=false;D->StopWanderingAtHome();D->PhysicsReaction->bDrawDebug=false;
            auto* Floor=W->SpawnActor<AStaticMeshActor>();
            Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
            Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
            Floor->SetActorScale3D(FVector(30,30,.1));
            Floor->SetActorLocation(Home-FVector(0,0,D->Capsule->GetScaledCapsuleHalfHeight()+6));
            Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
            auto* Camera=W->SpawnActor<ACameraActor>();Camera->GetCameraComponent()->FieldOfView=55.f;
            Camera->SetActorLocation(Home+FVector(-300,-380,90));
            Camera->SetActorRotation((Home+FVector(0,0,-20)-Camera->GetActorLocation()).Rotation());PC->SetViewTarget(Camera);
            Dir=FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_LEG_REVIEW_DIR"));
            if(Dir.IsEmpty())Dir=FPaths::ProjectSavedDir()/TEXT("SpellReview/LegBalance");
            IFileManager::Get().MakeDirectory(*Dir,true);
            CSV=TEXT("time,case,hits,rootStep,pelvisZ,footLeftZ,footRightZ,deviation,state\n");
            return false;
        }
        const float T=W->GetTimeSeconds()-Start;
        const int32 Case=FMath::Min(int32(T/4.5f),3);
        const float Local=T-Case*4.5f;
        if(Case!=CurrentCase)
        {
            D->ResetDummy();D->StopWanderingAtHome();
            D->Stats->SetStatBaseByName(TEXT("MaxHealth"),500.f);D->Stats->SetStatBaseByName(TEXT("Health"),500.f);
            CurrentCase=Case;bShot=false;StartHits=D->EnergyHitCount;Step=Deviation=0.f;bStepping=false;bPowered=false;
        }
        if(!bShot) BalanceHome=D->GetActorLocation(); // Spawned capsules can settle a few cm onto the temporary floor.
        if(Local>=.7f && !bShot)
        {
            const bool Left=Case<2;const float Charge=Case%2?1.f:0.f;
            // Aim below the knee joint: a 60 cm charged sphere aimed at that joint also catches
            // the hanging hand from the side. Keep the real enlarged sweep and first-contact behavior.
            const FVector Knee=D->Mesh->GetBoneLocation(Left?TEXT("calf_l"):TEXT("calf_r"));
            const FVector Ankle=D->Mesh->GetBoneLocation(Left?TEXT("foot_l"):TEXT("foot_r"));
            FVector Point=FMath::Lerp(Knee,Ankle,0.32f);
            const float FloorZ=Home.Z-D->Capsule->GetScaledCapsuleHalfHeight()-1.f;
            Point.Z=FMath::Max(Point.Z,FloorZ+S->GetRadius(Charge)+2.f);
            const FVector Out=D->GetActorRightVector()*(Left?-1.f:1.f);
            FActorSpawnParameters Spawn;Spawn.Owner=P;
            auto* Orb=W->SpawnActor<ARPGEnergyOrb>(Point+Out*150.f,FRotator::ZeroRotator,Spawn);
            Orb->Launch(-Out,Charge,S->GetRadius(Charge),S->ProjectileSpeed,S->Range,
                FMath::Lerp(S->TapDamage,S->ChargedDamage,Charge),FMath::Lerp(S->TapImpulse,S->ChargedImpulse,Charge),S->Color);
            bShot=true;
        }
        if(D->EnergyHitCount>StartHits)
        {
            Step=FMath::Max(Step,float(FVector::Dist2D(BalanceHome,D->GetActorLocation())));
            Deviation=FMath::Max(Deviation,D->PhysicsReaction->LastReaction.PeakDeviation);
            bPowered|=D->PhysicsReaction->GetReactionState()==ERPGPhysicsReactionState::Powered;
            if(auto* Single=D->Mesh->GetSingleNodeInstance())
                bStepping|=Single->GetCurrentAsset() && Single->GetCurrentAsset()!=D->IdleAnimation && Single->GetCurrentAsset()!=D->LocomotionBlendSpace;
        }
        if(T>=NextSample)
        {
            CSV+=FString::Printf(TEXT("%.3f,%d,%d,%.3f,%.2f,%.2f,%.2f,%.3f,%d\n"),T,Case,D->EnergyHitCount-StartHits,Step,
                D->Mesh->GetBoneLocation(TEXT("pelvis")).Z,D->Mesh->GetBoneLocation(TEXT("foot_l")).Z,D->Mesh->GetBoneLocation(TEXT("foot_r")).Z,
                Deviation,int32(D->PhysicsReaction->GetReactionState()));NextSample+=1.f/30.f;
            if(bVisual)FScreenshotRequest::RequestScreenshot(Dir/FString::Printf(TEXT("leg_%05d.png"),Shot++),false,false);
        }
        if(Local>=4.35f && !Checked[Case])
        {
            Checked[Case]=true;const FString Label=FString::Printf(TEXT("%s %s"),Case<2?TEXT("left"):TEXT("right"),Case%2?TEXT("charged"):TEXT("tap"));
            const FName Limb=D->PhysicsReaction->GetLimbForBone(D->LastEnergyHit.Hit.BoneName);
            Test->TestEqual(Label+TEXT(" one swept projectile hit"),D->EnergyHitCount-StartHits,1);
            Test->TestEqual(Label+TEXT(" correct leg"),Limb,Case<2?FName(TEXT("LegLeft")):FName(TEXT("LegRight")));
            Test->TestTrue(Label+TEXT(" leg yields while taking an authored balance step"),bPowered && bStepping && Deviation>1.f && Step>3.f);
            Test->TestTrue(Label+TEXT(" short step without a root launch"),Step<60.f && FMath::Abs(D->GetActorLocation().Z-BalanceHome.Z)<2.f);
            Test->TestTrue(Label+TEXT(" returns to standing"),D->PhysicsReaction->GetReactionState()==ERPGPhysicsReactionState::Animated);
            Test->TestFalse(Label+TEXT(" finite pose"),D->Mesh->GetBoneLocation(TEXT("pelvis")).ContainsNaN());
            Peaks[Case]=Deviation;Impulse[Case]=D->PhysicsReaction->LastReaction.ImpulseMagnitude;
            Report+=FString::Printf(TEXT("%s bone=%s step=%.2f deviation=%.2f impulse=%.1f\n"),*Label,*D->LastEnergyHit.Hit.BoneName.ToString(),Step,Deviation,Impulse[Case]);
        }
        if(T<17.9f)return false;
        Test->TestTrue(TEXT("Charged leg hits have stronger localized impulses"),Impulse[1]>Impulse[0]*1.5f && Impulse[3]>Impulse[2]*1.5f);
        FFileHelper::SaveStringToFile(Report,*(Dir/TEXT("result.txt")));FFileHelper::SaveStringToFile(CSV,*(Dir/TEXT("samples.csv")));Test->AddInfo(Report);return true;
    }
private:
    FAutomationTestBase* Test;bool bVisual,bShot=false,bStepping=false,bPowered=false;
    int32 Wait=0,Warmup=0,Shot=0,CurrentCase=-1,StartHits=0;
    float Start=-1,NextSample=0,Step=0,Deviation=0,Peaks[4]={},Impulse[4]={};bool Checked[4]={};
    FVector Home,BalanceHome;FString Dir,CSV,Report;TWeakObjectPtr<ARPGCombatTestDummy> D;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnergyLegBalanceTest,"RPG.Spells.LegBalance",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FEnergyLegBalanceTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FEnergyLegBalanceReview(this,false));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnergyLegBalanceVisual,"Visual.EnergyLegBalance",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FEnergyLegBalanceVisual::RunTest(const FString&)
{
    auto* Play=NewObject<ULevelEditorPlaySettings>();Play->NewWindowWidth=1280;Play->NewWindowHeight=752;Play->LastExecutedPlayModeType=PlayMode_InEditorFloating;
    FRequestPlaySessionParams Session;Session.WorldType=EPlaySessionWorldType::PlayInEditor;Session.EditorPlaySettings=Play;GEditor->RequestPlaySession(Session);
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));ADD_LATENT_AUTOMATION_COMMAND(FEnergyLegBalanceReview(this,true));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
