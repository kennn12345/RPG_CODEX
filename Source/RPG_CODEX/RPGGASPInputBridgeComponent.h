#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGGASPInputBridgeComponent.generated.h"

class UEnhancedInputLocalPlayerSubsystem;
class UInputMappingContext;
class FStructProperty;
class FBoolProperty;

/** Adapts RPG controls to the duplicated GASP CMC pawn without editing the sample or old player. */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class RPG_CODEX_API URPGGASPInputBridgeComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    URPGGASPInputBridgeComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    UPROPERTY(EditAnywhere, Category = "RPG|GASP")
    TSoftObjectPtr<UInputMappingContext> SampleMappingContext;

    /** GASP keeps movement/look, keyboard jump/traversal, and keyboard walk. RPG owns the other buttons. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|GASP")
    bool bInputReady = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|GASP")
    bool bStateReady = false;
    /** Camera-facing eight-way ground locomotion, using GASP's existing run databases. */
    UPROPERTY(EditAnywhere,Category="RPG|GASP") bool bDirectionalGroundMovement = true;

private:
    void ConfigureInput();
    void SynchronizeLocomotion();

    UPROPERTY(Transient)
    TObjectPtr<UInputMappingContext> SampleContext;

    UPROPERTY(Transient)
    TObjectPtr<UInputMappingContext> LocomotionContext;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UInputMappingContext>> LegacyContexts;

    TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> BoundSubsystem;
    FStructProperty* InputStateProperty = nullptr;
    FBoolProperty* SprintProperty = nullptr;
    FBoolProperty* CrouchProperty = nullptr;
    FBoolProperty* AimProperty = nullptr;
    FBoolProperty* StrafeProperty = nullptr;
    bool bKeyboardConfigured = false;
    float DirectionalGroundHold = 0.f;
};
