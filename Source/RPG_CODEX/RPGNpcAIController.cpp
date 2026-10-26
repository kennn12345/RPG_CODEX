#include "RPGNpcAIController.h"

#include "RPGNpcCharacter.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"

ARPGNpcAIController::ARPGNpcAIController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ARPGNpcAIController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const ARPGNpcCharacter* Npc = Cast<ARPGNpcCharacter>(GetPawn());
	UWorld* World = GetWorld();
	if (!Npc || !World) return;
	const float Now = World->GetTimeSeconds();

	// Reacting / down / getting up: stand still, and start walking again a moment after it's over.
	if (Npc->IsBusy())
	{
		bWasBusy = true;
		return;
	}
	if (bWasBusy)
	{
		bWasBusy = false;
		NextMoveTime = Now + FMath::FRandRange(0.4f, 1.2f);
	}
	if (!Npc->bWander || GetMoveStatus() != EPathFollowingStatus::Idle || Now < NextMoveTime) return;

	// Somewhere it can walk to (the navigation mesh routes it around walls and corners), at least a short walk away.
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	FNavLocation Spot;
	bool bFound = false;
	for (int32 Try = 0; Try < 6 && Nav && !bFound; ++Try)
		bFound = Nav->GetRandomReachablePointInRadius(Npc->GetHome(), Npc->WanderRadius, Spot)
			&& FVector::Dist2D(Spot.Location, Npc->GetActorLocation()) >= 150.f;
	if (bFound) MoveToLocation(Spot.Location, 40.f);
	NextMoveTime = Now + FMath::FRandRange(Npc->WanderPauseMin, FMath::Max(Npc->WanderPauseMin, Npc->WanderPauseMax));
}
