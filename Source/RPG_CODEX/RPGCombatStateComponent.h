#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGPlayerTypes.h"
#include "RPGCombatStateComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRPGOnPlayerActionSignature, const FRPGPlayerActionInfo&, ActionInfo);

/**
 * Holds every player state that combat, animation and UI code will want to read
 * (IsBlocking, AttackChargePercent, CurrentCombatMovementState...).
 *
 * It does NOT read buttons. URPGControllerInputComponent reads buttons and writes here.
 * Future animation / weapon code should listen to OnPlayerAction and read these variables.
 */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class RPG_CODEX_API URPGCombatStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGCombatStateComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ---------------- Equipment (change these in the Details panel to test) ----------------

	/** Both Empty = unarmed (fists work like dual wielding). No weapons exist yet, so the player starts unarmed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Equipment")
	ERPGHandItemType CurrentEquippedRightHandType = ERPGHandItemType::Empty;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Equipment")
	ERPGHandItemType CurrentEquippedLeftHandType = ERPGHandItemType::Empty;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "RPG|Equipment")
	bool bWeaponsDrawn = false;

	// ---------------- States ----------------

	/** 0 to 1 while RT (or LT when dual wielding) is held. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	float AttackChargePercent = 0.f;

	/** 0 to 1 while Y is held. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	float KickChargePercent = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	bool bIsBlocking = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	bool bIsSprinting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	bool bIsFlying = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	bool bIsCrouched = false;

	/** True when both hands hold a one-handed weapon. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	bool bIsDualWielding = false;

	/** True when both hands are Empty. Fists then act as two weapons: RT right punch, LT left punch, RT + LT guard. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	bool bIsUnarmed = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	bool bIsAiming = false;

	/** Jumping or falling (not flying). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	bool bIsInAir = false;

	/** True once an attack button has been held longer than a tap. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	bool bIsChargingAttack = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	bool bIsChargingKick = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	ERPGCombatMovementState CurrentCombatMovementState = ERPGCombatMovementState::Grounded;

	/** Kicks alternate legs. This is the leg the next kick will use. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	ERPGKickLeg NextKickLeg = ERPGKickLeg::Right;

	/** Phase of the current punch / kick animation (windup, charge hold, strike, recovery). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	ERPGAttackPhase AttackPhase = ERPGAttackPhase::None;

	/** True while the current attack is a kick (AttackPhase then describes the kick). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	bool bAttackPhaseIsKick = false;

	/** Seconds the current attack has been held at its loaded pose (0 until it reaches it). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	float ChargeHoldSeconds = 0.f;

	/** Charge (0 to 1) of the most recent punch / kick release, and the game time it was released. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	float LastReleaseChargePercent = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	float LastReleaseTime = 0.f;

	/**
	 * Melee hit window: true only around the strike's impact (written by the animation code, read by the melee hit
	 * detection). Idle hands / feet, windup and charge hold never hit.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	bool bStrikeHitWindowOpen = false;

	/** The limb the current strike lands with. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	ERPGStrikeLimb StrikeLimb = ERPGStrikeLimb::None;

	/** The current strike is the Charged version. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	bool bStrikeCharged = false;

	/** Seconds from the current strike's authored impact frame (negative before it). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	float StrikeTimeFromImpact = 0.f;

	/** Goes up by one every time a hit window opens, so each strike can only hit a target once. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	int32 StrikeId = 0;

	/**
	 * Attack aim (URPGCombatAimComponent): the direction the body actually strikes in, from where the player looks,
	 * within the torso's limits. World-space direction, and the torso's turn from the hips (+ right) and pitch (+ up)
	 * in degrees. Forward with no turn outside combat.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	FVector AttackAimDirection = FVector::ForwardVector;

	/** World point under the reticle (the target's surface when one is within reach) that attacks aim at. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	FVector AttackAimPoint = FVector::ZeroVector;

	/** The reticle is on something within melee aim range. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	bool bAttackAimOnTarget = false;

	/** The body can turn the strike all the way to the reticle (false: it goes as far as its limits allow). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	bool bAttackAimInReach = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	float AttackAimYaw = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	float AttackAimPitch = 0.f;

	/** The body's visible turn / tilt toward the aim right now (smoothed, and blended by combat / attack). Presentation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	float AttackAimVisualYaw = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|State")
	float AttackAimVisualPitch = 0.f;

	/** "Right Punch", "Charged Left Kick"... for the current strike. */
	UFUNCTION(BlueprintPure, Category = "RPG|State")
	FString GetStrikeName() const;

	// ---------------- Debug ----------------

	/**
	 * On-screen debug text (this panel, action messages, melee HIT lines, the dummy's hit counter) only shows while the
	 * console variable rpg.Debug is 1 (default 0: hidden, so the animations can be judged). The flags below then pick
	 * which parts show. Log output is unaffected.
	 */
	static bool IsDebugDisplayEnabled();

	/** Print a message on screen every time an action happens ("Right Attack", "Dodge"...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Debug")
	bool bShowDebugMessages = true;

	/** Show a live panel of all the states above in the top-left of the screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Debug")
	bool bShowDebugStatePanel = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Debug")
	float DebugMessageDuration = 2.f;

	// ---------------- Events ----------------

	/** Fires for every action. Bind to this later to play animations, deal damage, open menus, etc. */
	UPROPERTY(BlueprintAssignable, Category = "RPG|Events")
	FRPGOnPlayerActionSignature OnPlayerAction;

	// ---------------- Functions ----------------

	UFUNCTION(BlueprintCallable, Category = "RPG|Equipment")
	void SetEquippedHandTypes(ERPGHandItemType RightHand, ERPGHandItemType LeftHand);

	UFUNCTION(BlueprintPure, Category = "RPG|Equipment")
	bool HasShieldEquipped() const;

	UFUNCTION(BlueprintPure, Category = "RPG|Equipment")
	bool HasBowEquipped() const;

	/** True when RT and LT are two independent hands (dual wielding or unarmed): LT attacks, RT + LT guards. */
	UFUNCTION(BlueprintPure, Category = "RPG|Equipment")
	bool UsesTwoHandTriggerLayout() const { return bIsDualWielding || bIsUnarmed; }

	/** Refreshes the states that come from the character itself (in air, flying, crouched, dual wielding). */
	UFUNCTION(BlueprintCallable, Category = "RPG|State")
	void UpdateDerivedStates();

	/** Fills in the current state, prints the debug message and broadcasts OnPlayerAction. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Events")
	void ReportAction(const FRPGPlayerActionInfo& ActionInfo);

	/** C++ shortcut for ReportAction. */
	void ReportSimpleAction(ERPGPlayerAction Action, ERPGHand Hand = ERPGHand::None, float ChargePercent = 0.f, AActor* Target = nullptr);

	void PrintDebugMessage(const FString& Message, const FColor& Color = FColor::Cyan) const;

	static bool IsAttackAction(ERPGPlayerAction Action);
	static bool IsKickAction(ERPGPlayerAction Action);
	static FString GetActionDisplayName(const FRPGPlayerActionInfo& ActionInfo);

private:
	void DrawDebugStatePanel() const;

	/** A kick charge picked its leg on press; the release that follows uses the same leg instead of alternating again. */
	bool bKickChargePending = false;
	ERPGKickLeg ChargingKickLeg = ERPGKickLeg::Right;
};
