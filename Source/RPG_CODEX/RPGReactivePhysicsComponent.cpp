#include "RPGReactivePhysicsComponent.h"

#include "RPGPhysicsReactionComponent.h"
#include "RPG_CODEX.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "PhysicsControlComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"

namespace
{
	TAutoConsoleVariable<int32> CVarReactiveDebug(TEXT("rpg.ReactivePhysics.Debug"),0,TEXT("Draw phase-one physical hit bodies, target error and impulse."));
	float Ease(float T) { T=FMath::Clamp(T,0.f,1.f); return T*T*T*(T*(T*6.f-15.f)+10.f); }
}

URPGReactivePhysicsComponent::URPGReactivePhysicsComponent()
{
	PrimaryComponentTick.bCanEverTick=true;
	PrimaryComponentTick.TickGroup=TG_PrePhysics;
}

USkeletalMeshComponent* URPGReactivePhysicsComponent::ResolveMesh() const
{
	if (const ACharacter* C=Cast<ACharacter>(GetOwner())) return C->GetMesh();
	return GetOwner()?GetOwner()->FindComponentByClass<USkeletalMeshComponent>():nullptr;
}

void URPGReactivePhysicsComponent::BeginPlay()
{
	Super::BeginPlay();
	Mesh=ResolveMesh();
	if (!Mesh || !Mesh->GetPhysicsAsset() || Mesh->GetBoneIndex(UpperBodyRoot)==INDEX_NONE)
	{
		UE_LOG(LogRPGInput,Warning,TEXT("REACTIVE: %s requires a valid mesh, Physics Asset and configured upper root [%s]; legacy fallback retained."),*GetOwner()->GetName(),*UpperBodyRoot.ToString());
		return;
	}
	for (const USkeletalBodySetup* B:Mesh->GetPhysicsAsset()->SkeletalBodySetups)
		if (B && (B->BoneName==UpperBodyRoot || Mesh->BoneIsChildOf(B->BoneName,UpperBodyRoot))) UpperBodies.Add(B->BoneName);
	if (UpperBodies.IsEmpty()) return;
	// PhysicsControl's kinematic targets and Physical Animation both require a fresh pose offscreen too.
	SavedTickOption=Mesh->VisibilityBasedAnimTickOption;
	bSavedRateOptimizations=Mesh->bEnableUpdateRateOptimizations;
	Mesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mesh->bEnableUpdateRateOptimizations=false;
	bConfiguredPose=true;
	Legacy=GetOwner()->FindComponentByClass<URPGPhysicsReactionComponent>();
	if (Legacy)
	{
		EnsurePoseEvaluation();
		Legacy->OnPhysicsOwnershipRequested.AddUObject(this,&URPGReactivePhysicsComponent::YieldToLegacy);
		PrimaryComponentTick.AddPrerequisite(Legacy,Legacy->PrimaryComponentTick);
	}
	if (UPhysicsControlComponent* PC=GetOwner()->FindComponentByClass<UPhysicsControlComponent>())
		PrimaryComponentTick.AddPrerequisite(PC,PC->PrimaryComponentTick);
	PhysicalAnimation=NewObject<UPhysicalAnimationComponent>(GetOwner(),TEXT("RPGReactivePhysicalAnimation"));
	PhysicalAnimation->RegisterComponent();
	PhysicalAnimation->PrimaryComponentTick.AddPrerequisite(this,PrimaryComponentTick);
	bReady=true;
	UE_LOG(LogRPGInput,Display,TEXT("REACTIVE READY %s mesh=%s physics=%s root=%s upperBodies=%d legacy=%d"),*GetOwner()->GetName(),*GetNameSafe(Mesh->GetSkeletalMeshAsset()),*Mesh->GetPhysicsAsset()->GetPathName(),*UpperBodyRoot.ToString(),UpperBodies.Num(),Legacy!=nullptr);
}

void URPGReactivePhysicsComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	ReleaseBodies(true);
	if (Legacy) Legacy->OnPhysicsOwnershipRequested.RemoveAll(this);
	if (PhysicalAnimation) PhysicalAnimation->DestroyComponent();
	if (UAnimInstance* Anim=SynchronousAnim.Get()) Anim->bUseMultiThreadedAnimationUpdate=bSavedParallelUpdate;
	if (Mesh && bConfiguredPose) { Mesh->VisibilityBasedAnimTickOption=SavedTickOption; Mesh->bEnableUpdateRateOptimizations=bSavedRateOptimizations; }
	bReady=false;
	Super::EndPlay(Reason);
}

