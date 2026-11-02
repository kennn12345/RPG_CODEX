#include "RPGBodyImpactComponent.h"

#include "RPGBodyMass.h"
#include "RPGMovementAbilityComponent.h"
#include "RPGFlightAnimationComponent.h"
#include "RPGNpcCharacter.h"
#include "RPGPhysicsReactionComponent.h"
#include "RPGHitOutcomeComponent.h"
#include "RPGPowerSystem.h"
#include "RPG_CODEX.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimTypes.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	/** The two recorded recoveries are the entire gameplay pool. */
	enum class EGetUpKind : uint8 { Stand, Crouch, Roll, Run };
	struct FGetUp { const TCHAR* Name; EGetUpKind Kind; const TCHAR* Style; };
	const FGetUp GetUps[] = {
		{ TEXT("FaceDown_Capture8614"), EGetUpKind::Stand, TEXT("CapturedPush") },
		{ TEXT("FaceUp_Capture8611"), EGetUpKind::Stand, TEXT("CapturedSitTurn") },
	};

	/** Body pose features compared between the ragdoll and the clips: where these bones are relative to the hips, in a
	 *  frame turned with the body (X along the hips-to-head direction on the ground, Z up), and how much each matters. */
	struct FFeatureBone { const TCHAR* Bone; float Weight; };
	const FFeatureBone FeatureBones[] = {
		{ TEXT("head"), 1.5f }, { TEXT("spine_05"), 1.5f },
		{ TEXT("hand_l"), 1.f }, { TEXT("hand_r"), 1.f }, { TEXT("lowerarm_l"), 0.7f }, { TEXT("lowerarm_r"), 0.7f },
		{ TEXT("calf_l"), 0.7f }, { TEXT("calf_r"), 0.7f }, { TEXT("foot_l"), 1.f }, { TEXT("foot_r"), 1.f },
		{ TEXT("upperarm_l"), 0.3f }, { TEXT("upperarm_r"), 0.3f }, { TEXT("thigh_l"), 0.3f }, { TEXT("thigh_r"), 0.3f },
	};
	constexpr int32 NumFeatures = UE_ARRAY_COUNT(FeatureBones);

	/** The limb segments (indices into FeatureBones: upper arm, forearm, thigh, shin on each side). A start whose segment
	 *  points far from the ragdoll's (MaxSegmentTurn) can't be faded into: half way, the shortest turn between the two
	 *  flips from one way round to the other and the limb whips and shakes through the fade. */
	const int32 Segments[][2] = { { 10, 4 }, { 4, 2 }, { 11, 5 }, { 5, 3 }, { 12, 6 }, { 6, 8 }, { 13, 7 }, { 7, 9 } };
	constexpr float MaxSegmentTurn = 110.f;
	/** Weight of the limb turn (deg^2) against the pose error (weighted cm^2) when choosing a start. */
	constexpr float TurnCost = 1.5f;

	struct FPoseFeatures
	{
		FVector Points[NumFeatures];
		float PelvisHeight = 0.f;
		FVector ChestFacing = FVector::ZeroVector;
	};
	FVector ChestFacing(const FReferenceSkeleton& Ref,const FQuat& Actual)
	{
		FTransform Rest=FTransform::Identity;
		for (int32 I=Ref.FindBoneIndex(TEXT("spine_05"));I!=INDEX_NONE;I=Ref.GetParentIndex(I)) Rest=Rest*Ref.GetRefBonePose()[I];
		return Actual.RotateVector(Rest.GetRotation().UnrotateVector(FVector::YAxisVector)).GetSafeNormal();
	}
	FPoseFeatures MakeFeatures(const FVector& Pelvis, const FVector* Bones, float PelvisHeight,const FVector& Facing)
	{
		// Bones[0] is the head: the frame turns with the hips-to-head direction (flattened), so any facing compares.
		FVector X = FVector(Bones[0].X - Pelvis.X, Bones[0].Y - Pelvis.Y, 0.f).GetSafeNormal();
		if (X.IsNearlyZero()) X = FVector::ForwardVector;
		const FVector Y = FVector::CrossProduct(FVector::UpVector, X);
		FPoseFeatures F;
		for (int32 I = 0; I < NumFeatures; ++I)
		{
			const FVector D = Bones[I] - Pelvis;
			F.Points[I] = FVector(FVector::DotProduct(D, X), FVector::DotProduct(D, Y), D.Z);
		}
		F.PelvisHeight = PelvisHeight;
		F.ChestFacing=FVector(FVector::DotProduct(Facing,X),FVector::DotProduct(Facing,Y),Facing.Z).GetSafeNormal();
		return F;
	}
	float ChestTurn(const FPoseFeatures& A,const FPoseFeatures& B)
	{
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(float(FVector::DotProduct(A.ChestFacing,B.ChestFacing)),-1.f,1.f)));
	}
	const TCHAR* SegmentNames[] = { TEXT("upper arm L"), TEXT("forearm L"), TEXT("upper arm R"), TEXT("forearm R"),
		TEXT("thigh L"), TEXT("shin L"), TEXT("thigh R"), TEXT("shin R") };
	/** The largest turn (degrees) any limb segment would make from pose A to pose B (and which segment). */
	float LargestSegmentTurn(const FPoseFeatures& A, const FPoseFeatures& B, int32* OutSegment = nullptr)
	{
		float Largest = 0.f;
		for (int32 I = 0; I < UE_ARRAY_COUNT(Segments); ++I)
		{
			const int32* S = Segments[I];
			const FVector DA = (A.Points[S[1]] - A.Points[S[0]]).GetSafeNormal(), DB = (B.Points[S[1]] - B.Points[S[0]]).GetSafeNormal();
			const float Turn = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(float(FVector::DotProduct(DA, DB)), -1.f, 1.f)));
			if (Turn > Largest) { Largest = Turn; if (OutSegment) *OutSegment = I; }
		}
		return Largest;
	}
	float PoseError(const FPoseFeatures& A, const FPoseFeatures& B)
	{
		float E = 2.f * FMath::Square(A.PelvisHeight - B.PelvisHeight);
		for (int32 I = 0; I < NumFeatures; ++I) E += FeatureBones[I].Weight * float((A.Points[I] - B.Points[I]).SizeSquared());
		return E;
	}

	/** One candidate start: a clip at a time, its pose features, and where it lies relative to its root (pelvis X / Y,
	 *  pelvis-to-head direction in degrees) for placing the capsule. */
	struct FGetUpSample
	{
		const FGetUp* GetUp = nullptr;
		UAnimSequence* Clip = nullptr;
		float Time = 0.f;
		FPoseFeatures Features;
		FVector2D PelvisFromRoot = FVector2D::ZeroVector;
		float HeadYaw = 0.f;
	};

	FTransform ClipBoneTransform(const UAnimSequence* Clip, const FReferenceSkeleton& Ref, int32 Index, double Time)
	{
		// Component space at Time: the bone's local transform times each parent's, up to the root.
		FTransform Result = FTransform::Identity;
		const FAnimExtractContext Context(Time);
		for (int32 I = Index; I != INDEX_NONE; I = Ref.GetParentIndex(I))
		{
			FTransform Local;
			Clip->GetBoneTransform(Local, FSkeletonPoseBoneIndex(I), Context, false);
			Result = Result * Local;
		}
		return Result;
	}

	/** Every get-up's candidate start frames (the first 0.6 s, every 0.15 s), measured once from the clips. */
	const TArray<FGetUpSample>& GetUpSamples()
	{
		static TArray<FGetUpSample> Samples;
		if (Samples.Num()) return Samples;
		for (const FGetUp& G : GetUps)
		{
			const FString Path = FString::Printf(TEXT("/Game/RPG/Animations/SK_UEFN_Mannequin/RPG_GetUp_%s.RPG_GetUp_%s"), G.Name, G.Name);
			UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, *Path);
			const USkeleton* Skeleton = Clip ? Clip->GetSkeleton() : nullptr;
			if (!Skeleton) continue;
			Clip->AddToRoot();   // kept for the session (the samples point at it)
			const FReferenceSkeleton& Ref = Skeleton->GetReferenceSkeleton();
			const int32 PelvisIndex = Ref.FindBoneIndex(TEXT("pelvis"));
			if (PelvisIndex == INDEX_NONE) continue;
			for (float T = 0.f; T <= FMath::Min(0.6f, Clip->GetPlayLength() * 0.3f) + KINDA_SMALL_NUMBER; T += 0.15f)
			{
				const FVector Root = ClipBoneTransform(Clip, Ref, 0, T).GetLocation();
				const FVector Pelvis = ClipBoneTransform(Clip, Ref, PelvisIndex, T).GetLocation();
				FVector Bones[NumFeatures];
				bool bAll = true;
				for (int32 I = 0; I < NumFeatures; ++I)
				{
					const int32 Index = Ref.FindBoneIndex(FeatureBones[I].Bone);
					if (Index == INDEX_NONE) { bAll = false; break; }
					Bones[I] = ClipBoneTransform(Clip, Ref, Index, T).GetLocation();
				}
				if (!bAll) continue;
				FGetUpSample S;
				S.GetUp = &G;
				S.Clip = Clip;
				S.Time = T;
				const FQuat Chest=ClipBoneTransform(Clip,Ref,Ref.FindBoneIndex(TEXT("spine_05")),T).GetRotation();
				S.Features = MakeFeatures(Pelvis, Bones, float(Pelvis.Z - Root.Z),ChestFacing(Ref,Chest));
				S.PelvisFromRoot = FVector2D(Pelvis.X - Root.X, Pelvis.Y - Root.Y);
				S.HeadYaw = FMath::RadiansToDegrees(FMath::Atan2(Bones[0].Y - Pelvis.Y, Bones[0].X - Pelvis.X));
				Samples.Add(S);
			}
		}
		return Samples;
	}

	const FGetUpSample* ChooseGetUp(const FPoseFeatures& Now,bool bWantsToMove,FName LastStyle,FName LastClip,int32& OutClose,bool& bAnyFadeable)
	{
		const TArray<FGetUpSample>& Samples=GetUpSamples();
		auto Moves=[](const FGetUpSample& S){return S.GetUp->Kind==EGetUpKind::Run || S.GetUp->Kind==EGetUpKind::Crouch;};
		const bool bOnlyMoving=bWantsToMove && Samples.ContainsByPredicate(Moves);
		auto Kind=[&](const FGetUpSample& S){return bOnlyMoving?Moves(S):S.GetUp->Kind!=EGetUpKind::Run;};
		// Limb positions alone cannot distinguish a body rolled onto its front from its back.
		auto Compatible=[&](const FGetUpSample& S){return LargestSegmentTurn(Now,S.Features)<=MaxSegmentTurn && ChestTurn(Now,S.Features)<=MaxSegmentTurn;};
		bAnyFadeable=Samples.ContainsByPredicate([&](const FGetUpSample& S){return Kind(S) && Compatible(S);});
		auto Score=[&](const FGetUpSample& S){return PoseError(Now,S.Features)+TurnCost*(FMath::Square(LargestSegmentTurn(Now,S.Features))+FMath::Square(ChestTurn(Now,S.Features)));};
		TMap<const FGetUp*,const FGetUpSample*> PerClip;
		float Best=TNumericLimits<float>::Max();
		for (const FGetUpSample& S:Samples)
		{
			if (!Kind(S) || (bAnyFadeable && !Compatible(S))) continue;
			const FGetUpSample* const* Previous=PerClip.Find(S.GetUp);
			if (!Previous || Score(S)<Score(**Previous)) PerClip.Add(S.GetUp,&S);
			Best=FMath::Min(Best,Score(S));
		}
		TArray<const FGetUpSample*> Close;
		for (const auto& P:PerClip) if (Score(*P.Value)<=Best*1.25f+25.f) Close.Add(P.Value);
		OutClose=Close.Num();
		if (Close.IsEmpty()) return nullptr;
		// Preserve the same pose-score/limb-turn envelope; variety only resolves safe ties.
		if (!LastStyle.IsNone() && Close.ContainsByPredicate([&](const FGetUpSample* S){return FName(S->GetUp->Style)!=LastStyle;}))
			Close.RemoveAll([&](const FGetUpSample* S){return FName(S->GetUp->Style)==LastStyle;});
		else if (!LastClip.IsNone() && Close.ContainsByPredicate([&](const FGetUpSample* S){return FName(S->Clip->GetName())!=LastClip;}))
			Close.RemoveAll([&](const FGetUpSample* S){return FName(S->Clip->GetName())==LastClip;});
		return Close[FMath::RandRange(0,Close.Num()-1)];
	}
}

