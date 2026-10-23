#include "RPGFlightAnimationComponent.h"

#include "RPGCombatStateComponent.h"
#include "RPGMovementAbilityComponent.h"
#include "RPGPhysicsReactionComponent.h"
#include "RPGAnimInstance.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

URPGFlightAnimationComponent::URPGFlightAnimationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	Hover = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_FlyHover.RPG_Proto_FlyHover"));
	Forward = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_FlyForward.RPG_Proto_FlyForward"));
	Fast = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_FlyFast.RPG_Proto_FlyFast"));
}

void URPGFlightAnimationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	UAnimInstance* Instance = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (!Movement || !Instance) return;

	if (DeltaTime > 0.f)
	{
		const FVector Accel = (Movement->Velocity - FlowLastVelocity) / DeltaTime;
		FlowLastVelocity = Movement->Velocity;
		UpdateLimbFlow(Character->GetMesh(), Instance, Accel, Movement->Velocity, Movement->IsFlying(), DeltaTime);
	}
	if (!Movement->IsFlying())
	{
		ResetBodyReaction(Character->GetMesh());
		if (URPGPhysicsReactionComponent* Physics = GetOwner()->FindComponentByClass<URPGPhysicsReactionComponent>()) Physics->SetFlightFollow(false);
		if (Playing && Instance->Montage_IsPlaying(Playing)) Instance->Montage_Stop(0.25f, Playing);
		Playing = nullptr;
		PlayingClip = nullptr;
		return;
	}

	UpdateBodyReaction(Character->GetMesh(), Movement->Velocity, DeltaTime);
	// Follow-through: the chest and head lead (the animation and lean above), the rest trails on physics.
	URPGPhysicsReactionComponent* Physics = GetOwner()->FindComponentByClass<URPGPhysicsReactionComponent>();
	if (!Physics && bFollowThrough)
	{
		Physics = NewObject<URPGPhysicsReactionComponent>(GetOwner(), TEXT("RPGFlightPhysics"));
		Physics->bHitReactions = false;
		Physics->RegisterComponent();
	}
	if (Physics) Physics->SetFlightFollow(bFollowThrough);

	// Which pose: fast flight (L3) or high speed -> fast; moving -> forward; otherwise hover.
	const URPGMovementAbilityComponent* Abilities = GetOwner()->FindComponentByClass<URPGMovementAbilityComponent>();
	const float Speed = float(Movement->Velocity.Size2D());
	TSoftObjectPtr<UAnimSequence> Want = Hover;
	const TCHAR* Name = TEXT("RPG_Proto_FlyHover");
	if ((Abilities && Abilities->IsFastFlight() && Speed > ForwardSpeed) || Speed >= FastSpeed) { Want = Fast; Name = TEXT("RPG_Proto_FlyFast"); }
	else if (Speed >= ForwardSpeed) { Want = Forward; Name = TEXT("RPG_Proto_FlyForward"); }
	// The clip for this character's skeleton: the set one if it fits, else the generator's copy for that skeleton
	// (/Game/RPG/Animations/<skeleton>/, e.g. the GASP player's SK_UEFN_Mannequin).
	UAnimSequence* Clip = Want.LoadSynchronous();
	if (!Clip || Clip->GetSkeleton() != Instance->CurrentSkeleton)
	{
		Clip = Instance->CurrentSkeleton ? LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/RPG/Animations/%s/%s.%s"),
			*Instance->CurrentSkeleton->GetName(), Name, Name), nullptr, LOAD_NoWarn | LOAD_Quiet) : nullptr;
		if (Clip && Clip->GetSkeleton() != Instance->CurrentSkeleton) Clip = nullptr;
	}
	if (!Clip) return;

	// While an attack, charge or block is running it owns the body: wait, then come back. (The combat stance loop is not
	// an attack: flight replaces it.) Otherwise switch pose when it changes.
	const URPGCombatStateComponent* State = GetOwner()->FindComponentByClass<URPGCombatStateComponent>();
	if (State && (State->AttackPhase != ERPGAttackPhase::None || State->bIsChargingAttack || State->bIsBlocking))
	{
		Playing = nullptr; PlayingClip = nullptr;
		return;
	}
	const bool bOurs = Playing && Instance->Montage_IsPlaying(Playing);
	if (bOurs && PlayingClip == Clip) return;

	Playing = Instance->PlaySlotAnimationAsDynamicMontage(Clip, TEXT("DefaultSlot"), BlendTime, BlendTime, 1.f, 100000);
	PlayingClip = Playing ? Clip : nullptr;
}