FName URPGReactivePhysicsComponent::ResolveBody(FName Bone) const
{
	if (!Mesh || Mesh->GetBoneIndex(Bone)==INDEX_NONE) return NAME_None;
	while (!Bone.IsNone())
	{
		if (Mesh->GetBodyInstance(Bone)) return Bone;
		Bone=Mesh->GetParentBone(Bone);
	}
	return NAME_None;
}

void URPGReactivePhysicsComponent::EnsurePoseEvaluation()
{
	if (!Legacy || !Mesh) return;
	UAnimInstance* Anim=Mesh->GetAnimInstance();
	if (Anim==SynchronousAnim.Get()) return;
	if (UAnimInstance* Previous=SynchronousAnim.Get()) Previous->bUseMultiThreadedAnimationUpdate=bSavedParallelUpdate;
	SynchronousAnim=Anim;
	if (Anim)
	{
		// UE 5.8 PhysicsControl consumes the editable pose buffer. Parallel evaluation can leave it
		// empty after the mesh swaps its buffers in rendered play, starving even kinematic targets.
		// Keep only this NPC's animation synchronous; no global CVar or engine source modification.
		bSavedParallelUpdate=Anim->bUseMultiThreadedAnimationUpdate;
		Anim->bUseMultiThreadedAnimationUpdate=false;
	}
}

bool URPGReactivePhysicsComponent::AcquireBodies()
{
	if (bOwnsBodies) return true;
	// Standalone users must also have exclusive ownership. Never overwrite another simulator.
	if (!Legacy)
		for (FName Bone:UpperBodies)
			if (const FBodyInstance* B=Mesh->GetBodyInstance(Bone); B && B->IsInstanceSimulatingPhysics()) return false;
	if (Legacy && !Legacy->BeginLocalPhysicalAnimation(UpperBodies)) return false;
	SavedCollision=Mesh->GetCollisionEnabled();
	if (!CollisionEnabledHasPhysics(SavedCollision)) Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PhysicalAnimation->SetSkeletalMeshComponent(Mesh);
	PhysicalAnimation->SetStrengthMultiplyer(1.f);
	for (FName Bone:UpperBodies)
	{
		BodyBlends.Add(Bone,0.f); TargetBlends.Add(Bone,0.f);
		if (!Legacy) if (FBodyInstance* Body=Mesh->GetBodyInstance(Bone)) { Body->SetInstanceSimulatePhysics(true); Body->PhysicsBlendWeight=0.f; }
	}
	bOwnsBodies=true;
	return true;
}

void URPGReactivePhysicsComponent::SetBlend(FName Bone,float Blend)
{
	Blend=FMath::Clamp(Blend,0.f,1.f);
	BodyBlends.FindOrAdd(Bone)=Blend;
	if (Legacy) Legacy->SetLocalPhysicalAnimationWeight(Bone,Blend);
	if (FBodyInstance* Body=Mesh?Mesh->GetBodyInstance(Bone):nullptr) Body->PhysicsBlendWeight=Blend;
	PeakBlend=FMath::Max(PeakBlend,Blend);
}

void URPGReactivePhysicsComponent::ApplyMuscles()
{
	const float Recovery=State==ERPGReactivePhysicsState::Recovering?Ease(RecoveryTime/FMath::Max(.05f,Profile.RecoverySeconds)):0.f;
	for (FName Bone:UpperBodies)
	{
		FPhysicalAnimationData Drive;
		Drive.bIsLocalSimulation=true; // parent-relative orientation; joints retain positions/lengths
		Drive.OrientationStrength=FMath::Max(0.f,Profile.OrientationStrength);
		Drive.AngularVelocityStrength=FMath::Max(0.f,Profile.AngularDamping);
		Drive.MaxAngularForce=FMath::Max(1.f,Profile.MaxAngularForce);
		if (!PhysicalAnimationProfile.IsNone())
		{
			const int32 Index=Mesh->GetPhysicsAsset()->FindBodyIndex(Bone);
			if (Mesh->GetPhysicsAsset()->SkeletalBodySetups.IsValidIndex(Index))
				if (const FPhysicalAnimationProfile* Named=Mesh->GetPhysicsAsset()->SkeletalBodySetups[Index]->FindPhysicalAnimationProfile(PhysicalAnimationProfile))
					Drive=Named->PhysicalAnimationData;
		}
		const float Yield=Bone==LastHitBone?FMath::Lerp(FMath::Clamp(Profile.HitMuscleScale,.05f,1.f),1.f,Recovery):1.f;
		Drive.OrientationStrength*=Yield;
		PhysicalAnimation->ApplyPhysicalAnimationSettings(Bone,Drive);
	}
}

