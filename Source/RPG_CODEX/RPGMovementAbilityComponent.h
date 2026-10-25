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
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
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

    /** Flight-only steering authority. The ground acceleration is restored on exit. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Flight", meta = (ClampMin = "1"))
    float FlightAcceleration = 2000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Flight", meta = (ClampMin = "1"))
    float FastFlightAcceleration = 3600.f;

	/** Fly where you look: pushing forward climbs when looking up and dives when looking down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Flight")
	bool bFlightFollowsLookPitch = true;

	/** Looking up/down less than this many degrees keeps level flight, so a slightly tilted camera doesn't drift. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Flight", meta = (ClampMin = "0", ClampMax = "45"))
	float FlightPitchDeadZone = 8.f;

	/** Steepest climb/dive angle when looking straight up/down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Flight", meta = (ClampMin = "10", ClampMax = "85"))
	float FlightMaxPitch = 80.f;

    /** Applied once to the character's normal Max Fly Speed at BeginPlay. Hover keeps its own speed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Flight", meta = (ClampMin = "0.1"))
    float NormalFlightSpeedScale = 1.1f;
	/** Flight speed with L3 fast flight on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Flight")
	float FastFlightSpeed = 2000.f;

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

	/** While true and flying, the character rises straight up (hold A). */
	UFUNCTION(BlueprintCallable, Category = "RPG|Flight")
	void SetFlightRiseHeld(bool bHeld) { bFlightRiseHeld = bHeld; }
    bool IsFlightRiseHeld() const { return bFlightRiseHeld; }

	/** L3 while flying. Returns true if fast flight is now on. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Flight")
	bool ToggleFastFlight();

	UFUNCTION(BlueprintPure, Category = "RPG|Flight")
	bool IsFastFlight() const { return bFastFlight; }

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

	/** Adds the climb/dive and hold-A rise on top of the Left Stick input while flying. */
	void ApplyFlightVerticalInput();
    void UpdateFlightAcceleration(UCharacterMovementComponent* Movement, bool bFlying);
    void ConfigureFlightTickOrder(UCharacterMovementComponent* Movement, bool bFlying);
    void UpdateHoverSpeed(UCharacterMovementComponent* Movement, bool bFlying);
    void RestoreHoverSpeed(UCharacterMovementComponent* Movement);
    bool bHoverSpeedOverride = false;
    float HoverBaseFlySpeed = 0.f, HoverAppliedSpeed = 0.f;
    TWeakObjectPtr<UActorComponent> FlightPreMovementTick;
    bool bAddedFlightPrePrerequisite = false;
    bool bAddedFlightMovementPrerequisite = false;
    bool bFlightAccelerationOverride = false;
    float GroundAcceleration = 0.f;

	bool bSprinting = false;
	float WalkSpeedBeforeSprint = 0.f;

	bool bFlightRiseHeld = false;
	bool bFastFlight = false;
	float FlySpeedBeforeFastFlight = 0.f;
	double LastDodgeTime = -1000.0;
};
