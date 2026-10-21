#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGFlightAnimationComponent.generated.h"

class UAnimMontage;
class UAnimSequence;

/**
 * Flight poses: while the character is flying, loops hover / forward / fast flight (RPG_Proto_Fly*) on the full body,
 * picked by flight speed (fast flight on L3 always uses the fast pose), cross-fading when it changes. An attack montage
 * takes over the slot while it plays; the flight loop comes back on its own afterwards. Stops when flight ends.
 * Not yet: tilting into climbs / dives, upper-body-only attacks in the air.
 */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class RPG_CODEX_API URPGFlightAnimationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGFlightAnimationComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation")
	TSoftObjectPtr<UAnimSequence> Hover;
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation")
	TSoftObjectPtr<UAnimSequence> Forward;
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation")
	TSoftObjectPtr<UAnimSequence> Fast;

	/** Horizontal flight speed (cm/s) above which the forward pose plays instead of hover... */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "0"))
	float ForwardSpeed = 150.f;

	/** ...and above which the fast pose plays. */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "0"))
	float FastSpeed = 900.f;

	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "0"))
	float BlendTime = 0.35f;

private:
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> Playing;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> PlayingClip;
};
