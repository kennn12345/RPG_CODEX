#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGGroundFogComponent.generated.h"
class ULocalFogVolumeComponent;
class UPrimitiveComponent;
class URPGWaterZoneComponent;

/** Terrain-following spherical caps: dense fog stays below six metres above local ground. */
UCLASS(ClassGroup=(RPG))
class RPG_CODEX_API URPGGroundFogComponent:public UActorComponent
{
    GENERATED_BODY()
public:
    URPGGroundFogComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* Function) override;
    void SetWeather(float Fog);
    int32 GetVolumeCount()const{return Volumes.Num();}
    static constexpr float LayerHeight=609.6f; // 20 feet, cm.
private:
    void UpdateGrid();
    float GroundHeight(float X,float Y)const;
    UPROPERTY(Transient) TArray<TObjectPtr<ULocalFogVolumeComponent>> Volumes;
    TArray<TWeakObjectPtr<UPrimitiveComponent>> Ground;
    TArray<TWeakObjectPtr<URPGWaterZoneComponent>> Water;
    TMap<FIntPoint,float> HeightCache;
    FIntPoint Anchor=FIntPoint(MAX_int32,MAX_int32);
    struct FCell { ULocalFogVolumeComponent* Volume=nullptr; float Alpha=0; };
    TMap<FIntPoint,FCell> Cells;
    float Strength=0,Timer=0;
};
