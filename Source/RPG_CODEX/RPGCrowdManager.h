#pragma once

#include "CoreMinimal.h"
#include "Navigation/CrowdManager.h"
#include "RPGCrowdManager.generated.h"

/** Keep normal Detour navigation, but release its navmesh without rebuilding it during world shutdown. */
UCLASS(Config=Engine)
class RPG_CODEX_API URPGCrowdManager : public UCrowdManager
{
	GENERATED_BODY()
public:
	virtual void OnNavDataUnregistered(ANavigationData& NavData) override;
};
