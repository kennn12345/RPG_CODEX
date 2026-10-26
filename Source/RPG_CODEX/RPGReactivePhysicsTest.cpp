#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "RPGNpcCharacter.h"
#include "RPGReactivePhysicsComponent.h"
#include "RPGPhysicsReactionComponent.h"
#include "RPG_CODEX.h"
#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/SkeletalMeshActor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealClient.h"

namespace
{
	class FReactiveReview : public IAutomationLatentCommand
	{
	public:
		FReactiveReview(FAutomationTestBase* InTest,bool bInVisual):Test(InTest),bVisual(bInVisual) {}
		bool Update() override
		{
			UWorld* W=GEditor?GEditor->PlayWorld:nullptr;
			if (!W) return ++Frames>600?Fail(TEXT("No PIE world")):false;
			if (!Npc.IsValid())
			{
				for (TActorIterator<ARPGNpcCharacter> It(W);It;++It) { Npc=*It; break; }
				if (!Npc.IsValid()) return Fail(TEXT("No actual RPG NPC in map"));
				ARPGNpcCharacter* N=Npc.Get(); N->bWander=false;
				if (AController* C=N->GetController()) C->StopMovement();
				N->GetCharacterMovement()->StopMovementImmediately();
				// Disposable distant floor: isolate real NPC from other actors without saving/editing the map.
				AActor* Floor=W->SpawnActor<AActor>();
				UBoxComponent* Box=NewObject<UBoxComponent>(Floor); Floor->SetRootComponent(Box);
				Box->SetBoxExtent(FVector(1500,1500,20)); Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				Box->SetCollisionObjectType(ECC_WorldStatic); Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent();
				Floor->SetActorLocation(FVector(12000,12000,-20));
				UStaticMeshComponent* Surface=NewObject<UStaticMeshComponent>(Floor);
				Surface->SetupAttachment(Box); Surface->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
				Surface->SetRelativeScale3D(FVector(30,30,.4)); Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision); Surface->RegisterComponent();
				N->SetActorLocationAndRotation(FVector(12000,12000,N->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2),FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
				R=N->ReactivePhysics;
				if (!R || !R->IsReady()) return Fail(TEXT("Reactive component did not validate actual mesh/asset"));
				USkeletalMeshComponent* M=N->GetMesh();
				if (FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_REACTIVE_SYNC_ANIM"))==TEXT("1") && M->GetAnimInstance())
					M->GetAnimInstance()->bUseMultiThreadedAnimationUpdate=false;
				UE_LOG(LogRPGInput,Display,TEXT("REACTIVE AUDIT npc=%s class=%s mesh=%s anim=%s physics=%s"),*N->GetName(),*N->GetClass()->GetPathName(),*GetNameSafe(M->GetSkeletalMeshAsset()),*GetNameSafe(M->GetAnimInstance()),*M->GetPhysicsAsset()->GetPathName());
				for (const USkeletalBodySetup* B:M->GetPhysicsAsset()->SkeletalBodySetups)
					if (B) UE_LOG(LogRPGInput,Display,TEXT("REACTIVE BODY %s upper=%d mass=%.3f"),*B->BoneName.ToString(),R->GetUpperBodies().Contains(B->BoneName),M->GetBodyInstance(B->BoneName)?M->GetBodyInstance(B->BoneName)->GetBodyMass():0.f);
				for (const UPhysicsConstraintTemplate* C:M->GetPhysicsAsset()->ConstraintSetup)
					if (C) UE_LOG(LogRPGInput,Display,TEXT("REACTIVE JOINT %s %s swing=%.1f/%.1f twist=%.1f"),*C->DefaultInstance.ConstraintBone1.ToString(),*C->DefaultInstance.ConstraintBone2.ToString(),C->DefaultInstance.GetAngularSwing1Limit(),C->DefaultInstance.GetAngularSwing2Limit(),C->DefaultInstance.GetAngularTwistLimit());
				FString Name=FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_REACTIVE_REVIEW_DIR"));
				Dir=Name.IsEmpty()?FPaths::ProjectSavedDir()/TEXT("ReactivePhysics/Review"):Name;
				IFileManager::Get().MakeDirectory(*Dir,true);
				Csv=TEXT("time,state,blend,simUpper,simLower,pelvisZ,handX,handY,handZ,handSpeed,impulses\n");
				Start=W->GetTimeSeconds();
				if (bVisual)
				{
					Camera=W->SpawnActor<ACameraActor>(); Camera->GetCameraComponent()->SetFieldOfView(42.f);
					Camera->GetCameraComponent()->bConstrainAspectRatio=false;
					Camera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount=true;
					Camera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount=0.f;
					if (APlayerController* PC=W->GetFirstPlayerController()) PC->SetViewTarget(Camera);
				}
				return false;
			}
			ARPGNpcCharacter* N=Npc.Get(); USkeletalMeshComponent* M=N->GetMesh();
			const float T=W->GetTimeSeconds()-Start;
			if (Camera)
			{
				const FVector Look=N->GetActorLocation()+FVector(0,0,5),Eye=Look+FVector(300,380,70);
				Camera->SetActorLocationAndRotation(Eye,(Look-Eye).Rotation());
				if (APlayerController* PC=W->GetFirstPlayerController())
				{
					PC->bAutoManageActiveCameraTarget=false;
					PC->SetViewTarget(Camera);
					// The camera manager's cache belongs to the previous engine tick, before this latent command.
					if (PC->PlayerCameraManager && T>1.f && !PreviousEye.IsZero())
						MaxCameraError=FMath::Max(MaxCameraError,float(FVector::Dist(PC->PlayerCameraManager->GetCameraLocation(),PreviousEye)));
					PreviousEye=Eye;
				}
				if (T+1.e-4f-LastShot>=1.f/30.f)
				{
					FScreenshotRequest::RequestScreenshot(Dir/FString::Printf(TEXT("frame_%05d.png"),Shot++),false,false); LastShot=T;
				}
			}
			const FVector Hand=M->GetBoneLocation(TEXT("hand_l"));
			const float Speed=LastT>0?float(FVector::Distance(Hand,LastHand)/FMath::Max(T-LastT,.001f)):0.f;
			int32 Lower=0;
			for (FName Bone:{FName(TEXT("pelvis")),FName(TEXT("thigh_l")),FName(TEXT("thigh_r")),FName(TEXT("calf_l")),FName(TEXT("calf_r"))})
				if (const FBodyInstance* B=M->GetBodyInstance(Bone);B && B->IsInstanceSimulatingPhysics()) ++Lower;
			if (T>1.f && T<5.5f)
			{
				if (Lower>0) bLowerMoved=true;
				MaxSpeed=FMath::Max(MaxSpeed,Speed);
				for (FName Bone:R->GetUpperBodies())
				{
					const FTransform B=M->GetSocketTransform(Bone);
					if (B.ContainsNaN()) bFinite=false;
				}
			}
			Csv+=FString::Printf(TEXT("%.5f,%d,%.5f,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%d\n"),T,int32(R->State),R->GetBodyBlend(TEXT("upperarm_l")),R->GetSimulatingUpperBodies(),Lower,M->GetBoneLocation(TEXT("pelvis")).Z,Hand.X,Hand.Y,Hand.Z,Speed,R->AppliedImpulses);
			LastT=T; LastHand=Hand;
			if (Phase==0 && T>=1.f)
			{
				Test->TestFalse(TEXT("Invalid bone rejected without mutation"),R->ApplyHit(TEXT("not_a_bone"),Hand,FVector(100,0,0)));
				Test->TestFalse(TEXT("Zero impulse rejected"),R->ApplyHit(TEXT("upperarm_l"),Hand,FVector::ZeroVector));
				Test->TestTrue(TEXT("Left arm physical hit accepted"),Hit(TEXT("upperarm_l"),FVector(1,-.5,0),190.f));
				Phase=1;
			}
			if (Phase==1 && T>=1.18f)
			{
				Test->TestTrue(TEXT("Actual Chaos upper bodies simulate"),R->GetSimulatingUpperBodies()>0);
				Test->TestTrue(TEXT("Impulse actually reached body"),R->AppliedImpulses>0);
				Test->TestTrue(TEXT("Hit limb weighted more than opposite arm"),R->GetBodyBlend(TEXT("upperarm_l"))>R->GetBodyBlend(TEXT("upperarm_r")));
				Phase=2;
			}
			if (Phase==2 && T>=1.40f) { Test->TestTrue(TEXT("Repeated chest hit during recovery"),Hit(TEXT("spine_05"),FVector(-1,0,0),150.f)); Phase=3; }
			if (Phase==3 && T>=1.65f) { Test->TestTrue(TEXT("Opposite arm hit"),Hit(TEXT("upperarm_r"),FVector(-1,.5,0),190.f)); Phase=4; }
			if (Phase==4 && T>=2.8f)
			{
				Test->TestFalse(TEXT("Local reaction fully releases bodies"),R->IsLocalReactionActive());
				Test->TestEqual(TEXT("Exact zero restored blend"),R->GetBodyBlend(TEXT("upperarm_l")),0.f);
				Test->TestEqual(TEXT("No upper simulation after recovery"),R->GetSimulatingUpperBodies(),0);
				Test->TestTrue(TEXT("Fresh reaction for disable test"),Hit(TEXT("upperarm_l"),FVector(1,0,0),150.f)); Phase=5;
			}
			if (Phase==5 && T>=3.f) { R->bEnabled=false; Phase=6; }
			if (Phase==6 && T>=4.f)
			{
				Test->TestFalse(TEXT("Disabling during reaction fades and releases"),R->IsLocalReactionActive());
				R->bEnabled=true;
				Test->TestFalse(TEXT("Leg hit left to existing step system"),Hit(TEXT("calf_l"),FVector(1,0,0),120.f));
				const int32 Before=R->AppliedImpulses;
				FRPGEnergyImpact Impact; Impact.Direction=FVector(-1,0,0); Impact.Damage=1.f; Impact.Impulse=800.f;
				Impact.Hit.BoneName=TEXT("spine_05"); Impact.Hit.ImpactPoint=M->GetBoneLocation(Impact.Hit.BoneName);
				IRPGEnergySpellTarget::Execute_ReceiveEnergyHit(N,Impact);
				EnergyImpulseBefore=Before;
				Phase=7;
			}
			if (Phase==7 && T>=5.5f)
			{
				Test->TestFalse(TEXT("Lower body remains animation driven during all local hits"),bLowerMoved);
				Test->TestTrue(TEXT("Actual NPC spell interface routed an impulse through new component"),R->AppliedImpulses>EnergyImpulseBefore);
				Test->TestTrue(TEXT("All sampled upper transforms finite"),bFinite);
				Test->TestTrue(TEXT("Visible reaction has nonzero bounded blend"),R->PeakBlend>.2f && R->PeakBlend<=.85f);
				Test->TestTrue(TEXT("Local hand motion stays below 600 cm/s through recovery"),MaxSpeed<600.f);
				Test->TestTrue(TEXT("Begin local hit immediately before ragdoll"),Hit(TEXT("spine_05"),FVector(-1,0,0),120.f)); Phase=8;
			}
			if (Phase==8 && T>=5.65f)
			{
				const FVector BeforeHandoff=M->GetBoneLocation(TEXT("spine_05"));
				Test->TestTrue(TEXT("Strong impact accepted into full ragdoll"),Hit(TEXT("spine_05"),FVector(-1,0,.15),780.f));
				Test->TestTrue(TEXT("Motor handoff does not teleport the current body pose"),FVector::Dist(BeforeHandoff,M->GetBoneLocation(TEXT("spine_05")))<2.f);
				Test->TestFalse(TEXT("Local motor ownership released synchronously"),R->IsLocalReactionActive());
				Test->TestEqual(TEXT("Existing ragdoll coordinator owns full body"),N->PhysicsReaction->GetReactionState(),ERPGPhysicsReactionState::Limp);
				Phase=9;
			}
			if (Phase==9)
			{
				bSawRagdoll|=N->PhysicsReaction->GetNumSimulatingBodies()>R->GetUpperBodies().Num();
				if (T>9.f && N->PhysicsReaction->GetReactionState()==ERPGPhysicsReactionState::Animated && !N->IsBusy())
				{
					Test->TestTrue(TEXT("Full ragdoll included pelvis and legs"),bSawRagdoll);
					Test->TestTrue(TEXT("Custom get-up returns actor above floor"),N->GetActorLocation().Z>60.f);
					Test->TestTrue(TEXT("Local system ready again after get-up"),Hit(TEXT("upperarm_l"),FVector(1,0,0),120.f)); Phase=10; EndAt=T+1.2f;
				}
				else if (T>22.f) return Fail(TEXT("Existing get-up did not complete after strong impact"));
			}
			if (Phase==10 && T>=EndAt)
			{
				Test->TestFalse(TEXT("Post-get-up local hit also recovers"),R->IsLocalReactionActive());
				if (bVisual) Test->TestTrue(TEXT("Rendered camera follows inspected NPC (not player pawn)"),MaxCameraError<30.f);
				FFileHelper::SaveStringToFile(Csv,*(Dir/TEXT("trace.csv")));
				UE_LOG(LogRPGInput,Display,TEXT("REACTIVE RESULT impulses=%d maxHandSpeed=%.2f peakBlend=%.3f finite=%d lowerMoved=%d fullRagdoll=%d elapsed=%.2f cameraError=%.3f"),R->AppliedImpulses,MaxSpeed,R->PeakBlend,bFinite,bLowerMoved,bSawRagdoll,T,MaxCameraError);
				return true;
			}
			return false;
		}
	private:
		bool Hit(FName Bone,FVector Direction,float Speed)
		{
			USkeletalMeshComponent* M=Npc->GetMesh();
			const FBodyInstance* B=M->GetBodyInstance(Bone);
			if (!B) return false;
			return R->ApplyHit(Bone,M->GetBoneLocation(Bone)+FVector(0,0,4),Direction.GetSafeNormal()*Speed*B->GetBodyMass());
		}
		bool Fail(const TCHAR* Message) { Test->AddError(Message); if (!Dir.IsEmpty()) FFileHelper::SaveStringToFile(Csv,*(Dir/TEXT("trace.csv"))); return true; }
		FAutomationTestBase* Test;
		bool bVisual=false,bFinite=true,bLowerMoved=false,bSawRagdoll=false;
		TWeakObjectPtr<ARPGNpcCharacter> Npc;
		URPGReactivePhysicsComponent* R=nullptr;
		ACameraActor* Camera=nullptr;
		int32 Frames=0,Phase=0,Shot=0,EnergyImpulseBefore=0;
		float Start=0,LastT=0,LastShot=-1,MaxSpeed=0,EndAt=0,MaxCameraError=0;
		FVector LastHand=FVector::ZeroVector;
		FVector PreviousEye=FVector::ZeroVector;
		FString Dir,Csv;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGReactivePhysicsTest,"RPG.Npc.ReactivePhysics",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FRPGReactivePhysicsTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FReactiveReview(this,false));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGReactivePhysicsVisualTest,"Visual.ReactivePhysics",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FRPGReactivePhysicsVisualTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FReactiveReview(this,true));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

namespace
{
	/** Verify the reusable component without an NPC or legacy controller, including external cancellation/teardown. */
	class FReactiveStandaloneReview : public IAutomationLatentCommand
	{
	public:
		explicit FReactiveStandaloneReview(FAutomationTestBase* InTest):Test(InTest) {}
		bool Update() override
		{
			UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
			if (!World) { if (++WaitFrames<600) return false; Test->AddError(TEXT("Standalone review: no PIE world")); return true; }
			if (!Actor.IsValid())
			{
				ARPGNpcCharacter* Source=nullptr;
				for (TActorIterator<ARPGNpcCharacter> It(World);It;++It) { Source=*It; break; }
				if (!Source || !Source->GetMesh()->GetSkeletalMeshAsset()) { Test->AddError(TEXT("No actual NPC rig available for standalone fixture")); return true; }
				Actor=World->SpawnActor<ASkeletalMeshActor>(FVector(12000,14000,1000),FRotator::ZeroRotator);
				Mesh=Actor->GetSkeletalMeshComponent();
				Mesh->SetSkeletalMesh(Source->GetMesh()->GetSkeletalMeshAsset());
				Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				Mesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
				Mesh->bEnableUpdateRateOptimizations=true;
				UAnimSequence* Idle=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Characters/UEFN_Mannequin/Animations/Idle/M_Neutral_Stand_Idle_Loop.M_Neutral_Stand_Idle_Loop"));
				if (!Idle) { Test->AddError(TEXT("Actual UEFN idle unavailable")); return true; }
				Mesh->PlayAnimation(Idle,true);
				Reaction=NewObject<URPGReactivePhysicsComponent>(Actor.Get());
				Reaction->RegisterComponent();
				Reaction->bAutomaticRagdoll=false;
				if (!Reaction->IsReady()) { Test->AddError(TEXT("Standalone rig was not accepted")); return true; }
				Start=World->GetTimeSeconds(); return false;
			}
			const float T=World->GetTimeSeconds()-Start;
			FBodyInstance* Arm=Mesh->GetBodyInstance(TEXT("upperarm_l"));
			if (!Arm) { Test->AddError(TEXT("Standalone arm body missing")); return true; }
			if (Phase==0 && T>=.8f)
			{
				const FVector Point=Mesh->GetBoneLocation(TEXT("upperarm_l"));
				Test->TestFalse(TEXT("Standalone full ragdoll requires an existing recovery coordinator"),Reaction->ApplyHit(TEXT("upperarm_l"),Point,FVector(100,0,0),1.f,true));
				Test->TestTrue(TEXT("Standalone local physical hit accepted"),Reaction->ApplyHit(TEXT("upperarm_l"),Point,FVector(0,Arm->GetBodyMass()*180.f,0)));
				Phase=1;
			}
			if (Phase==1 && T>=1.f && T<2.f)
			{
				SawSimulated|=Reaction->GetSimulatingUpperBodies()>0;
				Reaction->Recover(); // a Blueprint may request the same recovery every update
			}
			if (Phase==1 && T>=2.f)
			{
				Test->TestTrue(TEXT("Standalone uses real simulated upper bodies"),SawSimulated);
				Test->TestFalse(TEXT("Repeated recovery requests must not postpone completion forever"),Reaction->IsLocalReactionActive());
				Test->TestEqual(TEXT("Standalone recovery restores collision mode"),int32(Mesh->GetCollisionEnabled()),int32(ECollisionEnabled::QueryOnly));
				const FVector Point=Mesh->GetBoneLocation(TEXT("upperarm_l"));
				for (int32 I=0;I<8;++I) Test->TestTrue(TEXT("Burst hit accepted"),Reaction->ApplyHit(TEXT("upperarm_l"),Point,FVector(0,Arm->GetBodyMass()*2000.f,0)));
				Phase=2;
			}
			if (Phase==2 && T>=2.15f)
			{
				Test->TestTrue(TEXT("Same-frame burst impulse remains body-mass bounded"),Reaction->LastAppliedImpulse<=Arm->GetBodyMass()*Reaction->MaxLocalVelocityChange+.1f);
				UPhysicalAnimationComponent* Motors=Reaction->GetPhysicalAnimation();
				Reaction->DestroyComponent();
				Test->TestFalse(TEXT("Destroying an active component stops its body simulation"),Arm->IsInstanceSimulatingPhysics());
				Test->TestFalse(TEXT("Destroying component unregisters owned motors"),Motors && Motors->IsRegistered());
				Test->TestEqual(TEXT("Teardown restores original collision"),int32(Mesh->GetCollisionEnabled()),int32(ECollisionEnabled::QueryOnly));
				Test->TestEqual(TEXT("Teardown restores original pose tick policy"),int32(Mesh->VisibilityBasedAnimTickOption),int32(EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered));
				Test->TestTrue(TEXT("Teardown restores update-rate optimization"),Mesh->bEnableUpdateRateOptimizations);
				UE_LOG(LogRPGInput,Display,TEXT("REACTIVE STANDALONE completed: simulated=%d burstImpulse=%.2f cap=%.2f"),SawSimulated,Reaction->LastAppliedImpulse,Arm->GetBodyMass()*Reaction->MaxLocalVelocityChange);
				return true;
			}
			return false;
		}
	private:
		FAutomationTestBase* Test;
		TWeakObjectPtr<ASkeletalMeshActor> Actor;
		USkeletalMeshComponent* Mesh=nullptr;
		URPGReactivePhysicsComponent* Reaction=nullptr;
		float Start=0.f;
		int32 Phase=0,WaitFrames=0;
		bool SawSimulated=false;
	};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGReactiveStandaloneTest,"RPG.Npc.ReactiveStandalone",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FRPGReactiveStandaloneTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FReactiveStandaloneReview(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
#endif
