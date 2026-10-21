#pragma once

#include "CoreMinimal.h"
#include "RPGPlayerTypes.generated.h"

class AActor;

/** What the player is holding in one hand. Used to decide what LT does and whether we are dual wielding. */
UENUM(BlueprintType)
enum class ERPGHandItemType : uint8
{
	Empty				UMETA(DisplayName = "Empty"),
	OneHandedWeapon		UMETA(DisplayName = "One-Handed Weapon"),
	TwoHandedWeapon		UMETA(DisplayName = "Two-Handed Weapon"),
	Shield				UMETA(DisplayName = "Shield"),
	Bow					UMETA(DisplayName = "Bow")
};

/**
 * The single "headline" state of the player, used later to pick animations.
 * When several are true at once, the most important one wins (see URPGCombatStateComponent::UpdateDerivedStates).
 */
UENUM(BlueprintType)
enum class ERPGCombatMovementState : uint8
{
	Grounded			UMETA(DisplayName = "Grounded"),
	Sprinting			UMETA(DisplayName = "Sprinting"),
	InAir				UMETA(DisplayName = "Jumping / Falling"),
	Flying				UMETA(DisplayName = "Flying"),
	Crouched			UMETA(DisplayName = "Crouched"),
	Blocking			UMETA(DisplayName = "Blocking"),
	Aiming				UMETA(DisplayName = "Aiming"),
	DualWielding		UMETA(DisplayName = "Dual Wielding")
};

UENUM(BlueprintType)
enum class ERPGHand : uint8
{
	None				UMETA(DisplayName = "None"),
	Right				UMETA(DisplayName = "Right"),
	Left				UMETA(DisplayName = "Left"),
	Both				UMETA(DisplayName = "Both")
};

UENUM(BlueprintType)
enum class ERPGKickLeg : uint8
{
	Right				UMETA(DisplayName = "Right Leg"),
	Left				UMETA(DisplayName = "Left Leg")
};

/** What kind of thing an interactable is. Lets A / D-Pad Down react differently (for example, Sleep at a bed). */
UENUM(BlueprintType)
enum class ERPGInteractionType : uint8
{
	Generic				UMETA(DisplayName = "Generic"),
	Conversation		UMETA(DisplayName = "NPC Conversation"),
	Door				UMETA(DisplayName = "Door"),
	Chest				UMETA(DisplayName = "Chest"),
	ItemPickup			UMETA(DisplayName = "Item Pickup"),
	Bed					UMETA(DisplayName = "Bed"),
	WorldObject			UMETA(DisplayName = "World Object")
};

/**
 * Every gameplay action the controller can produce.
 * The input code decides WHICH action happened; animation / combat code listens for it later
 * (URPGCombatStateComponent::OnPlayerAction) and decides HOW it looks.
 */
UENUM(BlueprintType)
enum class ERPGPlayerAction : uint8
{
	None				UMETA(DisplayName = "None"),

	// RT / LT attacks. The hand is stored separately in FRPGPlayerActionInfo::Hand.
	Attack				UMETA(DisplayName = "Attack"),
	HeavyAttack			UMETA(DisplayName = "Heavy Attack"),
	PowerAttack			UMETA(DisplayName = "Power Attack"),
	BlitzAttack			UMETA(DisplayName = "Blitz Attack"),
	AerialAttack		UMETA(DisplayName = "Aerial Attack"),
	FlyingAttack		UMETA(DisplayName = "Flying Attack"),
	ShieldBash			UMETA(DisplayName = "Shield Bash"),

	// LT defense
	Block				UMETA(DisplayName = "Block"),
	ShieldBlock			UMETA(DisplayName = "Shield Block"),
	Aim					UMETA(DisplayName = "Aim"),
	DualGuard			UMETA(DisplayName = "Dual Guard"),
	UnarmedGuard		UMETA(DisplayName = "Unarmed Guard"),
	DefenseReleased		UMETA(DisplayName = "Defense Released"),

	// RB / LB spells
	RightSpell			UMETA(DisplayName = "Right Spell"),
	RightSpellReleased	UMETA(DisplayName = "Right Spell Released"),
	LeftSpell			UMETA(DisplayName = "Left Spell"),
	LeftSpellReleased	UMETA(DisplayName = "Left Spell Released"),

	// Y kicks
	Kick				UMETA(DisplayName = "Kick"),
	HeavyKick			UMETA(DisplayName = "Heavy Kick"),
	PowerKick			UMETA(DisplayName = "Power Kick"),
	BlitzKick			UMETA(DisplayName = "Blitz Kick"),
	AerialKick			UMETA(DisplayName = "Aerial Kick"),
	FlyingKick			UMETA(DisplayName = "Flying Kick"),

	// B
	Dodge				UMETA(DisplayName = "Dodge"),
	AirEvade			UMETA(DisplayName = "Air Evade"),

	// A
	Jump				UMETA(DisplayName = "Jump"),
	ChargedJump			UMETA(DisplayName = "Charged Jump"),
	FlightStarted		UMETA(DisplayName = "Flight Started"),
	FlightCancelled		UMETA(DisplayName = "Flight Cancelled"),
	Interact			UMETA(DisplayName = "Interact"),

	// X
	UseConsumable		UMETA(DisplayName = "Use Consumable"),

	// Sticks
	SprintStarted		UMETA(DisplayName = "Sprint Started"),
	SprintStopped		UMETA(DisplayName = "Sprint Stopped"),
	FlightBoost			UMETA(DisplayName = "Flight Boost (not built yet)"),
	CrouchOn			UMETA(DisplayName = "Crouch / Stealth On"),
	CrouchOff			UMETA(DisplayName = "Crouch / Stealth Off"),

