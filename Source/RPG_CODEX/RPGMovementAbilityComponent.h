#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGMovementAbilityComponent.generated.h"

class ACharacter;
class UCharacterMovementComponent;
class URPGCombatStateComponent;

/**
 * Placeholder movement abilities: sprint, crouch, flight, dodge and the charged (held) jump.
 * Kept separate from input so the button code stays small and these can be replaced later
 * (for example by the Power System) without touching the controller layout.
 */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class RPG_CODEX_API URPGMovementAbilityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGMovementAbilityComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ---------------- Tuning ----------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Sprint")
	float SprintSpeed = 850.f;

	/** Holding A keeps pushing the character up for this long, so holding = higher jump. 0 = no charged jump. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Jump")
	float ChargedJumpHoldTime = 0.3f;

	/** Turn off to disable "A while airborne = start flying". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Flight")
	bool bFlightAvailable = true;

	/** How quickly the character stops drifting while flying with no stick input. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Flight")
	float FlyingBrakingDeceleration = 1500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dodge")
	float DodgeSpeed = 1400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dodge")
	float AirEvadeSpeed = 1400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dodge")
	float DodgeCooldown = 0.6f;

	// ---------------- Abilities ----------------

	/** Starts sprinting. Only works on the ground while the Left Stick is pushed. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Sprint")
	bool TryStartSprint();

	UFUNCTION(BlueprintCallable, Category = "RPG|Sprint")
	void StopSprint();

	UFUNCTION(BlueprintPure, Category = "RPG|Sprint")
	bool IsSprinting() const { return bSprinting; }

	/** Grounded only. Returns true if the character is now crouching. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Crouch")
	bool ToggleCrouch();

	UFUNCTION(BlueprintCallable, Category = "RPG|Flight")
	bool StartFlight();

	UFUNCTION(BlueprintCallable, Category = "RPG|Flight")
	void StopFlight();

	/** Ground dodge in the Left Stick direction (backstep if the stick is centered). */
	UFUNCTION(BlueprintCallable, Category = "RPG|Dodge")
	bool TryDodge();

	/** Flight-only evade in the Left Stick direction (back if the stick is centered). */
	UFUNCTION(BlueprintCallable, Category = "RPG|Dodge")
	bool TryAirEvade();

private:
	ACharacter* GetCharacter() const;
	UCharacterMovementComponent* GetMovement() const;
	URPGCombatStateComponent* GetCombatState() const;

	/** Left Stick direction in world space, flattened. Falls back to "backwards" when the stick is centered. */
	FVector GetEvadeDirection() const;
	bool IsDodgeOnCooldown() const;

	bool bSprinting = false;
	float WalkSpeedBeforeSprint = 0.f;
	double LastDodgeTime = -1000.0;
};
