#include "RPGControllerInputComponent.h"

#include "RPGCameraModeComponent.h"
#include "RPGCombatAimComponent.h"
#include "RPGHUD.h"
#include "RPGCombatStateComponent.h"
#include "RPGInteractable.h"
#include "RPGInteractionComponent.h"
#include "RPGStrikeContactComponent.h"
#include "RPGMeleeHitComponent.h"
#include "RPGMovementAbilityComponent.h"
#include "RPGPrototypeAnimationComponent.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

namespace
{
	/** Returns the component of this type on the actor, creating and attaching it if it is missing. */
	template <typename TComponent>
	TComponent* FindOrCreateComponent(AActor* Owner, const TCHAR* Name)
	{
		if (TComponent* Existing = Owner->FindComponentByClass<TComponent>())
		{
			return Existing;
		}

		TComponent* NewComponent = NewObject<TComponent>(Owner, TComponent::StaticClass(), FName(Name));
		Owner->AddInstanceComponent(NewComponent);
		NewComponent->RegisterComponent();
		return NewComponent;
	}
}

URPGControllerInputComponent::URPGControllerInputComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

// =====================================================================================
// Setup
// =====================================================================================

void URPGControllerInputComponent::BeginPlay()
{
	Super::BeginPlay();

	EnsureHelperComponents();
	CreateInputActionsAndContexts();

	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		// The controller may possess us before or after Begin Play, so handle both.
		Pawn->ReceiveControllerChangedDelegate.AddUniqueDynamic(this, &URPGControllerInputComponent::HandleControllerChanged);
		SetupInputForController(Pawn->GetController());
	}
}

void URPGControllerInputComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	TeardownInput();
	Super::EndPlay(EndPlayReason);
}

void URPGControllerInputComponent::EnsureHelperComponents()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	CombatState = FindOrCreateComponent<URPGCombatStateComponent>(Owner, TEXT("RPGCombatState"));
	MovementAbilities = FindOrCreateComponent<URPGMovementAbilityComponent>(Owner, TEXT("RPGMovementAbilities"));
	CameraMode = FindOrCreateComponent<URPGCameraModeComponent>(Owner, TEXT("RPGCameraMode"));
	Interaction = FindOrCreateComponent<URPGInteractionComponent>(Owner, TEXT("RPGInteraction"));
	FindOrCreateComponent<URPGPrototypeAnimationComponent>(Owner, TEXT("RPGPrototypeAnimations"));
	FindOrCreateComponent<URPGMeleeHitComponent>(Owner, TEXT("RPGMeleeHits"));
	FindOrCreateComponent<URPGStrikeContactComponent>(Owner, TEXT("RPGStrikeContact"));
	FindOrCreateComponent<URPGCombatAimComponent>(Owner, TEXT("RPGCombatAim"));
}

UInputAction* URPGControllerInputComponent::CreateInputAction(const TCHAR* Name, EInputActionValueType ValueType)
{
	UInputAction* Action = NewObject<UInputAction>(this, FName(Name));
	Action->ValueType = ValueType;
	return Action;
}

