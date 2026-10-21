#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "RPGHitCameraShake.generated.h"

/** Short, sharp camera jolt when the player lands a hit. Played with a scale per reaction (URPGPowerSettings Shake*). */
UCLASS()
class RPG_CODEX_API URPGHitCameraShake : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	URPGHitCameraShake(const FObjectInitializer& ObjectInitializer);
};
