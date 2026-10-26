#include "RPGBodyImpactComponent.h"

#include "RPGBodyMass.h"
#include "RPGMovementAbilityComponent.h"
#include "RPGNpcCharacter.h"
#include "RPGPhysicsReactionComponent.h"
#include "RPG_CODEX.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimTypes.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	/** GASP's 12 get-ups and their kind (stand up, crouch scramble, roll then stand, up into a run). */
	enum class EGetUpKind : uint8 { Stand, Crouch, Roll, Run };
	struct FGetUp { const TCHAR* Name; EGetUpKind Kind; };
	const FGetUp GetUps[] = {
		{ TEXT("stand_F"), EGetUpKind::Stand }, { TEXT("stand_B"), EGetUpKind::Stand }, { TEXT("stand_L"), EGetUpKind::Stand }, { TEXT("stand_R"), EGetUpKind::Stand },
		{ TEXT("crouch_F"), EGetUpKind::Crouch }, { TEXT("crouch_B"), EGetUpKind::Crouch }, { TEXT("crouch_L"), EGetUpKind::Crouch }, { TEXT("crouch_R"), EGetUpKind::Crouch },
		{ TEXT("roll_L"), EGetUpKind::Roll }, { TEXT("roll_R"), EGetUpKind::Roll },
		{ TEXT("run_F"), EGetUpKind::Run }, { TEXT("run_B"), EGetUpKind::Run },
	};

	/** Body pose features compared between the ragdoll and the clips: where these bones are relative to the hips, in a
	 *  frame turned with the body (X along the hips-to-head direction on the ground, Z up), and how much each matters. */
	struct FFeatureBone { const TCHAR* Bone; float Weight; };
	const FFeatureBone FeatureBones[] = {
		{ TEXT("head"), 1.5f }, { TEXT("spine_05"), 1.5f },
		{ TEXT("hand_l"), 1.f }, { TEXT("hand_r"), 1.f }, { TEXT("lowerarm_l"), 0.7f }, { TEXT("lowerarm_r"), 0.7f },
		{ TEXT("calf_l"), 0.7f }, { TEXT("calf_r"), 0.7f }, { TEXT("foot_l"), 1.f }, { TEXT("foot_r"), 1.f },
	};
	constexpr int32 NumFeatures = UE_ARRAY_COUNT(FeatureBones);

	struct FPoseFeatures
	{
		FVector Points[NumFeatures];
		float PelvisHeight = 0.f;
	};
	FPoseFeatures MakeFeatures(const FVector& Pelvis, const FVector* Bones, float PelvisHeight)
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
		return F;
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
			const FString Path = FString::Printf(TEXT("/Game/Characters/UEFN_Mannequin/Animations/Ragdoll/M_ragdoll_getup_%s.M_ragdoll_getup_%s"), G.Name, G.Name);
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
				S.Features = MakeFeatures(Pelvis, Bones, float(Pelvis.Z - Root.Z));
				S.PelvisFromRoot = FVector2D(Pelvis.X - Root.X, Pelvis.Y - Root.Y);
				S.HeadYaw = FMath::RadiansToDegrees(FMath::Atan2(Bones[0].Y - Pelvis.Y, Bones[0].X - Pelvis.X));
				Samples.Add(S);
			}
		}
		return Samples;
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
	// Down (and not an NPC, which keeps its capsule where it fell): the capsule follows the body, so the camera does.
	if (bDown && bOwnLimpHandler)
	{
		const FVector Pelvis = Character->GetMesh()->GetBoneLocation(TEXT("pelvis"));
		// No teleport flag: a physics teleport would carry the simulated body along with the capsule, which then moved the
		// hips, which moved the capsule... and threw the body off without end.
		Character->GetCapsuleComponent()->SetWorldLocation(Pelvis + FVector(0.f, 0.f, 20.f), false, nullptr, ETeleportType::None);
	}
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
	if (Closing < KnockDownSpeed) return;
	LastImpactTime = OtherImpact->LastImpactTime = World->GetTimeSeconds();

	// Momentum along the line of impact, with a little bounce: the struck body takes (1+e) m1/(m1+m2) of the closing
	// speed, the mover keeps (m1 - e m2)/(m1+m2) of it. Sideways motion mostly carries on.
	const float M1 = RPGBodyMass::TotalKgFor(BodyWeightLbs), M2 = RPGBodyMass::TotalKgFor(OtherImpact->BodyWeightLbs);
	const float Struck = (1.f + Restitution) * M1 / (M1 + M2) * Closing;
	const float Kept = (M1 - Restitution * M2) / (M1 + M2) * Closing;
	const FVector Sideways = Relative - Normal * Closing;
	const FVector Up(0.f, 0.f, Closing * Lift);
	OtherImpact->KnockDown(Other->GetVelocity() + Normal * Struck + Sideways * 0.3f + Up, Hit.ImpactPoint);
	UE_LOG(LogRPGInput, Log, TEXT("BODY IMPACT: %s into %s at %.0f cm/s -> struck %.0f cm/s, mover keeps %.0f cm/s%s"), *GetOwner()->GetActorNameOrLabel(),
		*Other->GetActorNameOrLabel(), Closing, Struck, Kept, Closing >= SelfKnockDownSpeed ? TEXT(", mover goes down too") : TEXT(""));

	ACharacter* Self = Cast<ACharacter>(GetOwner());
	const FVector After = LastVelocity - Normal * (Closing - Kept);
	if (Closing >= SelfKnockDownSpeed) KnockDown(After + Up * 0.5f, Hit.ImpactPoint);
	else if (Self && Self->GetCharacterMovement()) Self->GetCharacterMovement()->Velocity = After;   // carries on, slowed by the impact
}

