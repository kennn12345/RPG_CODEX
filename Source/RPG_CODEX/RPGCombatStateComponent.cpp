#include "RPGCombatStateComponent.h"

#include "RPG_CODEX.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	TAutoConsoleVariable<int32> CVarRPGDebug(TEXT("rpg.Debug"), 0,
		TEXT("On-screen debug text and melee debug drawing: 0 = hidden (default), 1 = shown."));

	// Fixed key so the state panel replaces itself every frame instead of stacking up.
	constexpr int32 DebugStatePanelKey = 927100;

	const TCHAR* YesNo(bool bValue)
	{
		return bValue ? TEXT("YES") : TEXT("no");
	}
}

URPGCombatStateComponent::URPGCombatStateComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void URPGCombatStateComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateDerivedStates();

	if (bShowDebugStatePanel && IsDebugDisplayEnabled())
	{
		DrawDebugStatePanel();
	}
}

void URPGCombatStateComponent::SetEquippedHandTypes(ERPGHandItemType RightHand, ERPGHandItemType LeftHand)
{
	CurrentEquippedRightHandType = RightHand;
	CurrentEquippedLeftHandType = LeftHand;
	UpdateDerivedStates();
}

bool URPGCombatStateComponent::HasShieldEquipped() const
{
	return CurrentEquippedLeftHandType == ERPGHandItemType::Shield || CurrentEquippedRightHandType == ERPGHandItemType::Shield;
}

bool URPGCombatStateComponent::HasBowEquipped() const
{
	return CurrentEquippedLeftHandType == ERPGHandItemType::Bow || CurrentEquippedRightHandType == ERPGHandItemType::Bow;
}

void URPGCombatStateComponent::UpdateDerivedStates()
{
	bIsDualWielding = CurrentEquippedRightHandType == ERPGHandItemType::OneHandedWeapon
		&& CurrentEquippedLeftHandType == ERPGHandItemType::OneHandedWeapon;
	bIsUnarmed = CurrentEquippedRightHandType == ERPGHandItemType::Empty
		&& CurrentEquippedLeftHandType == ERPGHandItemType::Empty;

	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		bIsCrouched = Character->bIsCrouched;

		if (const UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			bIsFlying = Movement->IsFlying();
			bIsInAir = Movement->IsFalling();
		}
	}

	// When several states are true, the most important one becomes the headline state.
	if (bIsFlying)				CurrentCombatMovementState = ERPGCombatMovementState::Flying;
	else if (bIsInAir)			CurrentCombatMovementState = ERPGCombatMovementState::InAir;
	else if (bIsBlocking)		CurrentCombatMovementState = ERPGCombatMovementState::Blocking;
	else if (bIsAiming)			CurrentCombatMovementState = ERPGCombatMovementState::Aiming;
	else if (bIsSprinting)		CurrentCombatMovementState = ERPGCombatMovementState::Sprinting;
	else if (bIsCrouched)		CurrentCombatMovementState = ERPGCombatMovementState::Crouched;
	else if (bIsDualWielding)	CurrentCombatMovementState = ERPGCombatMovementState::DualWielding;
	else						CurrentCombatMovementState = ERPGCombatMovementState::Grounded;
}

void URPGCombatStateComponent::ReportAction(const FRPGPlayerActionInfo& ActionInfo)
{
	UpdateDerivedStates();

	FRPGPlayerActionInfo Info = ActionInfo;
	Info.MovementState = CurrentCombatMovementState;
	Info.RightHandType = CurrentEquippedRightHandType;
	Info.LeftHandType = CurrentEquippedLeftHandType;

	const bool bChargedKickRelease = bKickChargePending &&
		(Info.Action == ERPGPlayerAction::Kick || Info.Action == ERPGPlayerAction::HeavyKick || Info.Action == ERPGPlayerAction::PowerKick);
	if (bChargedKickRelease)
	{
		// The release of a kick that started charging on press: same leg as its start.
		Info.KickLeg = ChargingKickLeg;
		bKickChargePending = false;
	}
	else if (IsKickAction(Info.Action))
	{
		Info.KickLeg = NextKickLeg;
		NextKickLeg = (NextKickLeg == ERPGKickLeg::Right) ? ERPGKickLeg::Left : ERPGKickLeg::Right;
		if (Info.Action == ERPGPlayerAction::KickChargeStarted)
		{
			ChargingKickLeg = Info.KickLeg;
			bKickChargePending = true;
		}
	}

	if (bShowDebugMessages)
	{
		PrintDebugMessage(GetActionDisplayName(Info), FColor::Yellow);
	}

	OnPlayerAction.Broadcast(Info);
}

void URPGCombatStateComponent::ReportSimpleAction(ERPGPlayerAction Action, ERPGHand Hand, float ChargePercent, AActor* Target)
{
	FRPGPlayerActionInfo Info;
	Info.Action = Action;
	Info.Hand = Hand;
	Info.ChargePercent = ChargePercent;
	Info.Target = Target;
	ReportAction(Info);
}

FString URPGCombatStateComponent::GetStrikeName() const
{
	FString Name;
	switch (StrikeLimb)
	{
	case ERPGStrikeLimb::RightHand:	Name = TEXT("Right Punch"); break;
	case ERPGStrikeLimb::LeftHand:	Name = TEXT("Left Punch"); break;
	case ERPGStrikeLimb::RightFoot:	Name = TEXT("Right Kick"); break;
	case ERPGStrikeLimb::LeftFoot:	Name = TEXT("Left Kick"); break;
	default:						return TEXT("None");
	}
	return bStrikeCharged ? TEXT("Charged ") + Name : Name;
}

