#include "RPGFlightAnimationComponent.h"

#include "RPGCombatStateComponent.h"
#include "RPGMovementAbilityComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
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
		if (Playing && Instance->Montage_IsPlaying(Playing)) Instance->Montage_Stop(0.25f, Playing);
		Playing = nullptr;
		PlayingClip = nullptr;
		return;
	}

	// Which pose: fast flight (L3) or high speed -> fast; moving -> forward; otherwise hover.
	const URPGMovementAbilityComponent* Abilities = GetOwner()->FindComponentByClass<URPGMovementAbilityComponent>();
	const float Speed = float(Movement->Velocity.Size2D());
	TSoftObjectPtr<UAnimSequence> Want = Hover;
	if ((Abilities && Abilities->IsFastFlight() && Speed > ForwardSpeed) || Speed >= FastSpeed) Want = Fast;
	else if (Speed >= ForwardSpeed) Want = Forward;
	UAnimSequence* Clip = Want.LoadSynchronous();
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
