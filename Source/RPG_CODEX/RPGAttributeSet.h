#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "RPGAttributeSet.generated.h"

/** Getter / setter / initter helpers for each attribute (the standard GAS pattern). */
#define RPG_ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

/**
 * Every character's core stats (Gameplay Ability System attributes). Used by the player and NPCs alike.
 *
 * Scales:
 *  - Strength: the 1-20 Strength ladder. 5 = a capable ordinary fighter, 20 = legendary.
 *  - Mass: kilograms (the standard bandit is 82 kg / 180 lb). Gameplay mass, separate from the physics bodies' mass.
 *  - Durability, Stability, Stagger Resistance, Recovery: 0-100, 50 = an average human.
 *  - Attack Speed: multiplier, 1 = normal. Never derived from Strength.
 *  - Magic Power, Magic Control: 0-100.
 *
 * Health and Stamina are clamped between 0 and their Max. Temporary buffs/debuffs go through Gameplay Effects,
 * which keeps them separate from the permanent base values.
 */
UCLASS()
class RPG_CODEX_API URPGAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	URPGAttributeSet();

	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;

	// ---- Survivability ----

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Stats")
	FGameplayAttributeData Health;
	RPG_ATTRIBUTE_ACCESSORS(URPGAttributeSet, Health)

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Stats")
	FGameplayAttributeData MaxHealth;
	RPG_ATTRIBUTE_ACCESSORS(URPGAttributeSet, MaxHealth)

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Stats")
	FGameplayAttributeData Stamina;
	RPG_ATTRIBUTE_ACCESSORS(URPGAttributeSet, Stamina)

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Stats")
	FGameplayAttributeData MaxStamina;
	RPG_ATTRIBUTE_ACCESSORS(URPGAttributeSet, MaxStamina)

	// ---- Physical ----

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Stats")
	FGameplayAttributeData Strength;
	RPG_ATTRIBUTE_ACCESSORS(URPGAttributeSet, Strength)

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Stats")
	FGameplayAttributeData AttackSpeed;
	RPG_ATTRIBUTE_ACCESSORS(URPGAttributeSet, AttackSpeed)

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Stats")
	FGameplayAttributeData Mass;
	RPG_ATTRIBUTE_ACCESSORS(URPGAttributeSet, Mass)

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Stats")
	FGameplayAttributeData Durability;
	RPG_ATTRIBUTE_ACCESSORS(URPGAttributeSet, Durability)

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Stats")
	FGameplayAttributeData Stability;
	RPG_ATTRIBUTE_ACCESSORS(URPGAttributeSet, Stability)

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Stats")
	FGameplayAttributeData StaggerResistance;
	RPG_ATTRIBUTE_ACCESSORS(URPGAttributeSet, StaggerResistance)

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Stats")
	FGameplayAttributeData Recovery;
	RPG_ATTRIBUTE_ACCESSORS(URPGAttributeSet, Recovery)

	// ---- Magic ----

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Stats")
	FGameplayAttributeData MagicPower;
	RPG_ATTRIBUTE_ACCESSORS(URPGAttributeSet, MagicPower)

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Stats")
	FGameplayAttributeData MagicControl;
	RPG_ATTRIBUTE_ACCESSORS(URPGAttributeSet, MagicControl)

private:
	/** Keeps Health / Stamina inside 0..Max and every other stat at 0 or above. */
	void ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const;
};