bool URPGReactivePhysicsComponent::ApplyHit(FName HitBone,FVector Point,FVector Impulse,float Intensity,bool bForceRagdoll)
{
	if (!bEnabled || !bReady || !FMath::IsFinite(Intensity) || Intensity<=0.f || Point.ContainsNaN() || Impulse.ContainsNaN() || Impulse.IsNearlyZero()) return false;
	const FName Bone=ResolveBody(HitBone);
	if (Bone.IsNone()) return false;
	const FBodyInstance* Body=Mesh->GetBodyInstance(Bone);
	const float Mass=Body?FMath::Max(.5f,Body->GetBodyMass()):1.f;
	const FVector Scaled=Impulse*FMath::Clamp(ImpulseScale,0.f,10.f)*FMath::Clamp(Intensity,0.f,2.f);
	if (Scaled.IsNearlyZero() || Scaled.ContainsNaN()) return false;
	if (bForceRagdoll || (bAutomaticRagdoll && Scaled.Size()/Mass>=FMath::Max(1.f,RagdollVelocityThreshold)))
	{
		if (!Legacy) return false; // full-body recovery needs the owner's existing get-up coordinator
		Legacy->EnterFullRagdoll(FMath::Max(.1f,RagdollSeconds));
		if (Legacy->GetReactionState()!=ERPGPhysicsReactionState::Limp) return false;
		Legacy->ApplyReactionImpulse(Bone,Point,Scaled.GetSafeNormal(),FMath::Min(float(Scaled.Size()),Mass*1200.f));
		State=ERPGReactivePhysicsState::Ragdoll;
		return true;
	}
	if (!UpperBodies.Contains(Bone)) return false; // legs/pelvis retain established stepping reactions
	if (Legacy && Legacy->GetReactionState()!=ERPGPhysicsReactionState::Animated) return false;
	if (!AcquireBodies()) return false;
	LastHitBone=Bone; LastPoint=Point;
	LastImpulse=Scaled.GetClampedToMaxSize(Mass*FMath::Clamp(MaxLocalVelocityChange,1.f,600.f));
	ReactionIntensity=FMath::Clamp(Intensity,.1f,1.f);
	const float Peak=FMath::Clamp(Profile.MaxBlend,0.f,.85f)*ReactionIntensity;
	for (FName B:UpperBodies)
	{
		const bool bSameBranch=B==Bone || Mesh->BoneIsChildOf(B,Bone) || Mesh->BoneIsChildOf(Bone,B);
		TargetBlends.FindOrAdd(B)=FMath::Max(BodyBlends.FindRef(B),Peak*(bSameBranch?1.f:FMath::Clamp(NeighborBlend,0.f,1.f)));
	}
	// Bound queued burst momentum per body, instead of accumulating arbitrary same-frame impulses.
	if (FPendingHit* Existing=PendingHits.FindByPredicate([Bone](const FPendingHit& H){ return H.Bone==Bone; }))
	{
		Existing->Impulse=(Existing->Impulse+LastImpulse).GetClampedToMaxSize(Mass*FMath::Clamp(MaxLocalVelocityChange,1.f,600.f));
		Existing->Point=Point;
	}
	else PendingHits.Add({Bone,Point,LastImpulse,0});
	State=ERPGReactivePhysicsState::Reacting; ReactionTime=RecoveryTime=0.f;
	ApplyMuscles();
	return true;
}

void URPGReactivePhysicsComponent::Recover()
{
	if (!bOwnsBodies) return;
	State=ERPGReactivePhysicsState::Recovering; RecoveryTime=0.f;
	for (FName Bone:UpperBodies) TargetBlends.FindOrAdd(Bone)=BodyBlends.FindRef(Bone);
}

void URPGReactivePhysicsComponent::ReleaseBodies(bool bRestoreAnimation)
{
	if (!bOwnsBodies) return;
	PhysicalAnimation->SetStrengthMultiplyer(0.f);
	PhysicalAnimation->SetSkeletalMeshComponent(nullptr); // actually release all motors, not competing zero-strength constraints
	if (Legacy) Legacy->EndLocalPhysicalAnimation(bRestoreAnimation);
	if (bRestoreAnimation && Mesh)
	{
		for (FName Bone:UpperBodies) if (FBodyInstance* Body=Mesh->GetBodyInstance(Bone)) { Body->PhysicsBlendWeight=0.f; Body->SetInstanceSimulatePhysics(false); }
		Mesh->SetCollisionEnabled(SavedCollision);
	}
	bOwnsBodies=false; PendingHits.Reset(); BodyBlends.Reset(); TargetBlends.Reset();
}

void URPGReactivePhysicsComponent::YieldToLegacy()
{
	ReleaseBodies(false); // no kinematic reset: retain momentum into the full ragdoll
	State=ERPGReactivePhysicsState::Ragdoll;
}

float URPGReactivePhysicsComponent::GetBodyBlend(FName Bone) const { return BodyBlends.FindRef(Bone); }

