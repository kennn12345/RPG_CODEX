#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "RPGEnergySpellTypes.generated.h"

/** A separate damage path: energy never opens a melee hit window or replaces punches. */
USTRUCT(BlueprintType)
struct RPG_CODEX_API FRPGEnergyImpact
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Energy") TObjectPtr<AActor> Attacker = nullptr;
    UPROPERTY(BlueprintReadOnly, Category="Energy") FHitResult Hit;
    UPROPERTY(BlueprintReadOnly, Category="Energy") FVector Direction = FVector::ForwardVector;
    UPROPERTY(BlueprintReadOnly, Category="Energy") float Charge = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Energy") float Damage = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Energy") float Impulse = 0.f;
};

UINTERFACE(BlueprintType)
class RPG_CODEX_API URPGEnergySpellTarget : public UInterface { GENERATED_BODY() };
class RPG_CODEX_API IRPGEnergySpellTarget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Energy")
    void ReceiveEnergyHit(const FRPGEnergyImpact& Impact);
};

/** Immutable snapshot copied to the animation worker. Left = 0, right = 1. */
struct FRPGEnergyPose
{
    float Weight = 0.f;
    float Charge = 0.f;
    float Extension = 0.f;
    float Time = 0.f;
    float HoverPitch = 0.f;
    float HoverBank = 0.f;
    FVector DirectionCS = FVector::YAxisVector;
    FVector UpCS = FVector::UpVector;
    FVector RightCS = FVector::XAxisVector;
};