void URPGControllerInputComponent::CreateInputActionsAndContexts()
{
	if (ControllerMappingContext)
	{
		return;
	}

	// Input Actions. These are created in code so nothing has to be set up by hand in the editor.
	// They can be swapped for real IA_ assets later when a key-rebinding menu is added.
	RightAttackAction	= CreateInputAction(TEXT("IA_RightAttack"));
	DefenseAction		= CreateInputAction(TEXT("IA_Defense"));
	RightSpellAction	= CreateInputAction(TEXT("IA_RightSpell"));
	LeftSpellAction		= CreateInputAction(TEXT("IA_LeftSpell"));
	KickAction			= CreateInputAction(TEXT("IA_Kick"));
	DodgeAction			= CreateInputAction(TEXT("IA_Dodge"));
	JumpInteractAction	= CreateInputAction(TEXT("IA_JumpInteractFlight"));
	ConsumableAction	= CreateInputAction(TEXT("IA_QuickConsumable"));
	SprintAction		= CreateInputAction(TEXT("IA_Sprint"));
	CrouchAction		= CreateInputAction(TEXT("IA_CrouchStealth"));
	QuickMenuAction		= CreateInputAction(TEXT("IA_QuickMenu"));
	DrawSheatheAction	= CreateInputAction(TEXT("IA_DrawSheathe"));
	CameraButtonAction	= CreateInputAction(TEXT("IA_CameraButton"));
	WaitAction			= CreateInputAction(TEXT("IA_WaitSleep"));
	JournalAction		= CreateInputAction(TEXT("IA_Journal"));
	MainMenuAction		= CreateInputAction(TEXT("IA_MainMenu"));
	CameraAdjustAction	= CreateInputAction(TEXT("IA_CameraAdjust"), EInputActionValueType::Axis2D);

	// The Xbox layout. Left Stick move and Right Stick look stay in the template's IMC_Default.
	ControllerMappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_RPGController"));
	ControllerMappingContext->MapKey(RightAttackAction,	EKeys::Gamepad_RightTrigger);			// RT
	ControllerMappingContext->MapKey(DefenseAction,		EKeys::Gamepad_LeftTrigger);			// LT
	ControllerMappingContext->MapKey(RightSpellAction,	EKeys::Gamepad_RightShoulder);			// RB
	ControllerMappingContext->MapKey(LeftSpellAction,	EKeys::Gamepad_LeftShoulder);			// LB
	ControllerMappingContext->MapKey(KickAction,		EKeys::Gamepad_FaceButton_Top);			// Y
	ControllerMappingContext->MapKey(DodgeAction,		EKeys::Gamepad_FaceButton_Right);		// B
	ControllerMappingContext->MapKey(JumpInteractAction,EKeys::Gamepad_FaceButton_Bottom);		// A
	ControllerMappingContext->MapKey(ConsumableAction,	EKeys::Gamepad_FaceButton_Left);		// X
	ControllerMappingContext->MapKey(SprintAction,		EKeys::Gamepad_LeftThumbstick);			// Left Stick Click
	ControllerMappingContext->MapKey(CrouchAction,		EKeys::Gamepad_RightThumbstick);		// Right Stick Click
	ControllerMappingContext->MapKey(QuickMenuAction,	EKeys::Gamepad_DPad_Up);				// D-Pad Up
	ControllerMappingContext->MapKey(DrawSheatheAction,	EKeys::Gamepad_DPad_Left);				// D-Pad Left
	ControllerMappingContext->MapKey(CameraButtonAction,EKeys::Gamepad_DPad_Right);				// D-Pad Right
	ControllerMappingContext->MapKey(WaitAction,		EKeys::Gamepad_DPad_Down);				// D-Pad Down
	ControllerMappingContext->MapKey(JournalAction,		EKeys::Gamepad_Special_Left);			// View
	ControllerMappingContext->MapKey(MainMenuAction,	EKeys::Gamepad_Special_Right);			// Menu

	// Only added while D-Pad Right is held. Takes the Right Stick away from normal camera look.
	CameraAdjustMappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_RPGCameraAdjust"));
	FEnhancedActionKeyMapping& StickMapping = CameraAdjustMappingContext->MapKey(CameraAdjustAction, EKeys::Gamepad_Right2D);
	StickMapping.Modifiers.Add(NewObject<UInputModifierDeadZone>(CameraAdjustMappingContext));
}

