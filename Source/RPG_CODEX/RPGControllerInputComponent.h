#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputActionValue.h"
#include "RPGPlayerTypes.h"
#include "RPGControllerInputComponent.generated.h"

class AController;
class APawn;
class APlayerController;
class UEnhancedInputComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;
class URPGCameraModeComponent;
class URPGCombatStateComponent;
class URPGInteractionComponent;
class URPGMovementAbilityComponent;

/** Remembers how long a button has been held, so we can tell tap / hold / full charge apart. */
struct FRPGButtonHold
{
	bool bHeld = false;
	/** True only while this press is building up a tap / heavy / power result. */
	bool bCharging = false;
	bool bFullChargeAnnounced = false;
	float HeldTime = 0.f;

	void Press()
	{
		bHeld = true;
		bCharging = false;
		bFullChargeAnnounced = false;
		HeldTime = 0.f;
	}
};

/**
 * THE XBOX CONTROLLER LAYOUT.
 *
 * Add this ONE component to your character Blueprint. On Begin Play it:
 *  1. Adds the helper components if they are missing (Combat State, Movement Ability, Camera Mode, Interaction).
 *  2. Creates the Input Actions and the "IMC_RPGController" Mapping Context in code.
 *  3. Binds every button to the functions below.
 *
 * This component only decides WHICH action happened ("Heavy Attack", "Blitz Kick"...).
 * It reports it to URPGCombatStateComponent, which prints it and fires OnPlayerAction.
 * Animations, damage and Power System physics are NOT done here.
 *
 * Triggers: RT attacks, LT defends. When dual wielding OR unarmed (both hands Empty), RT and LT are two
 * independent hands (LT = left attack) and RT + LT together = guard.
 *
 * The template's existing IA_Move / IA_Look / IA_Jump keep working. Our mapping context has a
 * higher priority, so the controller A button goes to our Jump/Interact/Flight logic instead of
 * the template jump (keyboard Space still uses the template jump).
 */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class RPG_CODEX_API URPGControllerInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGControllerInputComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ---------------- Timing (tune these) ----------------

	/** Released before this many seconds = tap (regular attack / quick kick). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Timing")
	float TapMaxTime = 0.25f;

	/**
	 * Held at least this long = full charge (Power Attack / Power Kick). Anything between tap and this = heavy.
	 * Charge counts from the press; the attack then waits at its loaded pose until release, however long that is.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Timing")
	float FullChargeTime = 1.0f;

	/** Hold D-Pad Right this long to enter camera adjust mode instead of toggling perspective. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Timing")
	float CameraAdjustHoldTime = 0.35f;

	/** Holding A this long after a jump prints "Charged Jump". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Timing")
	float ChargedJumpMessageTime = 0.2f;

	// ---------------- Mapping context priorities ----------------

	/** Higher than the template IMC_Default (0) so our A button wins over the template jump. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Input")
	int32 ControllerMappingPriority = 1;

	/** Only active while holding D-Pad Right. Higher still, so the Right Stick stops turning the camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Input")
	int32 CameraAdjustMappingPriority = 10;

private:
	// ---------------- Setup ----------------

	void EnsureHelperComponents();
	void CreateInputActionsAndContexts();
	UInputAction* CreateInputAction(const TCHAR* Name, EInputActionValueType ValueType = EInputActionValueType::Boolean);
	void BindInputActions();

	UFUNCTION()
	void HandleControllerChanged(APawn* Pawn, AController* OldController, AController* NewController);

	void SetupInputForController(AController* NewController);
	void TeardownInput();
	UEnhancedInputLocalPlayerSubsystem* GetInputSubsystem() const;

	// ---------------- Button handlers ----------------

	void OnRightTriggerPressed();		// RT
	void OnRightTriggerReleased();
	void OnLeftTriggerPressed();		// LT
	void OnLeftTriggerReleased();
	void OnRightSpellPressed();			// RB
	void OnRightSpellReleased();
	void OnLeftSpellPressed();			// LB
	void OnLeftSpellReleased();
	void OnKickPressed();				// Y
	void OnKickReleased();
	void OnDodgePressed();				// B
	void OnJumpPressed();				// A
	void OnJumpReleased();
	void OnConsumablePressed();			// X
	void OnSprintPressed();				// Left Stick Click
	void OnCrouchPressed();				// Right Stick Click
	void OnQuickMenuPressed();			// D-Pad Up
	void OnDrawSheathePressed();		// D-Pad Left
	void OnCameraButtonPressed();		// D-Pad Right
	void OnCameraButtonReleased();
	void OnCameraAdjust(const FInputActionValue& Value); // Right Stick while D-Pad Right is held
	void OnWaitPressed();				// D-Pad Down
	void OnJournalPressed();			// View
	void OnMainMenuPressed();			// Menu

	// ---------------- Helpers ----------------

	/** Sprint / air / flying versions of an attack or kick. Returns None when no context applies. */
	ERPGPlayerAction GetContextAction(bool bIsKick) const;

	/** Turns how long the button was held into tap / heavy / power. */
	ERPGPlayerAction ResolveChargeTier(float HeldTime, ERPGPlayerAction Tap, ERPGPlayerAction Heavy, ERPGPlayerAction Power) const;
	float GetChargePercent(const FRPGButtonHold& Hold) const;
	void FinishAttackCharge(FRPGButtonHold& Hold, ERPGHand Hand);

	void StartDualGuard();
	void StopDualGuard();
	void StartCameraAdjust();
	void StopCameraAdjust();

	void UpdateChargeStates(float DeltaTime);

	// ---------------- Helper components (created automatically) ----------------

	UPROPERTY(Transient)
	TObjectPtr<URPGCombatStateComponent> CombatState;

	UPROPERTY(Transient)
	TObjectPtr<URPGMovementAbilityComponent> MovementAbilities;

	UPROPERTY(Transient)
	TObjectPtr<URPGCameraModeComponent> CameraMode;

	UPROPERTY(Transient)
	TObjectPtr<URPGInteractionComponent> Interaction;

	// ---------------- Input objects (created in code) ----------------

	UPROPERTY(Transient)
	TObjectPtr<UEnhancedInputComponent> BindingsComponent;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> ControllerMappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> CameraAdjustMappingContext;

	UPROPERTY(Transient) TObjectPtr<UInputAction> RightAttackAction;		// RT
	UPROPERTY(Transient) TObjectPtr<UInputAction> DefenseAction;			// LT
	UPROPERTY(Transient) TObjectPtr<UInputAction> RightSpellAction;			// RB
	UPROPERTY(Transient) TObjectPtr<UInputAction> LeftSpellAction;			// LB
	UPROPERTY(Transient) TObjectPtr<UInputAction> KickAction;				// Y
	UPROPERTY(Transient) TObjectPtr<UInputAction> DodgeAction;				// B
	UPROPERTY(Transient) TObjectPtr<UInputAction> JumpInteractAction;		// A
	UPROPERTY(Transient) TObjectPtr<UInputAction> ConsumableAction;			// X
	UPROPERTY(Transient) TObjectPtr<UInputAction> SprintAction;				// Left Stick Click
	UPROPERTY(Transient) TObjectPtr<UInputAction> CrouchAction;				// Right Stick Click
	UPROPERTY(Transient) TObjectPtr<UInputAction> QuickMenuAction;			// D-Pad Up
	UPROPERTY(Transient) TObjectPtr<UInputAction> DrawSheatheAction;		// D-Pad Left
	UPROPERTY(Transient) TObjectPtr<UInputAction> CameraButtonAction;		// D-Pad Right
	UPROPERTY(Transient) TObjectPtr<UInputAction> WaitAction;				// D-Pad Down
	UPROPERTY(Transient) TObjectPtr<UInputAction> JournalAction;			// View
	UPROPERTY(Transient) TObjectPtr<UInputAction> MainMenuAction;			// Menu
	UPROPERTY(Transient) TObjectPtr<UInputAction> CameraAdjustAction;		// Right Stick (adjust mode only)

	TWeakObjectPtr<APlayerController> BoundController;

	// ---------------- Button state ----------------

	FRPGButtonHold RightTriggerHold;
	FRPGButtonHold LeftTriggerHold;
	FRPGButtonHold KickHold;
	FRPGButtonHold JumpHold;
	FRPGButtonHold CameraButtonHold;

	bool bInDualGuard = false;
	/** LT is currently holding a normal block or bow aim (not dual-wield). */
	bool bLeftTriggerDefending = false;
	bool bJumpStartedFromGround = false;
};