	// D-Pad
	QuickMenu			UMETA(DisplayName = "Quick Menu"),
	DrawSheathe			UMETA(DisplayName = "Draw/Sheathe"),
	CameraPerspective	UMETA(DisplayName = "Camera Perspective"),
	CameraAdjustStarted	UMETA(DisplayName = "Camera Adjust Mode On"),
	CameraAdjustEnded	UMETA(DisplayName = "Camera Adjust Mode Off"),
	Wait				UMETA(DisplayName = "Wait"),
	Sleep				UMETA(DisplayName = "Sleep"),

	// View / Menu
	Journal				UMETA(DisplayName = "Journal"),
	MainMenu			UMETA(DisplayName = "Main Menu"),

	// Charge-hold (kept at the end so existing values don't shift). Pressing RT / LT / Y starts the attack and
	// it loads into its hold pose; releasing still reports Attack / Heavy / Power (or Kick ...) with the charge.
	AttackChargeStarted		UMETA(DisplayName = "Attack Charge Started"),
	KickChargeStarted		UMETA(DisplayName = "Kick Charge Started"),
	/** The charging attack was cancelled before release (the other trigger was pressed for the guard). */
	AttackChargeCancelled	UMETA(DisplayName = "Attack Charge Cancelled")
};

/** Where the current punch / kick is in its animation. Set by the animation code for the Power System to read. */
UENUM(BlueprintType)
enum class ERPGAttackPhase : uint8
{
	None				UMETA(DisplayName = "None"),
	/** Moving into the loaded / chambered pose. */
	Windup				UMETA(DisplayName = "Windup"),
	/** Held at the loaded pose while the button stays down. */
	ChargeHold			UMETA(DisplayName = "Charge Hold"),
	/** Released, travelling to impact. */
	Strike				UMETA(DisplayName = "Strike"),
	/** Past impact: follow-through and return to stance. */
	Recovery			UMETA(DisplayName = "Recovery")
};

/** Everything a listener needs to know about an action, so animation selection can be added later without touching input code. */
USTRUCT(BlueprintType)
struct FRPGPlayerActionInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG")
	ERPGPlayerAction Action = ERPGPlayerAction::None;

	/** Which hand performed it (attacks, spells, blocks). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG")
	ERPGHand Hand = ERPGHand::None;

	/** Which leg performed it (kicks only). Filled in automatically and alternates each kick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG")
	ERPGKickLeg KickLeg = ERPGKickLeg::Right;

	/** 0 to 1. How charged the attack / kick was when released. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG")
	float ChargePercent = 0.f;

	/** Player state at the moment of the action. Filled in automatically. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG")
	ERPGCombatMovementState MovementState = ERPGCombatMovementState::Grounded;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG")
	ERPGHandItemType RightHandType = ERPGHandItemType::Empty;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG")
	ERPGHandItemType LeftHandType = ERPGHandItemType::Empty;

	/** The object involved, if any (interact / sleep). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG")
	TObjectPtr<AActor> Target = nullptr;
};

/** The limb an unarmed strike lands with (which bones the melee hit sweep follows). */
UENUM(BlueprintType)
enum class ERPGStrikeLimb : uint8
{
	None				UMETA(DisplayName = "None"),
	RightHand			UMETA(DisplayName = "Right Fist"),
	LeftHand			UMETA(DisplayName = "Left Fist"),
	RightFoot			UMETA(DisplayName = "Right Foot"),
	LeftFoot			UMETA(DisplayName = "Left Foot")
};

/**
 * One melee contact: who struck what, with which limb, where and how charged. Hit confirmation only; damage, stagger
 * and knockback are for the Power System to decide from this.
 */
USTRUCT(BlueprintType)
struct FRPGMeleeHitInfo
{
	GENERATED_BODY()

	/** "Right Punch", "Charged Left Kick"... */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	FString AttackName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	ERPGStrikeLimb Limb = ERPGStrikeLimb::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	bool bKick = false;

	/** Played the Charged version (the button was held past a tap). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	bool bCharged = false;

	/** 0 to 1, the charge the attack was released with. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	float ChargePercent = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	ERPGAttackPhase AttackPhase = ERPGAttackPhase::None;

	/** Seconds from the animation's authored impact frame (negative = the limb arrived before it). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	float TimeFromImpact = 0.f;

	/** Counts strikes; every hit of one strike shares it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	int32 StrikeId = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	TObjectPtr<AActor> Attacker = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	TObjectPtr<AActor> HitActor = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	TObjectPtr<UPrimitiveComponent> HitComponent = nullptr;

	/** Body (physics asset bone) that was hit, e.g. head, spine_05, thigh_l. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	FName HitBone;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	FVector ImpactLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	FVector ImpactNormal = FVector::ZeroVector;

	/** The attack aim at contact (where the player was looking, within the body's limits), world space, and the
	 *  torso's turn (+ right) / pitch (+ up) from the hips in degrees. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	FVector AimDirection = FVector::ZeroVector;

	/** The world point under the reticle the attack was aimed at. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	FVector AimPoint = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	float AimYaw = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	float AimPitch = 0.f;

	/** Direction the limb was travelling at contact. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	FVector StrikeDirection = FVector::ZeroVector;

	/** Limb speed at contact, cm/s. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	float LimbSpeed = 0.f;

	/** Horizontal distance between the attacker's and the target's origins. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	float Distance = 0.f;
};