void URPGControllerInputComponent::BindInputActions()
{
	UEnhancedInputComponent* Input = BindingsComponent;

	// "Started" = button pressed, "Completed" = button released.
	Input->BindAction(RightAttackAction,	ETriggerEvent::Started,		this, &URPGControllerInputComponent::OnRightTriggerPressed);
	Input->BindAction(RightAttackAction,	ETriggerEvent::Completed,	this, &URPGControllerInputComponent::OnRightTriggerReleased);
	Input->BindAction(DefenseAction,		ETriggerEvent::Started,		this, &URPGControllerInputComponent::OnLeftTriggerPressed);
	Input->BindAction(DefenseAction,		ETriggerEvent::Completed,	this, &URPGControllerInputComponent::OnLeftTriggerReleased);
	Input->BindAction(RightSpellAction,		ETriggerEvent::Started,		this, &URPGControllerInputComponent::OnRightSpellPressed);
	Input->BindAction(RightSpellAction,		ETriggerEvent::Completed,	this, &URPGControllerInputComponent::OnRightSpellReleased);
	Input->BindAction(LeftSpellAction,		ETriggerEvent::Started,		this, &URPGControllerInputComponent::OnLeftSpellPressed);
	Input->BindAction(LeftSpellAction,		ETriggerEvent::Completed,	this, &URPGControllerInputComponent::OnLeftSpellReleased);
	Input->BindAction(KickAction,			ETriggerEvent::Started,		this, &URPGControllerInputComponent::OnKickPressed);
	Input->BindAction(KickAction,			ETriggerEvent::Completed,	this, &URPGControllerInputComponent::OnKickReleased);
	Input->BindAction(DodgeAction,			ETriggerEvent::Started,		this, &URPGControllerInputComponent::OnDodgePressed);
	Input->BindAction(JumpInteractAction,	ETriggerEvent::Started,		this, &URPGControllerInputComponent::OnJumpPressed);
	Input->BindAction(JumpInteractAction,	ETriggerEvent::Completed,	this, &URPGControllerInputComponent::OnJumpReleased);
	Input->BindAction(ConsumableAction,		ETriggerEvent::Started,		this, &URPGControllerInputComponent::OnConsumablePressed);
	Input->BindAction(SprintAction,			ETriggerEvent::Started,		this, &URPGControllerInputComponent::OnSprintPressed);
	Input->BindAction(CrouchAction,			ETriggerEvent::Started,		this, &URPGControllerInputComponent::OnCrouchPressed);
	Input->BindAction(CrouchAction,			ETriggerEvent::Completed,	this, &URPGControllerInputComponent::OnCrouchReleased);
	Input->BindAction(QuickMenuAction,		ETriggerEvent::Started,		this, &URPGControllerInputComponent::OnQuickMenuPressed);
	Input->BindAction(DrawSheatheAction,	ETriggerEvent::Started,		this, &URPGControllerInputComponent::OnDrawSheathePressed);
	Input->BindAction(CameraButtonAction,	ETriggerEvent::Started,		this, &URPGControllerInputComponent::OnCameraButtonPressed);
	Input->BindAction(CameraButtonAction,	ETriggerEvent::Completed,	this, &URPGControllerInputComponent::OnCameraButtonReleased);
	Input->BindAction(CameraAdjustAction,	ETriggerEvent::Triggered,	this, &URPGControllerInputComponent::OnCameraAdjust);
	Input->BindAction(WaitAction,			ETriggerEvent::Started,		this, &URPGControllerInputComponent::OnWaitPressed);
	Input->BindAction(JournalAction,		ETriggerEvent::Started,		this, &URPGControllerInputComponent::OnJournalPressed);
	Input->BindAction(MainMenuAction,		ETriggerEvent::Started,		this, &URPGControllerInputComponent::OnMainMenuPressed);
}

void URPGControllerInputComponent::HandleControllerChanged(APawn* Pawn, AController* OldController, AController* NewController)
{
	TeardownInput();
	SetupInputForController(NewController);
}

void URPGControllerInputComponent::SetupInputForController(AController* NewController)
{
	APlayerController* PlayerController = Cast<APlayerController>(NewController);
	if (!PlayerController || !PlayerController->IsLocalController() || BoundController.Get() == PlayerController)
	{
		return;
	}

	if (!BindingsComponent)
	{
		// Our own input component, pushed onto the controller's input stack (same thing "Enable Input" does).
		BindingsComponent = NewObject<UEnhancedInputComponent>(GetOwner(), TEXT("RPGControllerBindings"));
		BindingsComponent->RegisterComponent();
		BindInputActions();
	}

	PlayerController->PushInputComponent(BindingsComponent);
	BoundController = PlayerController;

	// The combat reticle. Only replaces the engine's default HUD, never a custom one a game mode set up.
	if (!PlayerController->GetHUD() || PlayerController->GetHUD()->GetClass() == AHUD::StaticClass())
	{
		PlayerController->ClientSetHUD(ARPGHUD::StaticClass());
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = GetInputSubsystem())
	{
		Subsystem->AddMappingContext(ControllerMappingContext, ControllerMappingPriority);
	}
}

