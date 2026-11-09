#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGWeatherEffectsComponent.generated.h"
class UInstancedStaticMeshComponent;
class UDirectionalLightComponent;
class UAudioComponent;
class USoundWaveProcedural;
class UMaterialInterface;

/** Bounded local rain volume, roof/ground clipping, generated rain and thunder audio. */
UCLASS(ClassGroup=(RPG),meta=(BlueprintSpawnableComponent))
class RPG_CODEX_API URPGWeatherEffectsComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    URPGWeatherEffectsComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* Function) override;
    UFUNCTION(BlueprintCallable,Category="Weather") void TriggerLightning();
    UFUNCTION(BlueprintPure,Category="Weather") float GetLightningFlash() const { return Flash; }
    UFUNCTION(BlueprintPure,Category="Weather") int32 GetRainInstanceCount() const;
    bool IsRainAudioPlaying() const;
    bool IsThunderAudioPlaying() const;
    UPROPERTY(EditAnywhere,Category="Weather") TSoftObjectPtr<UMaterialInterface> RainMaterial;
private:
    UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> RainMesh;
    UPROPERTY(Transient) TObjectPtr<UDirectionalLightComponent> FlashLight;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> RainAudio;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> ThunderAudio;
    UPROPERTY(Transient) TObjectPtr<USoundWaveProcedural> RainWave;
    UPROPERTY(Transient) TObjectPtr<USoundWaveProcedural> ThunderWave;
    TArray<uint8> RainPCM,ThunderPCM;
    TArray<FTransform> RainTransforms;
    TArray<FVector> RainPositions;
    FVector PreviousRainCamera=FVector::ZeroVector;
    bool bRainInitialized=false,bRainWasVisible=false;
    float FloorHeights[75]{};
    float RainTime=0,TraceTime=0,FlashAge=100,Flash=0,ThunderDelay=-1,ThunderAge=100,NextFlash=8,AudioVolume=0;
    bool bSheltered=false;
    FRandomStream Random;
};