URPGBodyImpactComponent::URPGBodyImpactComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;   // after movement: LastVelocity is this frame's, read by next frame's hits
}

void URPGBodyImpactComponent::BeginPlay()
{
	Super::BeginPlay();
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		// GASP's characters let other characters pass through them; bodies collide here (and report it).
		Character->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Character->GetCapsuleComponent()->OnComponentHit.AddDynamic(this, &URPGBodyImpactComponent::OnCapsuleHit);
	}
	// NPCs stand themselves up (their own reactions); anyone else (the player) is stood up here.
	bOwnLimpHandler = !GetOwner()->IsA<ARPGNpcCharacter>();
	GetUpSamples();   // loaded and measured now, not in the middle of the first get-up (a frame hitch)
}

URPGPhysicsReactionComponent* URPGBodyImpactComponent::GetReaction(bool bCreate)
{
	URPGPhysicsReactionComponent* Reaction = GetOwner()->FindComponentByClass<URPGPhysicsReactionComponent>();
	if (!Reaction && bCreate)
	{
		Reaction = NewObject<URPGPhysicsReactionComponent>(GetOwner(), TEXT("RPGImpactRagdoll"));
		Reaction->bHitReactions = false;   // only the whole-body ragdoll
		Reaction->RegisterComponent();
	}
	if (Reaction && !Reaction->OnLimpEnding.IsBoundToObject(this))
		Reaction->OnLimpEnding.AddUObject(this, &URPGBodyImpactComponent::HandleLimpEnding);
	return Reaction;
}

void URPGBodyImpactComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character) return;
	LastVelocity = Character->GetVelocity();
	if (HeldGetUp)
	{
		UAnimInstance* Anim=Character->GetMesh()?Character->GetMesh()->GetAnimInstance():nullptr;
		URPGPhysicsReactionComponent* Reaction=GetReaction(false);
		if (!Anim || !Anim->Montage_IsActive(HeldGetUp))
		{
			HeldGetUp=nullptr;bGetUpAwaitingPhysics=false;
		}
		else if (Reaction && Reaction->GetReactionState()==ERPGPhysicsReactionState::Limp && !Reaction->IsReturning())
		{
			// Interrupted by a new knockdown: never resume the previous get-up later.
			Anim->Montage_Stop(.1f,HeldGetUp);HeldGetUp=nullptr;bGetUpAwaitingPhysics=false;
		}
		else if (!Reaction || Reaction->GetReactionState()==ERPGPhysicsReactionState::Animated)
		{
			bGetUpAwaitingPhysics=false;GetUpResumeTime+=DeltaTime;
			const float Alpha=FMath::SmoothStep(0.f,1.f,FMath::Clamp(GetUpResumeTime/.14f,0.f,1.f));
			Anim->Montage_SetPlayRate(HeldGetUp,GetUpTargetRate*Alpha);
			if (Alpha>=1.f) HeldGetUp=nullptr;
		}
	}
	// Down (and not an NPC, which keeps its capsule where it fell): the capsule follows the body, so the camera does.
	if (bDown && bOwnLimpHandler)
	{
		const FVector Pelvis = Character->GetMesh()->GetBoneLocation(TEXT("pelvis"));
		// The body is detached: teleport only the camera/capsule, leaving physical positions and velocities alone.
		Character->GetCapsuleComponent()->SetWorldLocation(Pelvis + FVector(0.f, 0.f, 20.f), false, nullptr, ETeleportType::TeleportPhysics);
	}
	else if (bMeshFreed)
	{
		// A simulated root cannot be attached yet. Keep the animation frame beneath the capsule during the pinned
		// return blend, then restore the hierarchy once PhysicsControl has made the bodies kinematic.
		ReturnMeshToCapsule(Character);
	}
}