int32 URPGReactivePhysicsComponent::GetSimulatingUpperBodies() const
{
	int32 Count=0;
	for (FName Bone:UpperBodies) if (const FBodyInstance* B=Mesh?Mesh->GetBodyInstance(Bone):nullptr; B && B->IsInstanceSimulatingPhysics()) ++Count;
	return Count;
}

void URPGReactivePhysicsComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick)
{
	Super::TickComponent(Dt,Type,Tick);
	if (!bReady || Dt<=0.f) return;
	EnsurePoseEvaluation();
	if (Mesh->GetEditableComponentSpaceTransforms().IsEmpty() && !bReportedEmptyPose)
	{
		bReportedEmptyPose=true;
		UE_LOG(LogRPGInput,Warning,TEXT("REACTIVE POSE CACHE %s readable=%d editable=%d parallel=%d"),*GetOwner()->GetName(),Mesh->GetComponentSpaceTransforms().Num(),Mesh->GetEditableComponentSpaceTransforms().Num(),Mesh->GetAnimInstance()?Mesh->GetAnimInstance()->bUseMultiThreadedAnimationUpdate:0);
	}
	if (!bOwnsBodies)
	{
		State=Legacy && Legacy->GetReactionState()!=ERPGPhysicsReactionState::Animated?ERPGReactivePhysicsState::Ragdoll:ERPGReactivePhysicsState::Animated;
		return;
	}
	if (!bEnabled && State!=ERPGReactivePhysicsState::Recovering) Recover();
	if (Legacy && Legacy->GetReactionState()!=ERPGPhysicsReactionState::Animated) { YieldToLegacy(); return; }
	for (int32 I=PendingHits.Num()-1;I>=0;--I)
	{
		FPendingHit& H=PendingHits[I]; FBodyInstance* B=Mesh->GetBodyInstance(H.Bone);
		if (B && B->IsInstanceSimulatingPhysics())
		{
			B->AddImpulseAtPosition(H.Impulse,H.Point); LastAppliedImpulse=float(H.Impulse.Size()); ++AppliedImpulses; PendingHits.RemoveAt(I);
		}
		else if (++H.Frames>8) { UE_LOG(LogRPGInput,Warning,TEXT("REACTIVE impulse body failed to simulate: %s"),*H.Bone.ToString()); PendingHits.RemoveAt(I); }
	}
	if (State==ERPGReactivePhysicsState::Reacting)
	{
		ReactionTime+=Dt;
		const float Response=1.f-FMath::Exp(-Dt/FMath::Max(.01f,Profile.BlendInSeconds*.35f));
		for (FName Bone:UpperBodies) SetBlend(Bone,FMath::Lerp(BodyBlends.FindRef(Bone),TargetBlends.FindRef(Bone),Response));
		if (ReactionTime>=FMath::Max(.01f,Profile.BlendInSeconds)+FMath::Max(0.f,Profile.HoldSeconds)) Recover();
	}
	else if (State==ERPGReactivePhysicsState::Recovering)
	{
		RecoveryTime+=Dt;
		const float Alpha=Ease(RecoveryTime/FMath::Max(.05f,Profile.RecoverySeconds));
		for (FName Bone:UpperBodies) SetBlend(Bone,TargetBlends.FindRef(Bone)*(1.f-Alpha));
		if (Alpha>=1.f) { ReleaseBodies(true); State=ERPGReactivePhysicsState::Animated; }
	}
	if (bOwnsBodies) ApplyMuscles();
	if (bOwnsBodies && (bDrawDebug || CVarReactiveDebug.GetValueOnGameThread()>0)) DrawReaction();
}

void URPGReactivePhysicsComponent::DrawReaction() const
{
	for (FName Bone:UpperBodies)
	{
		const FVector P=Mesh->GetBoneLocation(Bone);
		const FVector Target=PhysicalAnimation->GetBodyTargetTransform(Bone).GetLocation();
		DrawDebugSphere(GetWorld(),P,2.5f,6,Bone==LastHitBone?FColor::Orange:FColor::Cyan,false,0.f);
		DrawDebugLine(GetWorld(),P,Target,FColor::Green,false,0.f);
	}
	DrawDebugDirectionalArrow(GetWorld(),LastPoint,LastPoint+LastImpulse.GetSafeNormal()*45.f,8.f,FColor::Orange,false,0.f);
	DrawDebugString(GetWorld(),Mesh->GetComponentLocation()+FVector(0,0,180),FString::Printf(TEXT("Reactive %d [%s] peak %.2f impulses %d"),int32(State),*LastHitBone.ToString(),PeakBlend,AppliedImpulses),nullptr,FColor::Cyan,0.f);
}
