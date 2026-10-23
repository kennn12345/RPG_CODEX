#include "RPGFlightAnimationComponent.h"

#include "RPGCombatStateComponent.h"
#include "RPGMovementAbilityComponent.h"
#include "RPGPhysicsReactionComponent.h"
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
	}
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
	const FQuat Lean = FRotator(Pitch, 0.f, Roll).Quaternion();
	const FVector Pivot = RestLocation + FVector(0.f, 0.f, PivotHeight);
	Mesh->SetRelativeLocationAndRotation(Pivot + Lean.RotateVector(RestLocation - Pivot), Lean * RestRotation);
}

void URPGFlightAnimationComponent::ResetBodyReaction(USkeletalMeshComponent* Mesh)
{
	if (!bReacting) return;
	bReacting = false;
	if (Mesh) Mesh->SetRelativeLocationAndRotation(RestLocation, RestRotation);
}