void URPGControllerInputComponent::TeardownInput()
{
	APlayerController* PlayerController = BoundController.Get();
	if (!PlayerController)
	{
		return;
	}

	StopCameraAdjust();

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = GetInputSubsystem())
	{
		Subsystem->RemoveMappingContext(ControllerMappingContext);
		Subsystem->RemoveMappingContext(CameraAdjustMappingContext);
	}

	PlayerController->PopInputComponent(BindingsComponent);
	BoundController = nullptr;
}

UEnhancedInputLocalPlayerSubsystem* URPGControllerInputComponent::GetInputSubsystem() const
{
	const APlayerController* PlayerController = BoundController.Get();
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	return LocalPlayer ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer) : nullptr;
}

// =====================================================================================
// Tick: charge meters and hold timers
// =====================================================================================

void URPGControllerInputComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateChargeStates(DeltaTime);

	// A held after a ground jump: the character keeps rising (see ChargedJumpHoldTime on Movement Abilities).
	if (JumpHold.bHeld)
	{
		JumpHold.HeldTime += DeltaTime;

		if (bJumpStartedFromGround && !JumpHold.bFullChargeAnnounced && JumpHold.HeldTime >= ChargedJumpMessageTime)
		{
			JumpHold.bFullChargeAnnounced = true;
			CombatState->ReportSimpleAction(ERPGPlayerAction::ChargedJump);
		}
	}

	// R3 held while flying: stop flying once the hold is long enough to not be an accidental click.
	if (CrouchHold.bHeld)
	{
		CrouchHold.HeldTime += DeltaTime;

		if (!CrouchHold.bFullChargeAnnounced && CrouchHold.HeldTime >= FlightExitHoldTime)
		{
			CrouchHold.bFullChargeAnnounced = true; // Only once per press.
			CombatState->UpdateDerivedStates();

			if (CombatState->bIsFlying)
			{
				MovementAbilities->StopFlight();
				CombatState->ReportSimpleAction(ERPGPlayerAction::FlightCancelled);
			}
		}
	}

	// D-Pad Right held long enough: switch from "tap = perspective" to camera adjust mode.
	if (CameraButtonHold.bHeld)
	{
		CameraButtonHold.HeldTime += DeltaTime;

		if (!CameraMode->bIsAdjustingCamera && !CameraMode->bIsFirstPerson && CameraButtonHold.HeldTime >= CameraAdjustHoldTime)
		{
			StartCameraAdjust();
		}
	}
}

void URPGControllerInputComponent::UpdateChargeStates(float DeltaTime)
{
	if (!CombatState)
	{
		return;
	}

	FRPGButtonHold* ChargeHolds[] = { &RightTriggerHold, &LeftTriggerHold, &KickHold };
	for (FRPGButtonHold* Hold : ChargeHolds)
	{
		if (Hold->bHeld)
		{
			Hold->HeldTime += DeltaTime;
		}

		if (Hold->bCharging && !Hold->bFullChargeAnnounced && Hold->HeldTime >= FullChargeTime)
		{
			Hold->bFullChargeAnnounced = true;
			CombatState->PrintDebugMessage(Hold == &KickHold ? TEXT("Kick fully charged!") : TEXT("Attack fully charged!"), FColor::Orange);
		}
	}

	CombatState->AttackChargePercent = FMath::Max(GetChargePercent(RightTriggerHold), GetChargePercent(LeftTriggerHold));
	CombatState->bIsChargingAttack = (RightTriggerHold.bCharging && RightTriggerHold.HeldTime >= TapMaxTime)
		|| (LeftTriggerHold.bCharging && LeftTriggerHold.HeldTime >= TapMaxTime);

	CombatState->KickChargePercent = GetChargePercent(KickHold);
	CombatState->bIsChargingKick = KickHold.bCharging && KickHold.HeldTime >= TapMaxTime;
}

