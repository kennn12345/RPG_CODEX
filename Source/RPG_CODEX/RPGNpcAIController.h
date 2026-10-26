#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "RPGNpcAIController.generated.h"

/**
 * Walks an ARPGNpcCharacter around: picks a random spot it can actually reach (navigation mesh) within its
 * WanderRadius of home, walks there, pauses, picks another. Waits while the NPC is reacting, down or getting up.
 */
UCLASS()
class RPG_CODEX_API ARPGNpcAIController : public AAIController
{
	GENERATED_BODY()

public:
	ARPGNpcAIController();
	virtual void Tick(float DeltaTime) override;

private:
	float NextMoveTime = 0.f;
	bool bWasBusy = false;
};
