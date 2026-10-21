#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGInteractionComponent.generated.h"

/**
 * Finds the best nearby object that implements the RPG Interactable interface.
 * The A button asks this component first; if it finds something, A interacts instead of jumping.
 */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class RPG_CODEX_API URPGInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** How far away (in cm) an interactable can be. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Interaction")
	float InteractRange = 200.f;

	/** How much the object must be in front of the camera. 1 = dead ahead, 0 = anywhere in front, -1 = anywhere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Interaction", meta = (ClampMin = "-1", ClampMax = "1"))
	float MinFacingDot = 0.3f;

	/** If true, A interacts when something is in range. If false, A always jumps (interaction can be moved to another rule later). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Interaction")
	bool bInteractionHasPriorityOverJump = true;

	/** Returns the closest valid interactable in front of the player, or nothing. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Interaction")
	AActor* FindBestInteractable() const;
};