// =====================================================================================
// Helpers
// =====================================================================================

ERPGPlayerAction URPGControllerInputComponent::GetContextAction(bool bIsKick) const
{
	if (CombatState->bIsFlying)
	{
		return bIsKick ? ERPGPlayerAction::FlyingKick : ERPGPlayerAction::FlyingAttack;
	}
	if (CombatState->bIsInAir)
	{
		return bIsKick ? ERPGPlayerAction::AerialKick : ERPGPlayerAction::AerialAttack;
	}
	if (CombatState->bIsSprinting)
	{
		return bIsKick ? ERPGPlayerAction::BlitzKick : ERPGPlayerAction::BlitzAttack;
	}
	return ERPGPlayerAction::None;
}

ERPGPlayerAction URPGControllerInputComponent::ResolveChargeTier(float HeldTime, ERPGPlayerAction Tap, ERPGPlayerAction Heavy, ERPGPlayerAction Power) const
{
	if (HeldTime < TapMaxTime)
	{
		return Tap;
	}
	return HeldTime >= FullChargeTime ? Power : Heavy;
}

float URPGControllerInputComponent::GetChargePercent(const FRPGButtonHold& Hold) const
{
	return Hold.bCharging ? FMath::Clamp(Hold.HeldTime / FMath::Max(FullChargeTime, KINDA_SMALL_NUMBER), 0.f, 1.f) : 0.f;
}

void URPGControllerInputComponent::FinishAttackCharge(FRPGButtonHold& Hold, ERPGHand Hand)
{
	const ERPGPlayerAction Action = ResolveChargeTier(Hold.HeldTime,
		ERPGPlayerAction::Attack, ERPGPlayerAction::HeavyAttack, ERPGPlayerAction::PowerAttack);

	const float Charge = (Action == ERPGPlayerAction::Attack) ? 0.f : GetChargePercent(Hold);
	Hold.bCharging = false;
	CombatState->ReportSimpleAction(Action, Hand, Charge);
}

void URPGControllerInputComponent::StartDualGuard()
{
	// Pressing the second trigger cancels whatever attack the first trigger was charging (its punch has
	// already started loading, so tell the animation to drop it).
	if (RightTriggerHold.bCharging) CombatState->ReportSimpleAction(ERPGPlayerAction::AttackChargeCancelled, ERPGHand::Right);
	if (LeftTriggerHold.bCharging) CombatState->ReportSimpleAction(ERPGPlayerAction::AttackChargeCancelled, ERPGHand::Left);
	RightTriggerHold.bCharging = false;
	LeftTriggerHold.bCharging = false;
	bInDualGuard = true;
	CombatState->bIsBlocking = true;
	MovementAbilities->StopSprint();
	CombatState->ReportSimpleAction(CombatState->bIsUnarmed ? ERPGPlayerAction::UnarmedGuard : ERPGPlayerAction::DualGuard, ERPGHand::Both);
}

void URPGControllerInputComponent::StopDualGuard()
{
	// Letting go of either trigger ends the guard. The other trigger does NOT then attack.
	RightTriggerHold.bCharging = false;
	LeftTriggerHold.bCharging = false;
	bInDualGuard = false;
	CombatState->bIsBlocking = false;
	CombatState->ReportSimpleAction(ERPGPlayerAction::DefenseReleased, ERPGHand::Both);
}

void URPGControllerInputComponent::StartCameraAdjust()
{
	CameraMode->SetCameraAdjustMode(true);
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = GetInputSubsystem())
	{
		Subsystem->AddMappingContext(CameraAdjustMappingContext, CameraAdjustMappingPriority);
	}
	CombatState->ReportSimpleAction(ERPGPlayerAction::CameraAdjustStarted);
}

void URPGControllerInputComponent::StopCameraAdjust()
{
	if (!CameraMode || !CameraMode->bIsAdjustingCamera)
	{
		return;
	}

	CameraMode->SetCameraAdjustMode(false);
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = GetInputSubsystem())
	{
		Subsystem->RemoveMappingContext(CameraAdjustMappingContext);
	}
	CombatState->ReportSimpleAction(ERPGPlayerAction::CameraAdjustEnded);
}

