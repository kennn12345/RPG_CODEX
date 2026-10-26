#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RPGEnergySpellTypes.h"
#include "RPGEnergyOrb.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class URPGEnergySpellComponent;

/** Original procedural mesh/material effect; charge, swept projectile and brief impact share pooled visual parts. */
UCLASS(NotBlueprintable)
class RPG_CODEX_API ARPGEnergyOrb : public AActor
{
    GENERATED_BODY()
public:
    ARPGEnergyOrb();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
    void ChargeAt(URPGEnergySpellComponent* Source, bool bLeft);
    void StopChargeEffects();
    void Launch(const FVector& Direction, float Charge, float Radius, float Speed, float Range, float Damage, float Impulse, FLinearColor Color);
    static bool Trace(UWorld* World, const FVector& Start, const FVector& End, float Radius, AActor* Ignore, FHitResult& Hit);
    static void ApplyImpact(const FRPGEnergyImpact& Impact);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Energy") bool bLaunched = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Energy") bool bImpacted = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Energy") float OrbRadius = 5.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Energy") FRPGEnergyImpact Impact;
private:
    UPROPERTY() TObjectPtr<USceneComponent> Root;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Core;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Shell;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Filaments;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Sparks;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Trail;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> StaticArcs;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CoreMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ShellMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> RingMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> StaticMaterial;
    TWeakObjectPtr<URPGEnergySpellComponent> ChargeSource;
    bool bLeftHand = false;
    FVector Velocity = FVector::ZeroVector;
    float RemainingRange = 6000.f;
    float Age = 0.f;
    float ImpactAge = 0.f;
    FLinearColor Tint = FLinearColor(0.08f,0.5f,1.f);
    TArray<FVector> TrailPositions;
    struct FStaticCluster
    {
        int32 Cycle = INDEX_NONE;
        TArray<FVector> Points;
    };
    FStaticCluster StaticClusters[3];
    int32 StaticSeed = 0;
    float StaticTime = 0.f;
    void HitTarget(const FHitResult& Hit);
    void UpdateVisual(float Delta, float Charge);
    void UpdateStatic(float Delta, bool bFullyHeld);
};