bool URPGBodyImpactComponent::IsRecovering() const
{
	if (bDown || bGetUpAwaitingPhysics) return true;
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UAnimInstance* Anim = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	return Anim && RecoveryMontage && Anim->Montage_IsActive(RecoveryMontage);
}

void URPGBodyImpactComponent::OnCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	ACharacter* Other = Cast<ACharacter>(OtherActor);
	URPGBodyImpactComponent* OtherImpact = Other ? Other->FindComponentByClass<URPGBodyImpactComponent>() : nullptr;
	const UWorld* World = GetWorld();
	if (!OtherImpact || bDown || OtherImpact->IsDown() || !World || World->GetTimeSeconds() - LastImpactTime < 0.3f) return;

	// Closing speed along the line between the two (the hit normal points back at us).
	const FVector Normal = -Hit.ImpactNormal.GetSafeNormal();
	const FVector Relative = LastVelocity - Other->GetVelocity();
	const float Closing = float(FVector::DotProduct(Relative, Normal));
	if (Closing < MinBumpSpeed) return;
	LastImpactTime = OtherImpact->LastImpactTime = World->GetTimeSeconds();

	// Momentum along the line of impact, with a little bounce: the struck body takes (1+e) m1/(m1+m2) of the closing
	// speed, the mover keeps (m1 - e m2)/(m1+m2) of it. Sideways motion mostly carries on.
	const float M1 = RPGBodyMass::TotalKgFor(BodyWeightLbs), M2 = RPGBodyMass::TotalKgFor(OtherImpact->BodyWeightLbs);
	const float Struck = (1.f + Restitution) * M1 / (M1 + M2) * Closing;
	const float Kept = (M1 - Restitution * M2) / (M1 + M2) * Closing;
	const FVector Sideways = Relative - Normal * Closing;
	// How hard the one run into feels it: on foot at most a knockback; only a flying crash knocks it down.
	FRPGPowerInput BumpInput = URPGPowerSystem::MakeBumpInput(GetOwner(), Other, Normal, Closing);
	if (const URPGHitOutcomeComponent* Outcome = Other->FindComponentByClass<URPGHitOutcomeComponent>()) Outcome->PrepareInput(BumpInput);
	const FRPGPowerResult Felt = URPGPowerSystem::Calculate(BumpInput);
	const bool bSelfDown = Felt.bKnockdown && Closing >= SelfKnockDownSpeed;
	const FVector Up(0.f, 0.f, Felt.bKnockdown ? Closing * Lift : 0.f);
	if (Felt.bKnockdown) OtherImpact->KnockDown(Other->GetVelocity() + Normal * Struck + Sideways * 0.3f + Up, Hit.ImpactPoint);
	else OtherImpact->Bump(Normal * Struck, Hit.ImpactPoint, Felt.Reaction);
	UE_LOG(LogRPGInput, Log, TEXT("BODY IMPACT: %s into %s at %.0f cm/s -> %s, struck %.0f cm/s, mover keeps %.0f cm/s%s"), *GetOwner()->GetActorNameOrLabel(),
		*Other->GetActorNameOrLabel(), Closing, *UEnum::GetDisplayValueAsText(Felt.Reaction).ToString(), Struck, Kept, bSelfDown ? TEXT(", mover goes down too") : TEXT(""));

	ACharacter* Self = Cast<ACharacter>(GetOwner());
	const FVector After = LastVelocity - Normal * (Closing - Kept);
	if (bSelfDown) KnockDown(After + Up * 0.5f, Hit.ImpactPoint);
	else if (Self && Self->GetCharacterMovement()) Self->GetCharacterMovement()->Velocity = After;   // carries on, slowed by the impact
}

void URPGBodyImpactComponent::Bump(const FVector& Velocity, const FVector& Point, ERPGPowerReaction Reaction)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || bDown) return;
	LastBumpReaction = Reaction;
	++BumpCount;
	if (OnBumped.IsBound()) { OnBumped.Broadcast(Velocity, Point, Reaction); return; }
	if (URPGHitOutcomeComponent* Outcome = GetOwner()->FindComponentByClass<URPGHitOutcomeComponent>()) { Outcome->ReceiveBump(Velocity, Reaction); return; }
	// No reactions of its own (the player): it just takes the push on its feet.
	if (UCharacterMovementComponent* Move = Character->GetCharacterMovement(); Move && Move->IsMovingOnGround())
		Move->Velocity += FVector(Velocity.X, Velocity.Y, 0.f);
}

