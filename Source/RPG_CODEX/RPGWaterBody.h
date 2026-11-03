#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RPGWaterBody.generated.h"

class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class URPGWaterZoneComponent;

// A lake, pond or ocean: a water zone (box; its top is the surface) plus the visible surface mesh at that height.
// Scale the zone's box to cover the water; the surface mesh is placed at the box top and scaled with SurfaceScale.
UCLASS()
class RPG_CODEX_API ARPGWaterBody : public AActor
{
	GENERATED_BODY()

public:
	ARPGWaterBody();

	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Water")
	TObjectPtr<URPGWaterZoneComponent> Zone;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Water")
	TObjectPtr<UStaticMeshComponent> Surface;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	TObjectPtr<UStaticMesh> WaterMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	TObjectPtr<UMaterialInterface> WaterMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	FVector SurfaceScale = FVector(1.f);
};