// =====================================================================================
// RT: right-hand physical attack
// =====================================================================================

void URPGControllerInputComponent::OnRightTriggerPressed()
{
	CombatState->UpdateDerivedStates();
	RightTriggerHold.Press();

	// Dual wield or unarmed: RT + LT together = guard. Attacks fire on release, so the first
	// trigger's pending attack is simply cancelled here and no punch leaks out when guarding.
	if (CombatState->UsesTwoHandTriggerLayout() && LeftTriggerHold.bHeld)
	{
		StartDualGuard();
		return;
	}

	// Blocking with a shield: RT = shield bash.
	if (bLeftTriggerDefending && CombatState->bIsBlocking && CombatState->HasShieldEquipped())
	{
		CombatState->ReportSimpleAction(ERPGPlayerAction::ShieldBash, ERPGHand::Right);
		return;
	}

	// Sprinting / in the air / flying: a contextual attack fires right away (no charging).
	const ERPGPlayerAction ContextAction = GetContextAction(false);
	if (ContextAction != ERPGPlayerAction::None)
	{
		CombatState->ReportSimpleAction(ContextAction, ERPGHand::Right);
		return;
	}

	// Otherwise start charging: the punch starts now and waits at its loaded pose while RT is held.
	// Releasing decides tap / heavy / power and lets the punch go.
	RightTriggerHold.bCharging = true;
	CombatState->ReportSimpleAction(ERPGPlayerAction::AttackChargeStarted, ERPGHand::Right);
}

void URPGControllerInputComponent::OnRightTriggerReleased()
{
	RightTriggerHold.bHeld = false;

	if (bInDualGuard)
	{
		StopDualGuard();
		return;
	}

	if (RightTriggerHold.bCharging)
	{
		FinishAttackCharge(RightTriggerHold, ERPGHand::Right);
	}
}

// =====================================================================================
// LT: defense (or left-hand attack when dual wielding / unarmed)
// =====================================================================================

void URPGControllerInputComponent::OnLeftTriggerPressed()
{
	CombatState->UpdateDerivedStates();
	LeftTriggerHold.Press();

	if (CombatState->UsesTwoHandTriggerLayout())
	{
		if (RightTriggerHold.bHeld)
		{
			StartDualGuard();
			return;
		}

		const ERPGPlayerAction ContextAction = GetContextAction(false);
		if (ContextAction != ERPGPlayerAction::None)
		{
			CombatState->ReportSimpleAction(ContextAction, ERPGHand::Left);
			return;
		}

		LeftTriggerHold.bCharging = true;
		CombatState->ReportSimpleAction(ERPGPlayerAction::AttackChargeStarted, ERPGHand::Left);
		return;
	}

	bLeftTriggerDefending = true;
	MovementAbilities->StopSprint();

	if (CombatState->HasBowEquipped())
	{
		CombatState->bIsAiming = true;
		CombatState->ReportSimpleAction(ERPGPlayerAction::Aim, ERPGHand::Left);
		return;
	}

	CombatState->bIsBlocking = true;
	CombatState->ReportSimpleAction(CombatState->HasShieldEquipped() ? ERPGPlayerAction::ShieldBlock : ERPGPlayerAction::Block, ERPGHand::Left);
}

void URPGControllerInputComponent::OnLeftTriggerReleased()
{
	LeftTriggerHold.bHeld = false;

	if (bInDualGuard)
	{
		StopDualGuard();
		return;
	}

	if (LeftTriggerHold.bCharging)
	{
		FinishAttackCharge(LeftTriggerHold, ERPGHand::Left);
		return;
	}

	if (bLeftTriggerDefending)
	{
		bLeftTriggerDefending = false;
		CombatState->bIsBlocking = false;
		CombatState->bIsAiming = false;
		CombatState->ReportSimpleAction(ERPGPlayerAction::DefenseReleased, ERPGHand::Left);
	}
}