void URPGBodyImpactComponent::KnockDown(const FVector& Velocity, const FVector& Point, bool bFallPrepared)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	URPGPhysicsReactionComponent* Reaction = GetReaction(true);
	if (!Character || !Reaction || bDown) return;
	// It fights the fall: arms out toward where it is going, a braced ragdoll.
	if (!bFallPrepared)
		if (URPGHitOutcomeComponent* Outcome = GetOwner()->FindComponentByClass<URPGHitOutcomeComponent>())
			Outcome->BeginFall(Velocity, Outcome->IsKnockedOut());
	bDown = true;
	// Out of the air, no walking: the body is physical now.
	if (URPGMovementAbilityComponent* Abilities = GetOwner()->FindComponentByClass<URPGMovementAbilityComponent>()) Abilities->StopFlight();
	if (UCharacterMovementComponent* Move = Character->GetCharacterMovement()) { Move->StopMovementImmediately(); Move->DisableMovement(); }
	if (bOwnLimpHandler) { Character->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly); FreeMeshFromCapsule(Character); }
	// Stays limp for its flight (up and down again) and then DownTime once it settles.
	const float Gravity = 980.f;   // impacts fall under real gravity
	Reaction->Launch(Velocity, Point, DownTime + FMath::Max(0.f, 2.f * float(Velocity.Z) / Gravity), true);
	OnKnockedDown.Broadcast();
}

void URPGBodyImpactComponent::HandleLimpEnding()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || !bDown) return;
	if (!bOwnLimpHandler) { bDown = false; return; }   // an NPC stands itself up
	Character->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	float Duration = 0.f;
	// Holding a direction while down: get up into a run.
	StandUp(Character, 1.5f, Duration, Character->GetPendingMovementInputVector().SizeSquared() > 0.04f || Character->GetLastMovementInputVector().SizeSquared() > 0.04f);
	ReturnMeshToCapsule(Character);   // (StandUp does it before placing the capsule; this covers it giving up early)
	bDown = false;
}

void URPGBodyImpactComponent::FreeMeshFromCapsule(ACharacter* Character)
{
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (!Mesh || bMeshFreed) return;
	MeshParent = Mesh->GetAttachParent();
	MeshSocket = Mesh->GetAttachSocketName();
	MeshRelative = Mesh->GetRelativeTransform();
	if (URPGFlightAnimationComponent* Flight = Character->FindComponentByClass<URPGFlightAnimationComponent>())
		MeshRelative = Flight->ReleaseBodyToPhysics(MeshRelative);
	Mesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	bMeshFreed = true;
}

void URPGBodyImpactComponent::ReturnMeshToCapsule(ACharacter* Character)
{
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (!Mesh || !bMeshFreed) return;
	USceneComponent* Parent = MeshParent.Get();
	if (!Parent) return;
	const FTransform Target = MeshRelative * Parent->GetSocketTransform(MeshSocket);
	// Only restore the animation component's frame. A physics teleport here would also translate the ragdoll,
	// destroying the pose that ReturnToAnimated is about to pin for the get-up blend.
	Mesh->MoveComponent(Target.GetLocation() - Mesh->GetComponentLocation(), Target.GetRotation(), false, nullptr, MOVECOMP_SkipPhysicsMove);
	const URPGPhysicsReactionComponent* Reaction = Character->FindComponentByClass<URPGPhysicsReactionComponent>();
	if (Reaction && Reaction->GetReactionState() == ERPGPhysicsReactionState::Limp) return;
	if (!Mesh->AttachToComponent(Parent, FAttachmentTransformRules::KeepWorldTransform, MeshSocket)) return;
	Mesh->SetRelativeScale3D(MeshRelative.GetScale3D());
	bMeshFreed = false;
	MeshParent.Reset();
}

