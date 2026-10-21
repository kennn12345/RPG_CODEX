#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGCharacterStatsComponent.generated.h"

class UAbilitySystemComponent;
class URPGAttributeSet;

/** Starting values for a character's stats. Scales are explained on URPGAttributeSet. */
USTRUCT(BlueprintType)
struct FRPGBaseStats
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Stats", meta = (ClampMin = "1"))
	float MaxHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Stats", meta = (ClampMin = "0"))
	float MaxStamina = 100.f;

	/** 1-20 Strength ladder. 5 = capable ordinary fighter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Stats", meta = (ClampMin = "0"))
	float Strength = 5.f;

	/** Multiplier, 1 = normal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Stats", meta = (ClampMin = "0.1"))
	float AttackSpeed = 1.f;

	/** Kilograms. 82 kg = 180 lb. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Stats", meta = (ClampMin = "1"))
	float Mass = 82.f;

	/** 0-100, 50 = average human. Resistance to injury; separate from Health. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Stats", meta = (ClampMin = "0"))
	float Durability = 50.f;

	/** 0-100, 50 = average human. Resistance to losing balance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Stats", meta = (ClampMin = "0"))
	float Stability = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Stats", meta = (ClampMin = "0"))
	float StaggerResistance = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Stats", meta = (ClampMin = "0"))
	float Recovery = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Stats", meta = (ClampMin = "0"))
	float MagicPower = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Stats", meta = (ClampMin = "0"))
	float MagicControl = 10.f;
};

/**
 * Gives a character its stats. On BeginPlay it finds the owner's Ability System Component (or adds one), adds a
 * URPGAttributeSet and fills it from StartingStats. Works on any actor (the player Blueprint gets one automatically from
 * URPGControllerInputComponent; the combat test dummy has one built in).
 *
 * Console commands (while playing, press ` to open the console):
 *   rpg.Stats                          show every character's stats
 *   rpg.SetStat Player Strength 12     change a stat (Player, Dummy = every non-player character, or All)
 *   rpg.ResetStats                     put everyone back to their starting stats and full health
 */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class RPG_CODEX_API URPGCharacterStatsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGCharacterStatsComponent();

	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Stats")
	FRPGBaseStats StartingStats;

	/** The stats component on an actor, or null. */
	static URPGCharacterStatsComponent* FindStats(const AActor* Actor);

	UFUNCTION(BlueprintPure, Category = "RPG|Stats")
	UAbilitySystemComponent* GetAbilitySystem() const { return AbilitySystem; }

	/** Current values (base + any temporary effects). Null only if the owner is gone. */
	const URPGAttributeSet* GetAttributes();

	/** A stat's current value by name ("Strength", "health"...). Returns false if there is no stat with that name. */
	bool GetStatByName(FName StatName, float& OutValue);

	/** Sets a stat's permanent base value by name. Returns false if there is no stat with that name. */
	bool SetStatBaseByName(FName StatName, float Value);

	/** Back to StartingStats with full Health and Stamina. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "RPG|Stats")
	void ResetToStartingStats();

	/** Multi-line text of every stat, for the console and debug display. */
	FString DescribeStats();

private:
	/** Creates the Ability System Component and attribute set the first time anything needs them. */
	void EnsureInitialized();

	UPROPERTY(Transient)
	TObjectPtr<UAbilitySystemComponent> AbilitySystem;

	UPROPERTY(Transient)
	TObjectPtr<URPGAttributeSet> Attributes;
};
