#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RPGWaterWaves.h"
#include "RPGStylizedRiver.generated.h"

class UMaterialInterface;
class USplineComponent;
class USplineMeshComponent;
class UStaticMesh;

// A river of any length and shape: one straight water strip (flow runs along the mesh's +X and its UV V) is bent along
// the spline in pieces of about PieceLength, so the flowing texture stays continuous. Spline point scale Y widens or
// narrows the river at that point. Custom primitive data 0/1 = the piece's flow-UV offset and direction (seamless flow).
// The water has no collision - characters can walk into it; the riverbed is terrain. Each piece also gets a water
// zone (wading / underwater / buoyancy / current) unless bCreateWaterZones is off.
UCLASS()
class RPG_CODEX_API ARPGStylizedRiver : public AActor
{
	GENERATED_BODY()

public:
	ARPGStylizedRiver();

	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "River")
	TObjectPtr<USplineComponent> Spline;

	// Straight water strip lying along +X, width along Y (SM_Water_River_Strip10).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "River")
	TObjectPtr<UStaticMesh> WaterMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "River")
	TObjectPtr<UMaterialInterface> WaterMaterial;

	// River width (cm) where spline point scale Y is 1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "River", meta = (ClampMin = "100"))
	float Width = 800.f;

	// Target length of each bent piece (cm); the spline is split evenly so pieces are close to this.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "River", meta = (ClampMin = "100"))
	float PieceLength = 1000.f;

	// One water zone (URPGWaterZoneComponent) per piece: wading/underwater for characters, buoyancy and current.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "River|Water")
	bool bCreateWaterZones = true;

	// Depth of the zones below the surface (cm) - deeper than the riverbed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "River|Water", meta = (ClampMin = "10"))
	float ZoneDepth = 300.f;

	// Current along the spline (cm/s) for floating objects; match the material's FlowSpeed (m/s) x 100.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "River|Water")
	float FlowSpeed = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "River|Water")
	FRPGWaveSettings Waves;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "River|Water")
	TObjectPtr<UMaterialInterface> UnderwaterMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "River|Water")
	TObjectPtr<UMaterialInterface> CausticMaterial;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "River")
	void Rebuild();
};
