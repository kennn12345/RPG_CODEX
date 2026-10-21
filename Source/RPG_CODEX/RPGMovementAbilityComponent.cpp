#include "RPGMovementAbilityComponent.h"

#include "RPGCombatStateComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

URPGMovementAbilityComponent::URPGMovementAbilityComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void URPGMovementAbilityComponent::BeginPlay()
{
	Super::BeginPlay();

	if (ACharacter* Character = GetCharacter())
	{
		// Holding jump longer = higher jump. This is built into ACharacter.
		Character->JumpMaxHoldTime = ChargedJumpHoldTime;
	}

	if (UCharacterMovementComponent* Movement = GetMovement())
	{
		// The template character has crouching switched off, so switch it on.
		Movement->GetNavAgentPropertiesRef().bCanCrouch = true;

		// Without braking, a flying character drifts forever after you let go of the stick.
		if (Movement->BrakingDecelerationFlying <= 0.f)
		{
			Movement->BrakingDecelerationFlying = FlyingBrakingDeceleration;
		}
	}
}

void URPGMovementAbilityComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bSprinting)
	{
		return;
	}

	// Sprint ends automatically when the player stops moving, crouches, flies, blocks or aims.
	const ACharacter* Character = GetCharacter();
	const UCharacterMovementComponent* Movement = GetMovement();
	const URPGCombatStateComponent* State = GetCombatState();

	const bool bStoppedOnGround = Movement && Movement->IsMovingOnGround() && Movement->GetCurrentAcceleration().IsNearlyZero();
	const bool bFlying = Movement && Movement->IsFlying();
	const bool bCrouched = Character && Character->bIsCrouched;
	const bool bDefending = State && (State->bIsBlocking || State->bIsAiming);

	if (bStoppedOnGround || bFlying || bCrouched || bDefending)
	{
		StopSprint();
	}
}

bool URPGMovementAbilityComponent::TryStartSprint()
{
	ACharacter* Character = GetCharacter();
	UCharacterMovementComponent* Movement = GetMovement();
	URPGCombatStateComponent* State = GetCombatState();

	if (!Character || !Movement || bSprinting || !Movement->IsMovingOnGround())
	{
		return false;
	}

	if (Movement->GetCurrentAcceleration().IsNearlyZero())
	{
		return false; // Must be pushing the Left Stick to sprint.
	}

	if (State && (State->bIsBlocking || State->bIsAiming))
	{
		return false;
	}

	if (Character->bIsCrouched)
	{
		Character->UnCrouch();
	}

	WalkSpeedBeforeSprint = Movement->MaxWalkSpeed;
	Movement->MaxWalkSpeed = SprintSpeed;
	bSprinting = true;

	if (State)
	{
		State->bIsSprinting = true;
	}
	return true;
}

void URPGMovementAbilityComponent::StopSprint()
{
	if (!bSprinting)
	{
		return;
	}

	if (UCharacterMovementComponent* Movement = GetMovement())
	{
		Movement->MaxWalkSpeed = WalkSpeedBeforeSprint;
	}
	bSprinting = false;

	if (URPGCombatStateComponent* State = GetCombatState())
	{
		State->bIsSprinting = false;
	}
}

bool URPGMovementAbilityComponent::ToggleCrouch()
{
	ACharacter* Character = GetCharacter();
	const UCharacterMovementComponent* Movement = GetMovement();
	if (!Character || !Movement || !Movement->IsMovingOnGround())
	{
		return Character && Character->bIsCrouched;
	}

	if (Character->bIsCrouched)
	{
		Character->UnCrouch();
		return false;
	}

	StopSprint();
	Character->Crouch();
	return true;
}

bool URPGMovementAbilityComponent::StartFlight()
{
	UCharacterMovementComponent* Movement = GetMovement();
	if (!bFlightAvailable || !Movement || Movement->IsFlying())
	{
		return false;
	}

	StopSprint();
	Movement->SetMovementMode(MOVE_Flying);
	Movement->Velocity.Z = 0.f; // Hover in place instead of continuing to rise or fall.
	return true;
}

void URPGMovementAbilityComponent::StopFlight()
{
	UCharacterMovementComponent* Movement = GetMovement();
	if (Movement && Movement->IsFlying())
	{
		Movement->SetMovementMode(MOVE_Falling);
	}
}

bool URPGMovementAbilityComponent::TryDodge()
{
	ACharacter* Character = GetCharacter();
	const UCharacterMovementComponent* Movement = GetMovement();
	if (!Character || !Movement || !Movement->IsMovingOnGround() || IsDodgeOnCooldown())
	{
		return false;
	}

	LastDodgeTime = GetWorld()->GetTimeSeconds();

	// Placeholder: a quick shove. A real dodge animation / root motion replaces this later.
	Character->LaunchCharacter(GetEvadeDirection() * DodgeSpeed, true, false);
	return true;
}

bool URPGMovementAbilityComponent::TryAirEvade()
{
	UCharacterMovementComponent* Movement = GetMovement();
	if (!Movement || !Movement->IsFlying() || IsDodgeOnCooldown())
	{
		return false;
	}

	LastDodgeTime = GetWorld()->GetTimeSeconds();

	// Set velocity directly. LaunchCharacter would switch us to falling and end the flight.
	Movement->Velocity = GetEvadeDirection() * AirEvadeSpeed;
	return true;
}

ACharacter* URPGMovementAbilityComponent::GetCharacter() const
{
	return Cast<ACharacter>(GetOwner());
}

UCharacterMovementComponent* URPGMovementAbilityComponent::GetMovement() const
{
	const ACharacter* Character = GetCharacter();
	return Character ? Character->GetCharacterMovement() : nullptr;
}

URPGCombatStateComponent* URPGMovementAbilityComponent::GetCombatState() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<URPGCombatStateComponent>() : nullptr;
}

FVector URPGMovementAbilityComponent::GetEvadeDirection() const
{
	const ACharacter* Character = GetCharacter();
	if (!Character)
	{
		return FVector::ZeroVector;
	}

	// The template's Move code feeds the Left Stick into AddMovementInput, so this is the stick direction.
	const FVector StickDirection = Character->GetLastMovementInputVector().GetSafeNormal2D();
	return StickDirection.IsNearlyZero() ? -Character->GetActorForwardVector() : StickDirection;
}

bool URPGMovementAbilityComponent::IsDodgeOnCooldown() const
{
	return GetWorld() && (GetWorld()->GetTimeSeconds() - LastDodgeTime) < DodgeCooldown;
}
