#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RPGScatter.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

// Scatters one mesh as instances over whatever is below it (traced straight down), inside a box around the actor:
// grass fields, flower patches, pebbles, boulders, trees. Rules: slope limit, keep off a path polyline, stay above a
// height, out of water zones, optional noise patches (clustered flowers / wheat). Instances are saved with the level;
// call Rebuild (button in the details panel) after moving it or changing the terrain - it does not rebuild on load.
UCLASS()
class RPG_CODEX_API ARPGScatter : public AActor
{
	GENERATED_BODY()

public:
	ARPGScatter();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scatter")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Instances;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
	TObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
	TObjectPtr<UMaterialInterface> Material;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter", meta = (ClampMin = "0"))
	int32 Count = 1000;

	// Half size (cm) of the area around the actor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
	FVector2D HalfExtent = FVector2D(5000.f, 5000.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
	int32 Seed = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
	FVector2D ScaleRange = FVector2D(0.8f, 1.2f);

	// Surfaces steeper than this are skipped (minimum normal Z).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter", meta = (ClampMin = "0", ClampMax = "1"))
	float MinUpNormal = 0.75f;

	// Tilt instances to the surface (rocks) instead of standing upright (grass, trees).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
	bool bAlignToSurface = false;

	// Push instances into the ground (cm).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
	float SinkDepth = 0.f;

	// Keep this far (cm) from the ExcludePath polyline (world XY).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
	TArray<FVector> ExcludePath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
	float ExcludeRadius = 0.f;

	// Keep-out circles (world XY): X, Y = centre, Z = radius (cm) - e.g. open ground around a lake, a clearing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
	TArray<FVector> ExcludeCircles;

	// No instances where the ground is below this world Z (e.g. under a lake surface).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
	float MinGroundZ = -1.0e7f;

	// Skip ground under any water zone (lakes, ponds, rivers); WaterMargin (cm) also keeps the wet shoreline clear.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
	bool bSkipWater = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
	float WaterMargin = 10.f;

	// Noise patches: cell size (cm, 0 = everywhere) and how much of the area is kept (0..1, smaller = sparser patches).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
	float PatchSize = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter", meta = (ClampMin = "0", ClampMax = "1"))
	float PatchCoverage = 0.5f;

	// Blocking collision for big things (rocks, trees); off for grass and flowers.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
	bool bCollision = false;

	// Instances fade out beyond this distance (cm, 0 = never).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
	float CullDistance = 0.f;

	// Number of instances actually placed by the last Rebuild.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scatter")
	int32 Placed = 0;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Scatter")
	void Rebuild();
};