void URPGFlightAnimationComponent::UpdateBodyReaction(USkeletalMeshComponent* Mesh, const FVector& Velocity, float DeltaTime)
{
	if (!Mesh || DeltaTime <= 0.f) return;
	const AActor* Owner = GetOwner();
	if (!bReacting)
	{
		bReacting = true;
		RestLocation = Mesh->GetRelativeLocation();
		RestRotation = Mesh->GetRelativeRotation().Quaternion();
		LastVelocity = Velocity;
		LastHeading = Velocity.Rotation().Yaw;
		Pitch = Roll = 0.f;
		SmoothedYaw = float(Owner->GetActorRotation().Yaw);
	}
	// The body turns after the view: a smoothed yaw that catches up with the actor's.
	const float ActorYaw = float(Owner->GetActorRotation().Yaw);
	const float YawStep = FMath::FindDeltaAngleDegrees(SmoothedYaw, ActorYaw) * (1.f - FMath::Exp(-TurnFollowSpeed * DeltaTime));
	SmoothedYaw = FRotator::NormalizeAxis(SmoothedYaw + YawStep);
	TurnRateRad = FMath::DegreesToRadians(YawStep / DeltaTime);
	const float YawLag = FMath::FindDeltaAngleDegrees(ActorYaw, SmoothedYaw);
	const float Horizontal = float(Velocity.Size2D());
	const float React = FMath::Lerp(HoverReaction, 1.f, FMath::Clamp(Horizontal / FMath::Max(FastSpeed, 1.f), 0.f, 1.f));
	const FVector Accel = (Velocity - LastVelocity) / DeltaTime;
	const float AccelForward = float(FVector::DotProduct(Accel, Owner->GetActorForwardVector()));
	const float Side = float(FVector::DotProduct(Accel, Owner->GetActorRightVector()));
	// Climb / dive along the flight path, plus leaning into speeding up (nose down) and back when slowing.
	float WantPitch = FMath::RadiansToDegrees(FMath::Atan2(float(Velocity.Z), FMath::Max(Horizontal, 300.f))) * ClimbPitch;
	WantPitch -= FMath::Clamp(AccelForward / 1000.f * AccelLean, -15.f, 15.f);
	WantPitch = FMath::Clamp(WantPitch, -MaxPitch, MaxPitch);
	// Bank into turns (how fast the travel direction swings, more at speed) and lean into sideways pushes.
	const float Heading = Horizontal > 50.f ? float(Velocity.Rotation().Yaw) : LastHeading;
	const float TurnRate = FMath::FindDeltaAngleDegrees(LastHeading, Heading) / DeltaTime;
	float WantRoll = TurnRate * BankPerTurn * FMath::Clamp(Horizontal / FMath::Max(FastSpeed, 1.f), 0.f, 1.f) + FMath::Clamp(Side / 1000.f * AccelLean, -15.f, 15.f);
	WantRoll = FMath::Clamp(WantRoll, -MaxBank, MaxBank);
	Pitch = FMath::FInterpTo(Pitch, WantPitch * React, DeltaTime, ReactionSpeed);
	Roll = FMath::FInterpTo(Roll, WantRoll * React, DeltaTime, ReactionSpeed);
	LastVelocity = Velocity;
	LastHeading = Heading;

	// Turn about the hips, in the actor's frame.
	const FQuat Lean = FQuat(FVector::UpVector, FMath::DegreesToRadians(YawLag)) * FRotator(Pitch, 0.f, Roll).Quaternion();
	const FVector Pivot = RestLocation + FVector(0.f, 0.f, PivotHeight);
	Mesh->SetRelativeLocationAndRotation(Pivot + Lean.RotateVector(RestLocation - Pivot), Lean * RestRotation);
}

void URPGFlightAnimationComponent::ResetBodyReaction(USkeletalMeshComponent* Mesh)
{
	if (!bReacting) return;
	bReacting = false;
	TurnRateRad = 0.f;
	if (Mesh) Mesh->SetRelativeLocationAndRotation(RestLocation, RestRotation);
}

namespace
{
	/** Follow-through joints, parents first: bone, the way the limb hangs from it (mesh space), how much momentum moves
	 *  it, how stiff it is (lower = lags more, so the motion rolls down the chain), how much the air drags it, and
	 *  which side it is on (legs: +1 left, -1 right; they swing outward freely but only a little inward, so they never
	 *  cross into each other). Arms are held actively: half the swing, stiffer, no drag. */
	struct FFlowJoint { const TCHAR* Bone; FVector Dir; float Gain; float Stiff; float Drag; float LegSide; };
	const FFlowJoint FlowJoints[] = {
		{ TEXT("spine_01"), FVector(0, 0, -1), 0.12f, 2.f, 1.f, 0.f },
		{ TEXT("spine_02"), FVector(0, 0, -1), 0.08f, 2.f, 1.f, 0.f },
		{ TEXT("thigh_l"), FVector(0, 0, -1), 1.f, 1.f, 1.f, 1.f },
		{ TEXT("calf_l"), FVector(0, 0, -1), 0.8f, 0.6f, 1.f, 1.f },
		{ TEXT("thigh_r"), FVector(0, 0, -1), 1.f, 1.f, 1.f, -1.f },
		{ TEXT("calf_r"), FVector(0, 0, -1), 0.8f, 0.6f, 1.f, -1.f },
		{ TEXT("upperarm_l"), FVector(0, 0, -1), 0.45f, 1.5f, 0.f, 0.f },
		{ TEXT("lowerarm_l"), FVector(0, 0, -1), 0.3f, 1.2f, 0.f, 0.f },
		{ TEXT("upperarm_r"), FVector(0, 0, -1), 0.45f, 1.5f, 0.f, 0.f },
		{ TEXT("lowerarm_r"), FVector(0, 0, -1), 0.3f, 1.2f, 0.f, 0.f },
	};
}