// =====================================================================================
// RB / LB: spells. No melee charging; each spell can add its own hold behavior later
// by listening for the Pressed and Released actions.
// =====================================================================================

void URPGControllerInputComponent::OnRightSpellPressed()
{
	CombatState->ReportSimpleAction(ERPGPlayerAction::RightSpell, ERPGHand::Right);
}

void URPGControllerInputComponent::OnRightSpellReleased()
{
	CombatState->ReportSimpleAction(ERPGPlayerAction::RightSpellReleased, ERPGHand::Right);
}

void URPGControllerInputComponent::OnLeftSpellPressed()
{
	CombatState->ReportSimpleAction(ERPGPlayerAction::LeftSpell, ERPGHand::Left);
}

void URPGControllerInputComponent::OnLeftSpellReleased()
{
	CombatState->ReportSimpleAction(ERPGPlayerAction::LeftSpellReleased, ERPGHand::Left);
}

// =====================================================================================
// Y: kick (same tap / hold / full charge idea as RT)
// =====================================================================================

void URPGControllerInputComponent::OnKickPressed()
{
	CombatState->UpdateDerivedStates();
	KickHold.Press();

	const ERPGPlayerAction ContextAction = GetContextAction(true);
	if (ContextAction != ERPGPlayerAction::None)
	{
		CombatState->ReportSimpleAction(ContextAction);
		return;
	}

	KickHold.bCharging = true;
	CombatState->ReportSimpleAction(ERPGPlayerAction::KickChargeStarted);
}

void URPGControllerInputComponent::OnKickReleased()
{
	KickHold.bHeld = false;
	if (!KickHold.bCharging)
	{
		return;
	}

	const ERPGPlayerAction Action = ResolveChargeTier(KickHold.HeldTime,
		ERPGPlayerAction::Kick, ERPGPlayerAction::HeavyKick, ERPGPlayerAction::PowerKick);

	const float Charge = (Action == ERPGPlayerAction::Kick) ? 0.f : GetChargePercent(KickHold);
	KickHold.bCharging = false;
	CombatState->ReportSimpleAction(Action, ERPGHand::None, Charge);
}

// =====================================================================================
// B: dodge on the ground, air evade while flying. Direction = Left Stick.
// =====================================================================================

void URPGControllerInputComponent::OnDodgePressed()
{
	CombatState->UpdateDerivedStates();

	if (CombatState->bIsFlying)
	{
		if (MovementAbilities->TryAirEvade())
		{
			CombatState->ReportSimpleAction(ERPGPlayerAction::AirEvade);
		}
		return;
	}

	if (MovementAbilities->TryDodge())
	{
		CombatState->ReportSimpleAction(ERPGPlayerAction::Dodge);
	}
}

// =====================================================================================
// A: start flight in the air (hold to rise while flying), otherwise interact (if something is in range) or jump.
// =====================================================================================

void URPGControllerInputComponent::OnJumpPressed()
{
	CombatState->UpdateDerivedStates();
	JumpHold.Press();
	bJumpStartedFromGround = false;

	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return;
	}

	if (CombatState->bIsFlying)
	{
		// Hold A while flying = rise straight up. Flight is stopped with a held R3 instead.
		MovementAbilities->SetFlightRiseHeld(true);
		return;
	}

	if (CombatState->bIsInAir)
	{
		if (MovementAbilities->StartFlight())
		{
			CombatState->ReportSimpleAction(ERPGPlayerAction::FlightStarted);
		}
		return;
	}

	if (Interaction->bInteractionHasPriorityOverJump)
	{
		if (AActor* Target = Interaction->FindBestInteractable())
		{
			IRPGInteractable::Execute_Interact(Target, Character);
			CombatState->ReportSimpleAction(ERPGPlayerAction::Interact, ERPGHand::None, 0.f, Target);
			return;
		}
	}

	if (Character->bIsCrouched)
	{
		// Pressing A while crouched stands up first.
		MovementAbilities->ToggleCrouch();
		CombatState->ReportSimpleAction(ERPGPlayerAction::CrouchOff);
		return;
	}

	if (Character->CanJump())
	{
		Character->Jump();
		bJumpStartedFromGround = true;
		CombatState->ReportSimpleAction(ERPGPlayerAction::Jump);
	}
}

