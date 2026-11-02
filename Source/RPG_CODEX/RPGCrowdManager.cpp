#include "RPGCrowdManager.h"

#include "Engine/World.h"

void URPGCrowdManager::OnNavDataUnregistered(ANavigationData& NavData)
{
	const UWorld* World = GetWorld();
	if (World && World->bIsTearingDown)
	{
		if (MyNavData == &NavData)
		{
			// UE 5.8's base handler rebuilds Detour after removing its nav data, including when PIE is ending.
			// There cannot be a replacement navmesh in a shutting-down world. Unbind and release it instead.
			SetNavData(nullptr, false);
#if WITH_RECAST
			DestroyCrowdManager();
#endif
		}
		return;
	}
	Super::OnNavDataUnregistered(NavData);
}