UAnimMontage* URPGBodyImpactComponent::StandUp(ACharacter* Character, float Rate, float& OutDuration, bool bWantsToMove)
{
	OutDuration = 0.f;
	USkeletalMeshComponent* Body = Character ? Character->GetMesh() : nullptr;
	if (!Body) return nullptr;
	const FVector Pelvis = Body->GetBoneLocation(TEXT("pelvis"));
	FHitResult Ground;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RPGStandUp), false, Character);
	const bool bGround = Character->GetWorld()->LineTraceSingleByChannel(Ground, Pelvis + FVector(0.f, 0.f, 80.f), Pelvis - FVector(0.f, 0.f, 300.f), ECC_Visibility, Params);
	const float FloorZ = bGround ? float(Ground.ImpactPoint.Z) : float(Pelvis.Z) - 10.f;

	// The body's actual pose, compared with every get-up's first frames: the closest clip AND the frame to start it
	// from (wanting to move: only the get-ups that move off - into a run or a crouch scramble). A little variety among
	// near-equal matches.
	FVector Bones[NumFeatures];
	for (int32 I = 0; I < NumFeatures; ++I) Bones[I] = Body->GetBoneLocation(FeatureBones[I].Bone);
	const FPoseFeatures Now = MakeFeatures(Pelvis, Bones, float(Pelvis.Z) - FloorZ,
		ChestFacing(Body->GetSkeletalMeshAsset()->GetRefSkeleton(),Body->GetBoneQuaternion(TEXT("spine_05"))));
	URPGBodyImpactComponent* History=Character->FindComponentByClass<URPGBodyImpactComponent>();
	int32 CloseCount=0; bool bAnyFadeable=false;
	const FGetUpSample* Selected=ChooseGetUp(Now,bWantsToMove,History?History->LastGetUpStyle:NAME_None,History?History->LastGetUpClip:NAME_None,CloseCount,bAnyFadeable);
	if (!Selected) return nullptr;
	const FGetUpSample& Pick=*Selected;
	// A start still far from the body (limbs turning a long way) fades in more slowly: limbs untwist at a human speed.
	int32 TurnSegment = 0;
	const float Turn = LargestSegmentTurn(Now, Pick.Features, &TurnSegment);
	const float TorsoTurn=ChestTurn(Now,Pick.Features);
	const float AlignmentTurn=FMath::Max(Turn,TorsoTurn);
	if (AlignmentTurn > MaxSegmentTurn)
	{
		// No start close enough: the landing is written to Saved/GetUpMisses.csv (body parts relative to the hips, in
		// the matching frame: forward = hips to head along the ground), to author a get-up that starts like it.
		FString Row = FString::Printf(TEXT("%s,%s,%.0f,%s,%.1f"), *Character->GetActorNameOrLabel(), *Pick.Clip->GetName(), AlignmentTurn, TorsoTurn>Turn?TEXT("torso"):SegmentNames[TurnSegment], Now.PelvisHeight);
		for (int32 I = 0; I < NumFeatures; ++I) Row += FString::Printf(TEXT(",%s,%.1f,%.1f,%.1f"), FeatureBones[I].Bone, Now.Points[I].X, Now.Points[I].Y, Now.Points[I].Z);
		FFileHelper::SaveStringToFile(Row + LINE_TERMINATOR, *(FPaths::ProjectSavedDir() / TEXT("GetUpMisses.csv")), FFileHelper::EEncodingOptions::AutoDetect,
			&IFileManager::Get(), FILEWRITE_Append);
	}
	if (URPGPhysicsReactionComponent* Reaction = Character->FindComponentByClass<URPGPhysicsReactionComponent>())
		Reaction->NextReturnBlendTime = FMath::GetMappedRangeValueClamped(FVector2f(60.f, 150.f), FVector2f(0.5f, 1.f), AlignmentTurn);

	// Turn and place the capsule so that frame lies on the body (the fade back to animation blends into it).
	const FVector Head = Body->GetBoneLocation(TEXT("head")) - Pelvis;
	const float MeshYaw = FMath::RadiansToDegrees(FMath::Atan2(Head.Y, Head.X)) - Pick.HeadYaw;
	const FVector Offset = FRotator(0.f, MeshYaw, 0.f).RotateVector(FVector(Pick.PelvisFromRoot.X, Pick.PelvisFromRoot.Y, 0.f));
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float RelativeYaw = History && History->bMeshFreed ? History->MeshRelative.Rotator().Yaw : Body->GetRelativeRotation().Yaw;
	// Align the capsule independently of the physical pose, for NPCs as well as the player. Reattach only after
	// placing it, then restore the animation frame without moving the bodies used by the return blend.
	if (History) History->FreeMeshFromCapsule(Character);
	Character->SetActorLocationAndRotation(FVector(Pelvis.X - Offset.X, Pelvis.Y - Offset.Y, FloorZ + HalfHeight + 1.f),
		FRotator(0.f, MeshYaw - RelativeYaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	if (History) History->ReturnMeshToCapsule(Character);
	if (AController* Controller = Character->GetController())
		if (Controller->IsPlayerController()) Controller->SetControlRotation(FRotator(Controller->GetControlRotation().Pitch, Character->GetActorRotation().Yaw, 0.f));
	if (UCharacterMovementComponent* Move = Character->GetCharacterMovement()) Move->SetMovementMode(MOVE_Walking);

	UAnimInstance* Anim = Body->GetAnimInstance();
	if (!Anim) return nullptr;
	UAnimMontage* Montage = Anim->PlaySlotAnimationAsDynamicMontage(Pick.Clip, TEXT("DefaultSlot"), 0.f, 0.35f, Rate, 1, -1.f, Pick.Time);
	if (!Montage) return nullptr;
	if (History) History->RecoveryMontage = Montage;
	if (History) { History->LastGetUpClip=Pick.Clip->GetFName(); History->LastGetUpStyle=FName(Pick.GetUp->Style); }
	OutDuration = (Pick.Clip->GetPlayLength() - Pick.Time) / FMath::Max(Rate, 0.1f);
	if (History)
		if (URPGPhysicsReactionComponent* Reaction=Character->FindComponentByClass<URPGPhysicsReactionComponent>(); Reaction && Reaction->GetReactionState()==ERPGPhysicsReactionState::Limp)
		{
			// The old physics pose is pinned while fading. Moving the target torso beneath those
			// pinned arms can bend a shoulder backwards even when both endpoints are anatomical.
			// Align with the matched, stationary start first, then ease into its authored motion.
			Anim->Montage_SetPlayRate(Montage,0.f);
			History->HeldGetUp=Montage;History->GetUpTargetRate=Rate;
			History->GetUpResumeTime=0.f;History->bGetUpAwaitingPhysics=true;
			OutDuration+=FMath::Max(0.f,Reaction->NextReturnBlendTime)+.14f;
		}
	UE_LOG(LogRPGInput, Log, TEXT("%s gets up with %s from %.2fs (pose match %.0f cm, largest limb turn %.0f deg (%s)%s, %d close clips, style %s) at %.2fx"),
		*Character->GetActorNameOrLabel(), *Pick.Clip->GetName(), Pick.Time, FMath::Sqrt(PoseError(Now, Pick.Features) / (NumFeatures + 2)), Turn, SegmentNames[TurnSegment],
		bAnyFadeable ? TEXT("") : TEXT(", none closer"), CloseCount, Pick.GetUp->Style, Rate);
	return Montage;
}

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGGetUpVarietyTest,"RPG.Npc.GetUpVariety",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FRPGGetUpVarietyTest::RunTest(const FString&)
{
	const auto& Samples=GetUpSamples();
	const TSet<FName> Allowed={FName(TEXT("RPG_GetUp_FaceDown_Capture8614")),FName(TEXT("RPG_GetUp_FaceUp_Capture8611"))};
	TSet<FName> Loaded,Styles;
	for (const auto& S:Samples)
	{
		Loaded.Add(S.Clip->GetFName()); Styles.Add(FName(S.GetUp->Style));
		TestTrue(TEXT("Every recovery candidate belongs to the two mocap clips"),Allowed.Contains(S.Clip->GetFName()));
	}
	TestEqual(TEXT("Only the two mocap recoveries are loaded"),Loaded.Num(),2);
	TestEqual(TEXT("Both recorded styles are present"),Styles.Num(),2);
	int32 Poses=0,Selections=0;
	for (const auto& Source:Samples)
	{
		if (Source.Time!=0.f) continue;
		++Poses;
		for (const float Roll:{0.f,-90.f,90.f})
		{
			FPoseFeatures Landing=Source.Features;
			const FQuat Tilt(FVector::ForwardVector,FMath::DegreesToRadians(Roll));
			for (FVector& Point:Landing.Points) Point=Tilt.RotateVector(Point);
			Landing.ChestFacing=Tilt.RotateVector(Landing.ChestFacing);
			for (const bool bMove:{false,true})
			{
				int32 Count=0;bool bFadeable=false;
				const auto* Pick=ChooseGetUp(Landing,bMove,FName(Source.GetUp->Style),Source.Clip->GetFName(),Count,bFadeable);
				if (!TestNotNull(TEXT("Front/back/side landing can recover with the mocap-only pool"),Pick)) continue;
				TestTrue(TEXT("Moving intent and history cannot select a retired clip"),Allowed.Contains(Pick->Clip->GetFName()));
				if (bFadeable)
				{
					TestTrue(TEXT("Compatible selection retains limb-turn safety"),LargestSegmentTurn(Landing,Pick->Features)<=MaxSegmentTurn);
					TestTrue(TEXT("Compatible selection retains torso-turn safety"),ChestTurn(Landing,Pick->Features)<=MaxSegmentTurn);
				}
				if (Roll==0.f) TestEqual(TEXT("History does not force the opposite-facing capture for variety"),Pick->Clip->GetFName(),Source.Clip->GetFName());
				++Selections;
			}
		}
	}
	TestEqual(TEXT("Both recorded starting poses tested"),Poses,2);
	UE_LOG(LogRPGInput,Display,TEXT("GETUP MOCAP ONLY loaded=%d styles=%d sourcePoses=%d selections=%d"),Loaded.Num(),Styles.Num(),Poses,Selections);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGGetUpCaptureTest,"RPG.Npc.GetUpCapture",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FRPGGetUpCaptureTest::RunTest(const FString&)
{
	const FName Name(TEXT("RPG_GetUp_FaceDown_Capture8614"));
	const auto& Samples=GetUpSamples();
	const FGetUpSample* Start=Samples.FindByPredicate([&](const FGetUpSample& S){return S.Clip->GetFName()==Name && S.Time==0.f;});
	if (!TestNotNull(TEXT("Approved capture is in the production get-up table"),Start)) return false;
	UAnimSequence* Clip=Start->Clip;
	FPoseFeatures Opposite=Start->Features;Opposite.ChestFacing=-Opposite.ChestFacing;
	TestTrue(TEXT("Matching limb positions do not hide an opposite-facing torso"),ChestTurn(Start->Features,Opposite)>179.f);
	TestTrue(TEXT("Captured performance and closing steps retained"),Clip->GetPlayLength()>7.f && Clip->GetPlayLength()<9.f);
	int32 Close=0;bool bFadeable=false;
	const FGetUpSample* Pick=ChooseGetUp(Start->Features,false,NAME_None,NAME_None,Close,bFadeable);
	if (TestNotNull(TEXT("Matching prone landing has a recovery"),Pick))
	{
		TestEqual(TEXT("Matching capture start selects the recorded recovery"),Pick->Clip->GetFName(),Name);
		TestTrue(TEXT("Capture selection keeps the limb-turn bound"),bFadeable && LargestSegmentTurn(Start->Features,Pick->Features)<=MaxSegmentTurn);
	}
	const FReferenceSkeleton& Ref=Clip->GetSkeleton()->GetReferenceSkeleton();
	for (double T=0.;T<=Clip->GetPlayLength();T+=1./60.)
	{
		const FTransform Root=ClipBoneTransform(Clip,Ref,0,T);
		if (!Root.GetLocation().IsNearlyZero(.01f) || !Root.GetRotation().Equals(FQuat::Identity,.0001f))
		{
			AddError(TEXT("Recorded get-up moves the root; recovery placement owns displacement"));break;
		}
	}
	UAnimSequence* Idle=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Characters/UEFN_Mannequin/Animations/Idle/M_Neutral_Stand_Idle_Loop.M_Neutral_Stand_Idle_Loop"));
	if (TestNotNull(TEXT("GASP idle exists"),Idle))
	{
		TestEqual(TEXT("Capture uses the gameplay skeleton"),Clip->GetSkeleton(),Idle->GetSkeleton());
		for (const FFeatureBone& Bone:FeatureBones)
		{
			const int32 Index=Ref.FindBoneIndex(Bone.Bone);
			const FVector End=ClipBoneTransform(Clip,Ref,Index,Clip->GetPlayLength()).GetLocation();
			const FVector Stand=ClipBoneTransform(Idle,Ref,Index,0.).GetLocation();
			TestTrue(FString::Printf(TEXT("Capture ends in GASP idle: %s"),Bone.Bone),!End.ContainsNaN() && End.Equals(Stand,.5f));
		}
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGGetUpCapture8611Test,"RPG.Npc.GetUpCapture8611",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FRPGGetUpCapture8611Test::RunTest(const FString&)
{
	const FName Name(TEXT("RPG_GetUp_FaceUp_Capture8611"));
	const auto& Samples=GetUpSamples();
	const FGetUpSample* Start=Samples.FindByPredicate([&](const FGetUpSample& S){return S.Clip->GetFName()==Name && S.Time==0.f;});
	if (!TestNotNull(TEXT("Approved capture is in the production get-up table"),Start)) return false;
	UAnimSequence* Clip=Start->Clip;
	FPoseFeatures Opposite=Start->Features;Opposite.ChestFacing=-Opposite.ChestFacing;
	TestTrue(TEXT("Matching limb positions do not hide an opposite-facing torso"),ChestTurn(Start->Features,Opposite)>179.f);
	TestTrue(TEXT("Captured performance and closing steps retained"),Clip->GetPlayLength()>5.f && Clip->GetPlayLength()<8.f);
	int32 Close=0;bool bFadeable=false;
	const FGetUpSample* Pick=ChooseGetUp(Start->Features,false,NAME_None,NAME_None,Close,bFadeable);
	if (TestNotNull(TEXT("Matching supine landing has a recovery"),Pick))
	{
		TestEqual(TEXT("Matching capture start selects the recorded recovery"),Pick->Clip->GetFName(),Name);
		TestTrue(TEXT("Capture selection keeps the limb-turn bound"),bFadeable && LargestSegmentTurn(Start->Features,Pick->Features)<=MaxSegmentTurn);
	}
	const FReferenceSkeleton& Ref=Clip->GetSkeleton()->GetReferenceSkeleton();
	for (double T=0.;T<=Clip->GetPlayLength();T+=1./60.)
	{
		const FTransform Root=ClipBoneTransform(Clip,Ref,0,T);
		if (!Root.GetLocation().IsNearlyZero(.01f) || !Root.GetRotation().Equals(FQuat::Identity,.0001f))
		{
			AddError(TEXT("Recorded get-up moves the root; recovery placement owns displacement"));break;
		}
	}
	UAnimSequence* Idle=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Characters/UEFN_Mannequin/Animations/Idle/M_Neutral_Stand_Idle_Loop.M_Neutral_Stand_Idle_Loop"));
	if (TestNotNull(TEXT("GASP idle exists"),Idle))
	{
		TestEqual(TEXT("Capture uses the gameplay skeleton"),Clip->GetSkeleton(),Idle->GetSkeleton());
		for (const FFeatureBone& Bone:FeatureBones)
		{
			const int32 Index=Ref.FindBoneIndex(Bone.Bone);
			const FVector End=ClipBoneTransform(Clip,Ref,Index,Clip->GetPlayLength()).GetLocation();
			const FVector Stand=ClipBoneTransform(Idle,Ref,Index,0.).GetLocation();
			TestTrue(FString::Printf(TEXT("Capture ends in GASP idle: %s"),Bone.Bone),!End.ContainsNaN() && End.Equals(Stand,.5f));
		}
	}
	return true;
}
#endif
