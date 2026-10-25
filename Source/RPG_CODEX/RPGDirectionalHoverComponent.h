#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGDirectionalHoverComponent.generated.h"

/** View-facing hover locomotion. Forward travel flight remains owned by the existing flight components. */
UCLASS(ClassGroup=(RPG), meta=(BlueprintSpawnableComponent))
class RPG_CODEX_API URPGDirectionalHoverComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    URPGDirectionalHoverComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Tick) override;
    /** Called by the GASP bridge before its pre-movement settings are calculated. */
    void UpdateIntent();
    bool IsHovering() const { return bHoverRequested; }
    bool WantsHoverFacing() const { return bHoverRequested || bFacingHandoff; }
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG|Directional Hover",meta=(ClampMin="50")) float HoverSpeed = 320.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG|Directional Hover",meta=(ClampMin="0.05")) float PoseResponse = 0.22f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG|Directional Hover",meta=(ClampMin="0")) float IdleBobAmplitude = 1.6f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG|Directional Hover",meta=(ClampMin="1")) float IdleBobPeriod = 3.4f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG|Directional Hover",meta=(ClampMin="0",ClampMax="50")) float SideKneeLift = 30.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG|Directional Hover",meta=(ClampMin="0",ClampMax="70")) float SideKneeBend = 46.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG|Directional Hover",meta=(ClampMin="0",ClampMax="25")) float SideKneeOut = 22.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG|Directional Hover",meta=(ClampMin="0",ClampMax="70")) float SideHipOpen = 40.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG|Directional Hover",meta=(ClampMin="0",ClampMax="25")) float SideFootTuck = 14.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG|Directional Hover",meta=(ClampMin="0.1")) float FootTuckResponse = 0.35f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG|Directional Hover",meta=(ClampMin="0",ClampMax="20")) float TrailingArmMinOut = 8.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG|Directional Hover",meta=(ClampMin="0",ClampMax="15",Units="cm")) float TrailingArmSpacing = 5.08f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG|Directional Hover",meta=(ClampMin="0",ClampMax="25")) float SideBodyLean = 12.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG|Directional Hover",meta=(ClampMin="0",ClampMax="10")) float SideChestCounterLean = 2.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG|Directional Hover",meta=(ClampMin="0",ClampMax="25")) float SideArmLean = 14.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG|Directional Hover",meta=(ClampMin="0.05")) float SideArmResponse = 0.24f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG|Directional Hover",meta=(ClampMin="0.1")) float KneeDriftTime = 0.4f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG|Directional Hover",meta=(ClampMin="30",ClampMax="300")) float HoverTurnSpeed = 150.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="RPG|Directional Hover",meta=(ClampMin="90")) float HoverTurnAcceleration = 540.f;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="RPG|Directional Hover") bool bHoverRequested = false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="RPG|Directional Hover") FVector2D DirectionBlend = FVector2D::ZeroVector;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="RPG|Directional Hover") float PoseWeight = 0.f;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="RPG|Directional Hover") float BobWeight = 0.f;
private:
    float Phase = 0.f;
    float FacingYaw = 0.f;
    float FacingVelocity = 0.f, TurnBlend = 0.f;
    float ArmSideBlend = 0.f;
    bool bFacingHandoff = false;
    FVector2D KneeOutBlend = FVector2D::ZeroVector;
    FVector2D FootTuckBlend = FVector2D::ZeroVector, FootTuckVelocity = FVector2D::ZeroVector;
    bool bHaveFacing = false;
    FVector LastVelocity = FVector::ZeroVector;
    bool bHaveVelocity = false;
    UPROPERTY(Transient) TObjectPtr<class UInputAction> MoveAction;
    UPROPERTY(Transient) TObjectPtr<class UInputAction> WorldMoveAction;
};
