#include "RPGAttributeSet.h"

URPGAttributeSet::URPGAttributeSet()
{
	// Neutral defaults. Real starting values come from URPGCharacterStatsComponent::StartingStats.
	InitHealth(100.f);
	InitMaxHealth(100.f);
	InitStamina(100.f);
	InitMaxStamina(100.f);
	InitStrength(1.f);
	InitAttackSpeed(1.f);
	InitMass(82.f);
	InitDurability(50.f);
	InitStability(50.f);
	InitStaggerResistance(50.f);
	InitRecovery(50.f);
	InitMagicPower(0.f);
	InitMagicControl(0.f);
}

void URPGAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void URPGAttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void URPGAttributeSet::ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const
{
	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());
	}
	else if (Attribute == GetStaminaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxStamina());
	}
	else if (Attribute == GetMassAttribute())
	{
		NewValue = FMath::Max(NewValue, 1.f); // Mass divides in the Power System; never let it reach zero.
	}
	else
	{
		NewValue = FMath::Max(NewValue, 0.f);
	}
}