bool URPGCombatStateComponent::IsDebugDisplayEnabled()
{
	return CVarRPGDebug.GetValueOnGameThread() > 0;
}

void URPGCombatStateComponent::PrintDebugMessage(const FString& Message, const FColor& Color) const
{
	UE_LOG(LogRPGInput, Log, TEXT("%s"), *Message);

	if (GEngine && IsDebugDisplayEnabled())
	{
		GEngine->AddOnScreenDebugMessage(-1, DebugMessageDuration, Color, Message);
	}
}

bool URPGCombatStateComponent::IsAttackAction(ERPGPlayerAction Action)
{
	switch (Action)
	{
	case ERPGPlayerAction::Attack:
	case ERPGPlayerAction::HeavyAttack:
	case ERPGPlayerAction::PowerAttack:
	case ERPGPlayerAction::BlitzAttack:
	case ERPGPlayerAction::AerialAttack:
	case ERPGPlayerAction::FlyingAttack:
	case ERPGPlayerAction::ShieldBash:
		return true;
	default:
		return false;
	}
}

bool URPGCombatStateComponent::IsKickAction(ERPGPlayerAction Action)
{
	switch (Action)
	{
	case ERPGPlayerAction::Kick:
	case ERPGPlayerAction::HeavyKick:
	case ERPGPlayerAction::PowerKick:
	case ERPGPlayerAction::BlitzKick:
	case ERPGPlayerAction::AerialKick:
	case ERPGPlayerAction::FlyingKick:
	case ERPGPlayerAction::KickChargeStarted:
		return true;
	default:
		return false;
	}
}

FString URPGCombatStateComponent::GetActionDisplayName(const FRPGPlayerActionInfo& ActionInfo)
{
	FString Name = UEnum::GetDisplayValueAsText(ActionInfo.Action).ToString();

	if (IsAttackAction(ActionInfo.Action))
	{
		// "Attack" + right hand = "Right Attack", anything + left hand = "Left ...".
		if (ActionInfo.Hand == ERPGHand::Left)
		{
			Name = TEXT("Left ") + Name;
		}
		else if (ActionInfo.Action == ERPGPlayerAction::Attack)
		{
			Name = TEXT("Right Attack");
		}
	}

	if (ActionInfo.Action == ERPGPlayerAction::AttackChargeStarted || ActionInfo.Action == ERPGPlayerAction::AttackChargeCancelled)
	{
		Name = (ActionInfo.Hand == ERPGHand::Left ? TEXT("Left ") : TEXT("Right ")) + Name;
	}

	if (IsKickAction(ActionInfo.Action))
	{
		Name += FString::Printf(TEXT(" (%s)"), *UEnum::GetDisplayValueAsText(ActionInfo.KickLeg).ToString());
	}

	if (ActionInfo.ChargePercent > 0.f)
	{
		Name += FString::Printf(TEXT(" [%.0f%% charge]"), ActionInfo.ChargePercent * 100.f);
	}

	if (ActionInfo.Target)
	{
		Name += FString::Printf(TEXT(": %s"), *ActionInfo.Target->GetActorNameOrLabel());
	}

	return Name;
}

void URPGCombatStateComponent::DrawDebugStatePanel() const
{
	if (!GEngine)
	{
		return;
	}

	const FString Panel = FString::Printf(
		TEXT("--- RPG PLAYER STATE ---\n")
		TEXT("Movement State: %s\n")
		TEXT("Right Hand: %s   Left Hand: %s   Weapons Drawn: %s\n")
		TEXT("Sprinting: %s   Crouched: %s   In Air: %s   Flying: %s\n")
		TEXT("Blocking: %s   Aiming: %s   Dual Wielding: %s   Unarmed: %s\n")
		TEXT("Attack Charge: %.0f%% (charging: %s)   Kick Charge: %.0f%% (charging: %s)   Next Kick: %s\n")
		TEXT("Attack Phase: %s%s   Held at load: %.1fs   Last Release: %.0f%%"),
		*UEnum::GetDisplayValueAsText(CurrentCombatMovementState).ToString(),
		*UEnum::GetDisplayValueAsText(CurrentEquippedRightHandType).ToString(),
		*UEnum::GetDisplayValueAsText(CurrentEquippedLeftHandType).ToString(),
		YesNo(bWeaponsDrawn),
		YesNo(bIsSprinting), YesNo(bIsCrouched), YesNo(bIsInAir), YesNo(bIsFlying),
		YesNo(bIsBlocking), YesNo(bIsAiming), YesNo(bIsDualWielding), YesNo(bIsUnarmed),
		AttackChargePercent * 100.f, YesNo(bIsChargingAttack),
		KickChargePercent * 100.f, YesNo(bIsChargingKick),
		*UEnum::GetDisplayValueAsText(NextKickLeg).ToString(),
		*UEnum::GetDisplayValueAsText(AttackPhase).ToString(), bAttackPhaseIsKick ? TEXT(" (kick)") : TEXT(""),
		ChargeHoldSeconds, LastReleaseChargePercent * 100.f);

	GEngine->AddOnScreenDebugMessage(DebugStatePanelKey, 0.1f, FColor::Green, Panel);
}
