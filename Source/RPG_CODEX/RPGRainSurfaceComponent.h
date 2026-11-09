#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGRainSurfaceComponent.generated.h"
class UInstancedStaticMeshComponent;
class UMaterialParameterCollection;
class URPGWaterZoneComponent;

/** Fixed world-space impacts; bounded traces and pools, no camera-attached splash sheet. */
UCLASS(ClassGroup=(RPG))
class RPG_CODEX_API URPGRainSurfaceComponent:public UActorComponent
{
    GENERATED_BODY()
public:
    URPGRainSurfaceComponent();
    virtual void BeginPlay()override;
    virtual void TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Function)override;
    float GetWetness()const{return Wetness;}
    void PreviewWetness(float Value){Wetness=FMath::Clamp(Value,0.f,1.f);}
    int32 GetActiveImpactCount()const;
    int32 GetTraceCountLastTick()const{return LastTraces;}
    int32 GetWaterImpactCount()const;
private:
    struct FImpact {FVector Position=FVector::ZeroVector,Normal=FVector::UpVector;float Age=100,Life=.5f,Size=1;TWeakObjectPtr<URPGWaterZoneComponent> Water;};
    static constexpr int32 PoolSize=256;
    TArray<FImpact> Impacts;
    TArray<FTransform> RingTransforms,SplashTransforms;
    TArray<TWeakObjectPtr<URPGWaterZoneComponent>> WaterZones;
    UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Rings;
    UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Splashes;
    UPROPERTY(Transient) TObjectPtr<UMaterialParameterCollection> Lighting;
    FRandomStream Random;
    float Wetness=0,SpawnCredit=0;
    int32 NextSlot=0,LastTraces=0;
};
