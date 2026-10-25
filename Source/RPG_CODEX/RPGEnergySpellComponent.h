#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGPlayerTypes.h"
#include "RPGEnergySpellComponent.generated.h"

class ARPGEnergyOrb;
class URPGCombatStateComponent;
class USkeletalMeshComponent;

USTRUCT(BlueprintType)
struct FRPGEnergyHandState
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Energy") bool bHeld = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Energy") float Charge = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Energy") float PoseWeight = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Energy") int32 ShotsFired = 0;
    float HeldSeconds = 0.f;
    float ReleaseSeconds = 10.f;
    float ReleasedCharge = 0.f;
    bool bPendingShot = false;
    float ReleaseWait = 0.f;
    TWeakObjectPtr<ARPGEnergyOrb> Orb;
};

/** Permanently equipped test spell, independently charged by LB and RB. */
UCLASS(ClassGroup=(RPG), meta=(BlueprintSpawnableComponent))
class RPG_CODEX_API URPGEnergySpellComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    URPGEnergySpellComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* Tick) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Energy", meta=(ClampMin="0.1")) float FullChargeSeconds = 1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Energy", meta=(ClampMin="1")) float BaseRadius = 10.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Energy", meta=(ClampMin="1")) float FullSizeMultiplier = 2.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Energy") FLinearColor Color = FLinearColor(0.08f,0.5f,1.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Energy", meta=(ClampMin="100")) float ProjectileSpeed = 3300.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Energy", meta=(ClampMin="500")) float Range = 6000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Energy") float TapDamage = 6.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Energy") float ChargedDamage = 28.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Energy") float TapImpulse = 1200.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Energy") float ChargedImpulse = 5000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Energy|Aim", meta=(ClampMin="1",ClampMax="20")) float ForwardAimHalfAngle = 12.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Energy|Aim", meta=(ClampMin="30",ClampMax="720")) float FacingTurnRate = 300.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Energy|Aim") float FacingYawError = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Energy|Aim") float HoverPitch = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Energy|Aim") float HoverBank = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Energy") FRPGEnergyHandState Left;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Energy") FRPGEnergyHandState Right;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Energy") FVector AimPoint = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Energy") FVector AimDirection = FVector::ForwardVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Energy") bool bAimWithinLimits = true;

    UFUNCTION(BlueprintCallable, Category="Energy") void Press(ERPGHand Hand);
    UFUNCTION(BlueprintCallable, Category="Energy") void Release(ERPGHand Hand);
    UFUNCTION(BlueprintCallable, Category="Energy") void CancelAll();
    bool CanCast() const;
    FVector GetOrbLocation(bool bLeft) const;
    float GetRadius(float Charge) const;
    bool IsAiming() const;
private:
    UFUNCTION() void OnAction(const FRPGPlayerActionInfo& Info);
    void UpdateAim(float Delta);
    void UpdateHand(FRPGEnergyHandState& Hand, bool bLeft, float Delta);
    void Fire(FRPGEnergyHandState& Hand, bool bLeft);
    TWeakObjectPtr<URPGCombatStateComponent> State;
    TWeakObjectPtr<USkeletalMeshComponent> Mesh;
    float FacingYaw = 0.f;
    bool bFacingInitialized = false;
};
