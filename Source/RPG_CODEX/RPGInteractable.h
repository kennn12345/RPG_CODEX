#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "RPGPlayerTypes.h"
#include "RPGInteractable.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class URPGInteractable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Add this interface to any Blueprint the player can use with the A button
 * (NPCs, doors, chests, pickups, beds...).
 * In a Blueprint: Class Settings > Implemented Interfaces > Add > RPG Interactable.
 */
class RPG_CODEX_API IRPGInteractable
{
	GENERATED_BODY()

public:
	/** Return false to temporarily disable interaction (locked door, NPC busy...). Defaults to true. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RPG|Interaction")
	bool CanInteract(AActor* Interactor) const;

	/** Called when the player presses A on this object. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RPG|Interaction")
	void Interact(AActor* Interactor);

	/** What kind of interactable this is. Defaults to Generic. Return Bed to allow D-Pad Down = Sleep. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RPG|Interaction")
	ERPGInteractionType GetInteractionType() const;

	virtual bool CanInteract_Implementation(AActor* Interactor) const { return true; }
	virtual void Interact_Implementation(AActor* Interactor) {}
	virtual ERPGInteractionType GetInteractionType_Implementation() const { return ERPGInteractionType::Generic; }
};