void URPGBodyImpactComponent::KnockDown(const FVector& Velocity, const FVector& Point)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	URPGPhysicsReactionComponent* Reaction = GetReaction(true);
	if (!Character || !Reaction || bDown) return;
	bDown = true;
	// Out of the air, no walking: the body is physical now.
	if (URPGMovementAbilityComponent* Abilities = GetOwner()->FindComponentByClass<URPGMovementAbilityComponent>()) Abilities->StopFlight();
	if (UCharacterMovementComponent* Move = Character->GetCharacterMovement()) { Move->StopMovementImmediately(); Move->DisableMovement(); }
	if (bOwnLimpHandler) Character->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
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
	StandUp(Character, 1.f, Duration, Character->GetPendingMovementInputVector().SizeSquared() > 0.04f || Character->GetLastMovementInputVector().SizeSquared() > 0.04f);
	bDown = false;
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
	const FPoseFeatures Now = MakeFeatures(Pelvis, Bones, float(Pelvis.Z) - FloorZ);
	const TArray<FGetUpSample>& Samples = GetUpSamples();
	auto Allowed = [bWantsToMove](const FGetUpSample& S)
	{
		const bool bMoves = S.GetUp->Kind == EGetUpKind::Run || S.GetUp->Kind == EGetUpKind::Crouch;
		return bWantsToMove ? bMoves : S.GetUp->Kind != EGetUpKind::Run;
	};
	float Best = TNumericLimits<float>::Max();
	for (const FGetUpSample& S : Samples) if (Allowed(S)) Best = FMath::Min(Best, PoseError(Now, S.Features));
	TArray<const FGetUpSample*> Close;
	for (const FGetUpSample& S : Samples) if (Allowed(S) && PoseError(Now, S.Features) <= Best * 1.25f + 25.f) Close.Add(&S);
	if (Close.Num() == 0) return nullptr;
	const FGetUpSample& Pick = *Close[FMath::RandRange(0, Close.Num() - 1)];

	// Turn and place the capsule so that frame lies on the body (the fade back to animation blends into it).
	const FVector Head = Body->GetBoneLocation(TEXT("head")) - Pelvis;
	const float MeshYaw = FMath::RadiansToDegrees(FMath::Atan2(Head.Y, Head.X)) - Pick.HeadYaw;
	const FVector Offset = FRotator(0.f, MeshYaw, 0.f).RotateVector(FVector(Pick.PelvisFromRoot.X, Pick.PelvisFromRoot.Y, 0.f));
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Character->SetActorLocationAndRotation(FVector(Pelvis.X - Offset.X, Pelvis.Y - Offset.Y, FloorZ + HalfHeight + 1.f),
		FRotator(0.f, MeshYaw - Body->GetRelativeRotation().Yaw, 0.f), false, nullptr, ETeleportType::None);
	if (AController* Controller = Character->GetController())
		if (Controller->IsPlayerController()) Controller->SetControlRotation(FRotator(Controller->GetControlRotation().Pitch, Character->GetActorRotation().Yaw, 0.f));
	if (UCharacterMovementComponent* Move = Character->GetCharacterMovement()) Move->SetMovementMode(MOVE_Walking);

	UAnimInstance* Anim = Body->GetAnimInstance();
	if (!Anim) return nullptr;
	UAnimMontage* Montage = Anim->PlaySlotAnimationAsDynamicMontage(Pick.Clip, TEXT("DefaultSlot"), 0.f, 0.35f, Rate, 1, -1.f, Pick.Time);
	OutDuration = (Pick.Clip->GetPlayLength() - Pick.Time) / FMath::Max(Rate, 0.1f);
	UE_LOG(LogRPGInput, Log, TEXT("%s gets up with %s from %.2fs (pose match %.0f cm, %d close matches) at %.2fx"), *Character->GetActorNameOrLabel(),
		*Pick.Clip->GetName(), Pick.Time, FMath::Sqrt(Best / 12.f), Close.Num(), Rate);
	return Montage;
}
