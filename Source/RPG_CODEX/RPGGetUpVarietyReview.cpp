#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "RPGNpcCharacter.h"
#include "RPGBodyImpactComponent.h"
#include "RPGPhysicsReactionComponent.h"
#include "RPG_CODEX.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealClient.h"

namespace
{
	class FGetUpRepeatedFalls : public IAutomationLatentCommand
	{
	public:
		FGetUpRepeatedFalls(FAutomationTestBase* InTest,bool InVisual):Test(InTest),bVisual(InVisual) {}
		bool Update() override
		{
			UWorld* W=GEditor?GEditor->PlayWorld:nullptr;
			if (!W) { if (++Wait<600) return false; Test->AddError(TEXT("Get-up review: no PIE world")); return true; }
			if (!Npc.IsValid())
			{
				for (TActorIterator<ARPGNpcCharacter> It(W);It;++It) { Npc=*It; break; }
				if (!Npc.IsValid()) {Test->AddError(TEXT("Get-up review: no actual NPC"));return true;}
				Npc->bWander=false;
				if (AController* C=Npc->GetController()) C->StopMovement();
				Npc->GetCharacterMovement()->StopMovementImmediately();
				AStaticMeshActor* Floor=W->SpawnActor<AStaticMeshActor>(FVector(12000,12000,-25),FRotator::ZeroRotator);
				Floor->SetMobility(EComponentMobility::Movable);
				Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
				Floor->SetActorScale3D(FVector(50,50,.5)); Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
				Npc->SetActorLocationAndRotation(FVector(12000,12000,Npc->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2),FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
				Dir=FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_GETUP_VARIETY_DIR"));
				if (Dir.IsEmpty()) Dir=FPaths::ProjectSavedDir()/TEXT("GetUpVariety/Repeat");
				IFileManager::Get().MakeDirectory(*Dir,true);
				Rows=TEXT("episode,clip,style,elapsed,pelvisZ\n"); Captures=TEXT("frame,time,episode,clip,style\n");
				Start=EpisodeStart=W->GetTimeSeconds();
				if (bVisual)
				{
					Camera=W->SpawnActor<ACameraActor>(); Camera->GetCameraComponent()->SetFieldOfView(43.f);
					Camera->GetCameraComponent()->bConstrainAspectRatio=false;
					Camera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount=true;
					Camera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount=0.f;
				}
				return false;
			}
			const float Now=W->GetTimeSeconds(),T=Now-Start;
			if (Camera)
			{
				const FVector Look(Npc->GetActorLocation().X,Npc->GetActorLocation().Y,65.f),Eye=Look+FVector(280,360,110);
				Camera->SetActorLocationAndRotation(Eye,(Look-Eye).Rotation());
				if (APlayerController* PC=W->GetFirstPlayerController()) {PC->bAutoManageActiveCameraTarget=false;PC->SetViewTarget(Camera);}
				if (T+1.e-4f-LastShot>=1.f/30.f)
				{
					Captures+=FString::Printf(TEXT("%d,%.5f,%d,%s,%s\n"),Shot,T,Episode,*Npc->BodyImpact->LastGetUpClip.ToString(),*Npc->BodyImpact->LastGetUpStyle.ToString());
					FScreenshotRequest::RequestScreenshot(Dir/FString::Printf(TEXT("frame_%05d.png"),Shot++),false,false); LastShot=T;
				}
			}
			if (bWaiting && Now-EpisodeStart>=.8f)
			{
				static const FVector Throws[]={FVector(-320,0,90),FVector(0,320,80),FVector(-260,-200,75),FVector(220,-260,80)};
				Npc->BodyImpact->KnockDown(Throws[Episode],Npc->GetMesh()->GetBoneLocation(TEXT("spine_05")));
				EpisodeStart=Now;bWaiting=false;bSawLimp=false;
			}
			if (!bWaiting)
			{
				bSawLimp|=Npc->PhysicsReaction->GetReactionState()==ERPGPhysicsReactionState::Limp;
				if (bSawLimp && Npc->PhysicsReaction->GetReactionState()==ERPGPhysicsReactionState::Animated && !Npc->IsBusy())
				{
					const FName Clip=Npc->BodyImpact->LastGetUpClip,Style=Npc->BodyImpact->LastGetUpStyle;
					Test->TestFalse(TEXT("A real landing selected a custom get-up"),Clip.IsNone());
					Styles.Add(Style);Clips.Add(Clip);
					Rows+=FString::Printf(TEXT("%d,%s,%s,%.4f,%.3f\n"),Episode,*Clip.ToString(),*Style.ToString(),Now-EpisodeStart,Npc->GetMesh()->GetBoneLocation(TEXT("pelvis")).Z);
					UE_LOG(LogRPGInput,Display,TEXT("GETUP REPEAT episode=%d clip=%s style=%s duration=%.2f"),Episode,*Clip.ToString(),*Style.ToString(),Now-EpisodeStart);
					++Episode;bWaiting=true;EpisodeStart=Now;
					if (Episode==4)
					{
						Test->TestTrue(TEXT("Repeated real falls exercise more than one recovery style"),Styles.Num()>=2);
						Test->TestTrue(TEXT("Repeated real falls play more than one clip"),Clips.Num()>=2);
						Save();return true;
					}
				}
				else if (Now-EpisodeStart>18.f) {Test->AddError(TEXT("Get-up did not return to play within 18s"));Save();return true;}
			}
			return false;
		}
	private:
		void Save(){FFileHelper::SaveStringToFile(Rows,*(Dir/TEXT("selections.csv")));FFileHelper::SaveStringToFile(Captures,*(Dir/TEXT("captures.csv")));}
		FAutomationTestBase* Test;
		bool bVisual=false,bWaiting=true,bSawLimp=false;
		TWeakObjectPtr<ARPGNpcCharacter> Npc;
		ACameraActor* Camera=nullptr;
		TSet<FName> Clips,Styles;
		int32 Wait=0,Episode=0,Shot=0;
		float Start=0,EpisodeStart=0,LastShot=-1;
		FString Dir,Rows,Captures;
	};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGGetUpRepeatTest,"RPG.Npc.GetUpRepeat",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FRPGGetUpRepeatTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FGetUpRepeatedFalls(this,false));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGGetUpVarietyVisual,"Visual.GetUpVariety",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FRPGGetUpVarietyVisual::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FGetUpRepeatedFalls(this,true));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
