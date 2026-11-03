#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RPGFloatingProp.generated.h"

class URPGBuoyancyComponent;
class UStaticMeshComponent;

// A physics-simulated prop (barrel, crate, log, boat) that floats on any water zone via URPGBuoyancyComponent.
UCLASS()
class RPG_CODEX_API ARPGFloatingProp : public AActor
{
	GENERATED_BODY()

public:
	ARPGFloatingProp();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floating")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floating")
	TObjectPtr<URPGBuoyancyComponent> Buoyancy;
};
