#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "RPGNpcCharacter.h"
#include "RPGBodyImpactComponent.h"
#include "RPGPhysicsReactionComponent.h"
#include "RPG_CODEX.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimTypes.h"
#include "PhysicsEngine/BodyInstance.h"
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
			if (Npc->BodyImpact->IsGetUpAligning())
			{
				UAnimInstance* Anim=Npc->GetMesh()->GetAnimInstance();UAnimMontage* Montage=Anim?Anim->GetCurrentActiveMontage():nullptr;
				if (Montage)
				{
					const float Position=Anim->Montage_GetPosition(Montage);
					if (!bWasAligning) AlignPosition=Position;
					if (FMath::Abs(Position-AlignPosition)>.001f && !bReportedMovingAlign)
					{
						Test->AddError(TEXT("Get-up target moved while the ragdoll pose was still fading"));bReportedMovingAlign=true;
					}
					bSawAligning=true;
				}
				if (!bInterrupted && FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_GETUP_INTERRUPT"))==TEXT("1") && Now-EpisodeStart>2.f)
				{
					Npc->BodyImpact->KnockDown(FVector(0,-220,80),Npc->GetMesh()->GetBoneLocation(TEXT("spine_05")));
					bInterrupted=true;
				}
			}
			bWasAligning=Npc->BodyImpact->IsGetUpAligning();
			if (FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_GETUP_ARM_TRACE"))==TEXT("1"))
			{
				if (PoseRows.IsEmpty()) PoseRows=TEXT("time,episode,state,returning,montage_time,clip,bone,x,y,z,qx,qy,qz,qw\n");
				USkeletalMeshComponent* M=Npc->GetMesh();UAnimInstance* Anim=M->GetAnimInstance();
				UAnimMontage* Montage=Anim?Anim->GetCurrentActiveMontage():nullptr;
				const float MontageTime=Montage?Anim->Montage_GetPosition(Montage):-1.f;
				for (const FName Bone:{FName(TEXT("pelvis")),FName(TEXT("spine_05")),FName(TEXT("head")),FName(TEXT("upperarm_l")),FName(TEXT("lowerarm_l")),FName(TEXT("hand_l")),FName(TEXT("upperarm_r")),FName(TEXT("lowerarm_r")),FName(TEXT("hand_r"))})
				{
					const FTransform P=M->GetSocketTransform(Bone,RTS_Component);const FVector V=P.GetLocation();const FQuat Q=P.GetRotation();
					PoseRows+=FString::Printf(TEXT("%.5f,%d,%d,%d,%.5f,%s,%s,%.6f,%.6f,%.6f,%.9f,%.9f,%.9f,%.9f\n"),T,Episode,int32(Npc->PhysicsReaction->GetReactionState()),Npc->PhysicsReaction->IsReturning(),MontageTime,*Npc->BodyImpact->LastGetUpClip.ToString(),*Bone.ToString(),V.X,V.Y,V.Z,Q.X,Q.Y,Q.Z,Q.W);
				}
			}
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
					if (!bWaiting && Episode==0 && !bSeededCapture && Now-EpisodeStart>.25f && Npc->PhysicsReaction->GetReactionState()==ERPGPhysicsReactionState::Limp && !FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_GETUP_CAPTURE_REVIEW")).IsEmpty())
					{
						bSeededCapture=true;
						// Seed after the PhysicsControl movement-type change has reached Chaos.
						// Test-only matching landing. The production selector still chooses the recovery normally.
						const TCHAR* CaptureName=FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_GETUP_CAPTURE_REVIEW"))==TEXT("8611")?TEXT("RPG_GetUp_FaceUp_Capture8611"):TEXT("RPG_GetUp_FaceDown_Capture8614");
						const FString CapturePath=FString::Printf(TEXT("/Game/RPG/Animations/SK_UEFN_Mannequin/%s.%s"),CaptureName,CaptureName);
						UAnimSequence* Clip=LoadObject<UAnimSequence>(nullptr,*CapturePath);
						if (!Clip) {Test->AddError(TEXT("Missing recorded get-up for landing review"));Save();return true;}
						const FReferenceSkeleton& Ref=Clip->GetSkeleton()->GetReferenceSkeleton();
						USkeletalMeshComponent* Mesh=Npc->GetMesh();
						const FVector At=Npc->GetActorLocation();const FTransform Root(Mesh->GetComponentQuat(),FVector(At.X,At.Y,0.f));
						int32 Bodies=0;
						for (int32 B=0;B<Ref.GetNum();++B)
							if (FBodyInstance* Body=Mesh->GetBodyInstance(Ref.GetBoneName(B)))
							{
								FTransform Bone=FTransform::Identity;
								for (int32 I=B;I!=INDEX_NONE;I=Ref.GetParentIndex(I))
								{
									FTransform Local;Clip->GetBoneTransform(Local,FSkeletonPoseBoneIndex(I),FAnimExtractContext(0.),false);Bone=Bone*Local;
								}
								Body->SetBodyTransform(Bone*Root,ETeleportType::TeleportPhysics);
								Body->SetLinearVelocity(FVector::ZeroVector,false);Body->SetAngularVelocityInRadians(FVector::ZeroVector,false);++Bodies;
							}
						Test->TestTrue(TEXT("Recorded landing positioned actual physics bodies"),Bodies>=12);
						UE_LOG(LogRPGInput,Display,TEXT("GETUP CAPTURE SEEDED bodies=%d"),Bodies);
					}
			if (!bWaiting)
			{
				bSawLimp|=Npc->PhysicsReaction->GetReactionState()==ERPGPhysicsReactionState::Limp;
				if (bSawLimp && Npc->PhysicsReaction->GetReactionState()==ERPGPhysicsReactionState::Animated && !Npc->IsBusy())
				{
					const FName Clip=Npc->BodyImpact->LastGetUpClip,Style=Npc->BodyImpact->LastGetUpStyle;
						const FName ExpectedCapture=FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_GETUP_CAPTURE_REVIEW"))==TEXT("8611")?FName(TEXT("RPG_GetUp_FaceUp_Capture8611")):FName(TEXT("RPG_GetUp_FaceDown_Capture8614"));
						bSawCapture|=Clip==ExpectedCapture;
					Test->TestFalse(TEXT("A real landing selected a custom get-up"),Clip.IsNone());
					Styles.Add(Style);Clips.Add(Clip);
					Rows+=FString::Printf(TEXT("%d,%s,%s,%.4f,%.3f\n"),Episode,*Clip.ToString(),*Style.ToString(),Now-EpisodeStart,Npc->GetMesh()->GetBoneLocation(TEXT("pelvis")).Z);
					UE_LOG(LogRPGInput,Display,TEXT("GETUP REPEAT episode=%d clip=%s style=%s duration=%.2f"),Episode,*Clip.ToString(),*Style.ToString(),Now-EpisodeStart);
					++Episode;bWaiting=true;EpisodeStart=Now;
					if (Episode==4)
					{
							if (!FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_GETUP_CAPTURE_REVIEW")).IsEmpty())
								Test->TestTrue(TEXT("Actual NPC selected and completed the recorded recovery"),bSawCapture);
						if (FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_GETUP_INTERRUPT"))==TEXT("1"))
							Test->TestTrue(TEXT("A new knockdown interrupted the held get-up"),bInterrupted);
						Test->TestTrue(TEXT("Actual get-ups aligned before authored motion resumed"),bSawAligning);
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
		void Save(){FFileHelper::SaveStringToFile(Rows,*(Dir/TEXT("selections.csv")));FFileHelper::SaveStringToFile(Captures,*(Dir/TEXT("captures.csv")));if(!PoseRows.IsEmpty())FFileHelper::SaveStringToFile(PoseRows,*(Dir/TEXT("arms.csv")));}
		FAutomationTestBase* Test;
		bool bVisual=false,bWaiting=true,bSawLimp=false,bWasAligning=false,bSawAligning=false,bReportedMovingAlign=false,bInterrupted=false,bSawCapture=false,bSeededCapture=false;
		TWeakObjectPtr<ARPGNpcCharacter> Npc;
		ACameraActor* Camera=nullptr;
		TSet<FName> Clips,Styles;
		int32 Wait=0,Episode=0,Shot=0;
		float Start=0,EpisodeStart=0,LastShot=-1,AlignPosition=0;
		FString Dir,Rows,Captures,PoseRows;
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
