#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "RPGPlayerTypes.h"
#include "RPGMeleeTarget.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class URPGMeleeTarget : public UInterface
{
	GENERATED_BODY()
};

/**
 * Optional: anything that wants to know it was struck by a punch / kick (the test dummy, later enemies).
 * Targets without it are still detected and reported; this only lets them react.
 * In a Blueprint: Class Settings > Implemented Interfaces > Add > RPG Melee Target.
 */
class RPG_CODEX_API IRPGMeleeTarget
{
	GENERATED_BODY()

public:
	/** Hit confirmation only. Damage / stagger / knockback belong to the Power System, not here. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RPG|Melee")
	void ReceiveMeleeHit(const FRPGMeleeHitInfo& Hit);

	virtual void ReceiveMeleeHit_Implementation(const FRPGMeleeHitInfo& Hit) {}

	/** Every frame a striking limb is held against this target's surface and its animation still drives it in
	 *  (URPGStrikeContactComponent): the limb's speed into the surface at that point (cm/s, world). A target that can
	 *  give way (the dummy slides back) lets the strike follow through; one that ignores it stops the limb dead. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RPG|Melee")
	void ReceiveContactPush(const FVector& PushVelocity, FName Bone);

	virtual void ReceiveContactPush_Implementation(const FVector& PushVelocity, FName Bone) {}
};
