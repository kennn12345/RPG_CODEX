#include "RPGCharacterStatsComponent.h"

#include "RPG_CODEX.h"
#include "RPGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	void ShowStatsMessage(const FString& Message);

	/** The attribute on URPGAttributeSet whose name matches (ignoring case), or an invalid one. */
	FGameplayAttribute FindAttributeByName(FName StatName)
	{
		for (TFieldIterator<FProperty> It(URPGAttributeSet::StaticClass()); It; ++It)
		{
			if (FGameplayAttribute::IsGameplayAttributeDataProperty(*It) && It->GetName().Equals(StatName.ToString(), ESearchCase::IgnoreCase))
			{
				return FGameplayAttribute(*It);
			}
		}
		return FGameplayAttribute();
	}

	/** "Player", "Dummy" (every other character with stats) or "All". */
	TArray<URPGCharacterStatsComponent*> FindStatsTargets(UWorld* World, const FString& Who)
	{
		TArray<URPGCharacterStatsComponent*> Result;
		if (!World || !World->IsGameWorld())
		{
			// Only while playing: in the editor's level the stats aren't set up and must not be.
			ShowStatsMessage(TEXT("Stats commands only work while playing (press Play first)."));
			return Result;
		}

		const APawn* Player = UGameplayStatics::GetPlayerPawn(World, 0);
		const bool bAll = Who.Equals(TEXT("All"), ESearchCase::IgnoreCase);
		const bool bPlayer = Who.Equals(TEXT("Player"), ESearchCase::IgnoreCase);

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			URPGCharacterStatsComponent* Stats = URPGCharacterStatsComponent::FindStats(*It);
			if (!Stats)
			{
				continue;
			}

			const bool bIsPlayer = *It == Player;
			if (bAll || (bPlayer && bIsPlayer) || (!bPlayer && !bIsPlayer))
			{
				Result.Add(Stats);
			}
		}
		return Result;
	}

	void ShowStatsMessage(const FString& Message)
	{
		UE_LOG(LogRPGInput, Log, TEXT("%s"), *Message);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Cyan, Message);
		}
	}

	void StatsCommand(const TArray<FString>& Args, UWorld* World)
	{
		const FString Who = Args.Num() > 0 ? Args[0] : TEXT("All");
		for (URPGCharacterStatsComponent* Stats : FindStatsTargets(World, Who))
		{
			ShowStatsMessage(Stats->DescribeStats());
		}
	}

	void SetStatCommand(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 3)
		{
			ShowStatsMessage(TEXT("Usage: rpg.SetStat <Player|Dummy|All> <Stat> <Value>   e.g. rpg.SetStat Player Strength 12"));
			return;
		}

		const FName StatName(*Args[1]);
		const float Value = FCString::Atof(*Args[2]);
		const TArray<URPGCharacterStatsComponent*> Targets = FindStatsTargets(World, Args[0]);
		if (Targets.IsEmpty())
		{
			ShowStatsMessage(FString::Printf(TEXT("rpg.SetStat: nobody matches '%s' (use Player, Dummy or All)."), *Args[0]));
			return;
		}

		for (URPGCharacterStatsComponent* Stats : Targets)
		{
			if (!Stats->SetStatBaseByName(StatName, Value))
			{
				ShowStatsMessage(FString::Printf(TEXT("rpg.SetStat: no stat called '%s'. Type rpg.Stats to see the names."), *Args[1]));
				return;
			}

			float NewValue = 0.f;
			Stats->GetStatByName(StatName, NewValue);
			ShowStatsMessage(FString::Printf(TEXT("%s: %s = %.1f"), *GetNameSafe(Stats->GetOwner()), *Args[1], NewValue));
		}
	}

	void ResetStatsCommand(const TArray<FString>& Args, UWorld* World)
	{
		for (URPGCharacterStatsComponent* Stats : FindStatsTargets(World, TEXT("All")))
		{
			Stats->ResetToStartingStats();
		}
		ShowStatsMessage(TEXT("Stats reset to starting values."));
	}

	FAutoConsoleCommandWithWorldAndArgs GStatsCommand(
		TEXT("rpg.Stats"),
		TEXT("Show stats. rpg.Stats [Player|Dummy|All]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&StatsCommand));

	FAutoConsoleCommandWithWorldAndArgs GSetStatCommand(
		TEXT("rpg.SetStat"),
		TEXT("Change a stat. rpg.SetStat <Player|Dummy|All> <Stat> <Value>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetStatCommand));

	FAutoConsoleCommandWithWorldAndArgs GResetStatsCommand(
		TEXT("rpg.ResetStats"),
		TEXT("Reset every character to its starting stats and full health."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ResetStatsCommand));
}

URPGCharacterStatsComponent::URPGCharacterStatsComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void URPGCharacterStatsComponent::BeginPlay()
{
	Super::BeginPlay();
	EnsureInitialized();
}

URPGCharacterStatsComponent* URPGCharacterStatsComponent::FindStats(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<URPGCharacterStatsComponent>() : nullptr;
}

void URPGCharacterStatsComponent::EnsureInitialized()
{
	AActor* Owner = GetOwner();
	if (Attributes || !Owner)
	{
		return;
	}

	AbilitySystem = Owner->FindComponentByClass<UAbilitySystemComponent>();
	if (!AbilitySystem)
	{
		AbilitySystem = NewObject<UAbilitySystemComponent>(Owner, TEXT("AbilitySystem"));
		AbilitySystem->RegisterComponent();
	}
	AbilitySystem->InitAbilityActorInfo(Owner, Owner);

	Attributes = NewObject<URPGAttributeSet>(Owner, TEXT("RPGAttributes"));
	AbilitySystem->AddSpawnedAttribute(Attributes);

	ResetToStartingStats();
}

const URPGAttributeSet* URPGCharacterStatsComponent::GetAttributes()
{
	EnsureInitialized();
	return Attributes;
}

bool URPGCharacterStatsComponent::GetStatByName(FName StatName, float& OutValue)
{
	EnsureInitialized();
	const FGameplayAttribute Attribute = FindAttributeByName(StatName);
	if (!AbilitySystem || !Attribute.IsValid())
	{
		return false;
	}

	OutValue = AbilitySystem->GetNumericAttribute(Attribute);
	return true;
}

bool URPGCharacterStatsComponent::SetStatBaseByName(FName StatName, float Value)
{
	EnsureInitialized();
	const FGameplayAttribute Attribute = FindAttributeByName(StatName);
	if (!AbilitySystem || !Attribute.IsValid())
	{
		return false;
	}

	AbilitySystem->SetNumericAttributeBase(Attribute, Value);

	// Raising a maximum also fills the current value up to it, so "SetStat MaxHealth 500" gives 500 health.
	if (Attribute == URPGAttributeSet::GetMaxHealthAttribute())
	{
		AbilitySystem->SetNumericAttributeBase(URPGAttributeSet::GetHealthAttribute(), Value);
	}
	else if (Attribute == URPGAttributeSet::GetMaxStaminaAttribute())
	{
		AbilitySystem->SetNumericAttributeBase(URPGAttributeSet::GetStaminaAttribute(), Value);
	}
	return true;
}

void URPGCharacterStatsComponent::ResetToStartingStats()
{
	EnsureInitialized();
	if (!AbilitySystem)
	{
		return;
	}

	// Maximums first, so Health and Stamina aren't clamped to an old maximum.
	AbilitySystem->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), StartingStats.MaxHealth);
	AbilitySystem->SetNumericAttributeBase(URPGAttributeSet::GetHealthAttribute(), StartingStats.MaxHealth);
	AbilitySystem->SetNumericAttributeBase(URPGAttributeSet::GetMaxStaminaAttribute(), StartingStats.MaxStamina);
	AbilitySystem->SetNumericAttributeBase(URPGAttributeSet::GetStaminaAttribute(), StartingStats.MaxStamina);
	AbilitySystem->SetNumericAttributeBase(URPGAttributeSet::GetStrengthAttribute(), StartingStats.Strength);
	AbilitySystem->SetNumericAttributeBase(URPGAttributeSet::GetAttackSpeedAttribute(), StartingStats.AttackSpeed);
	AbilitySystem->SetNumericAttributeBase(URPGAttributeSet::GetMassAttribute(), StartingStats.Mass);
	AbilitySystem->SetNumericAttributeBase(URPGAttributeSet::GetDurabilityAttribute(), StartingStats.Durability);
	AbilitySystem->SetNumericAttributeBase(URPGAttributeSet::GetStabilityAttribute(), StartingStats.Stability);
	AbilitySystem->SetNumericAttributeBase(URPGAttributeSet::GetStaggerResistanceAttribute(), StartingStats.StaggerResistance);
	AbilitySystem->SetNumericAttributeBase(URPGAttributeSet::GetRecoveryAttribute(), StartingStats.Recovery);
	AbilitySystem->SetNumericAttributeBase(URPGAttributeSet::GetMagicPowerAttribute(), StartingStats.MagicPower);
	AbilitySystem->SetNumericAttributeBase(URPGAttributeSet::GetMagicControlAttribute(), StartingStats.MagicControl);
}

FString URPGCharacterStatsComponent::DescribeStats()
{
	const URPGAttributeSet* A = GetAttributes();
	if (!A)
	{
		return FString();
	}

	return FString::Printf(
		TEXT("%s\n")
		TEXT("  Health %.0f/%.0f   Stamina %.0f/%.0f\n")
		TEXT("  Strength %.1f   AttackSpeed %.2f   Mass %.0f kg\n")
		TEXT("  Durability %.0f   Stability %.0f   StaggerResistance %.0f   Recovery %.0f\n")
		TEXT("  MagicPower %.0f   MagicControl %.0f"),
		*GetNameSafe(GetOwner()),
		A->GetHealth(), A->GetMaxHealth(), A->GetStamina(), A->GetMaxStamina(),
		A->GetStrength(), A->GetAttackSpeed(), A->GetMass(),
		A->GetDurability(), A->GetStability(), A->GetStaggerResistance(), A->GetRecovery(),
		A->GetMagicPower(), A->GetMagicControl());
}