void URPGFlightAnimationComponent::UpdateLimbFlow(USkeletalMeshComponent* Mesh, UAnimInstance* Instance, const FVector& Accel, const FVector& Velocity, bool bFlying, float DeltaTime)
{
	URPGAnimInstance* Anim = Cast<URPGAnimInstance>(Instance);
	if (!Mesh || !Anim) return;
	const int32 N = UE_ARRAY_COUNT(FlowJoints);
	if (FlowAngle.Num() != N) { FlowAngle.Init(FVector::ZeroVector, N); FlowSpeed.Init(FVector::ZeroVector, N); }
	FlowWeight = FMath::FInterpConstantTo(FlowWeight, bFlying && bLimbFlow ? 1.f : 0.f, DeltaTime, 3.f);
	if (FlowWeight <= 0.f)
	{
		for (int32 I = 0; I < N; ++I) FlowAngle[I] = FlowSpeed[I] = FVector::ZeroVector;
		Anim->FlowBones.Reset();
		Anim->FlowRotations.Reset();
		return;
	}
	// Momentum in the mesh's own frame: limbs swing against the acceleration and are swept back by the air.
	const FTransform& ToMesh = Mesh->GetComponentTransform();
	const FVector A = ToMesh.InverseTransformVectorNoScale(Accel.GetClampedToMaxSize(6000.f)) / 1000.f;
	const FVector V = ToMesh.InverseTransformVectorNoScale(Velocity) / 1000.f;
	const float SpeedAlpha = FMath::Clamp(float(Velocity.Size()) / FMath::Max(FastSpeed, 1.f), 0.f, 1.f);
	const float MaxAngle = FMath::DegreesToRadians(FlowMaxAngle);
	// Small steps so the springs stay stable at any frame rate.
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(DeltaTime / (1.f / 120.f)), 1, 8);
	const float Dt = DeltaTime / Steps;
	Anim->FlowBones.SetNum(N);
	Anim->FlowRotations.SetNum(N);
	for (int32 I = 0; I < N; ++I)
	{
		const FFlowJoint& J = FlowJoints[I];
		const float K = FlowStiffness * J.Stiff * FMath::Lerp(1.f, FlowStiffnessAtSpeed, SpeedAlpha);
		const float C = 2.f * FlowDamping * FMath::Sqrt(K);
		const FVector Push = J.Gain * (FlowInertia * FVector::CrossProduct(J.Dir, -A) + FlowDrag * J.Drag * SpeedAlpha * FVector::CrossProduct(J.Dir, -V)
			- FlowTurn * TurnRateRad * FVector::UpVector * 10.f);   // lag behind the body's turn
		for (int32 S = 0; S < Steps; ++S)
		{
			FlowSpeed[I] += (Push - K * FlowAngle[I] - C * FlowSpeed[I]) * Dt;
			FlowAngle[I] += FlowSpeed[I] * Dt;
		}
		if (FlowAngle[I].Size() > MaxAngle) { FlowAngle[I] = FlowAngle[I].GetSafeNormal() * MaxAngle; FlowSpeed[I] *= 0.5f; }
		if (J.LegSide != 0.f)
		{
			// Sideways swing is a turn about the forward axis (+Y, mesh space; +X is the character's left): a positive
			// turn moves a hanging foot toward -X. Inward = toward the other leg: keep that small.
			const float Inward = 0.06f;
			FVector& Ang = FlowAngle[I];
			if (J.LegSide > 0.f && Ang.Y > Inward) { Ang.Y = Inward; FlowSpeed[I].Y = FMath::Min(FlowSpeed[I].Y, 0.f); }
			if (J.LegSide < 0.f && Ang.Y < -Inward) { Ang.Y = -Inward; FlowSpeed[I].Y = FMath::Max(FlowSpeed[I].Y, 0.f); }
		}
		Anim->FlowBones[I] = FName(J.Bone);
		Anim->FlowRotations[I] = FlowAngle[I] * FlowWeight;
	}
}