void URPGControllerInputComponent::OnJumpReleased()
{
	JumpHold.bHeld = false;
	bJumpStartedFromGround = false;
	MovementAbilities->SetFlightRiseHeld(false);

	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->StopJumping();
	}
}

// =====================================================================================
// X, sticks, D-Pad, View, Menu
// =====================================================================================

void URPGControllerInputComponent::OnConsumablePressed()
{
	CombatState->ReportSimpleAction(ERPGPlayerAction::UseConsumable);
}

void URPGControllerInputComponent::OnSprintPressed()
{
	CombatState->UpdateDerivedStates();

	if (CombatState->bIsFlying)
	{
		MovementAbilities->ToggleFastFlight();
		CombatState->ReportSimpleAction(ERPGPlayerAction::FlightBoost);
		return;
	}

	if (MovementAbilities->IsSprinting())
	{
		MovementAbilities->StopSprint();
		CombatState->ReportSimpleAction(ERPGPlayerAction::SprintStopped);
	}
	else if (MovementAbilities->TryStartSprint())
	{
		CombatState->ReportSimpleAction(ERPGPlayerAction::SprintStarted);
	}
}

void URPGControllerInputComponent::OnCrouchPressed()
{
	CombatState->UpdateDerivedStates();
	CrouchHold.Press();

	// Grounded only. No crouch while jumping or flying (holding R3 while flying stops flight, see TickComponent).
	if (CombatState->bIsFlying || CombatState->bIsInAir)
	{
		return;
	}

	const bool bNowCrouched = MovementAbilities->ToggleCrouch();
	CombatState->ReportSimpleAction(bNowCrouched ? ERPGPlayerAction::CrouchOn : ERPGPlayerAction::CrouchOff);
}

void URPGControllerInputComponent::OnCrouchReleased()
{
	CrouchHold.bHeld = false;
}

void URPGControllerInputComponent::OnQuickMenuPressed()
{
	CombatState->ReportSimpleAction(ERPGPlayerAction::QuickMenu);
}

void URPGControllerInputComponent::OnDrawSheathePressed()
{
	CombatState->bWeaponsDrawn = !CombatState->bWeaponsDrawn;
	CombatState->ReportSimpleAction(ERPGPlayerAction::DrawSheathe);
}

void URPGControllerInputComponent::OnCameraButtonPressed()
{
	CameraButtonHold.Press();
}

void URPGControllerInputComponent::OnCameraButtonReleased()
{
	CameraButtonHold.bHeld = false;

	if (CameraMode->bIsAdjustingCamera)
	{
		StopCameraAdjust(); // Was a hold: leave adjust mode, Right Stick goes back to normal look.
		return;
	}

	CameraMode->TogglePerspective(); // Was a tap.
	CombatState->ReportSimpleAction(ERPGPlayerAction::CameraPerspective);
}

void URPGControllerInputComponent::OnCameraAdjust(const FInputActionValue& Value)
{
	CameraMode->ApplyCameraAdjustInput(Value.Get<FVector2D>(), GetWorld()->GetDeltaSeconds());
}

void URPGControllerInputComponent::OnWaitPressed()
{
	// At a bed: Sleep. Anywhere else: Wait.
	AActor* Target = Interaction->FindBestInteractable();
	if (Target && IRPGInteractable::Execute_GetInteractionType(Target) == ERPGInteractionType::Bed)
	{
		CombatState->ReportSimpleAction(ERPGPlayerAction::Sleep, ERPGHand::None, 0.f, Target);
		return;
	}

	CombatState->ReportSimpleAction(ERPGPlayerAction::Wait);
}

void URPGControllerInputComponent::OnJournalPressed()
{
	CombatState->ReportSimpleAction(ERPGPlayerAction::Journal);
}

void URPGControllerInputComponent::OnMainMenuPressed()
{
	CombatState->ReportSimpleAction(ERPGPlayerAction::MainMenu);
}

