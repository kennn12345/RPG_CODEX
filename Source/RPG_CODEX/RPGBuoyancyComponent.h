#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGBuoyancyComponent.generated.h"

// Makes the owner's physics-simulating root float on any URPGWaterZoneComponent: each pontoon (local point) is pushed
// up in proportion to how deep it sits under the waved surface, damped, and dragged along the zone's current.
// With the default 8 corner pontoons a box settles about half-way between its top and bottom corners' lift balance.
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class RPG_CODEX_API URPGBuoyancyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGBuoyancyComponent();

	// Local-space float points; empty = the eight corners of the root's local bounds (pulled in 20%).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buoyancy")
	TArray<FVector> Pontoons;

	// Depth (cm) over which a pontoon goes from no lift to full lift.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buoyancy", meta = (ClampMin = "1"))
	float PontoonDepth = 40.f;

	// Full-lift force as a multiple of the body's weight.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buoyancy", meta = (ClampMin = "0"))
	float Strength = 2.f;

	// Velocity damping per second while submerged (vertical, horizontal).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buoyancy")
	FVector2D Damping = FVector2D(2.5f, 0.3f);

	// How strongly the current carries the body (1/s); drifts at FlowDrag / (FlowDrag + Damping.Y) of the current.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buoyancy")
	float FlowDrag = 0.8f;

	// True while any pontoon is in water (for tests / gameplay).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Buoyancy")
	bool bInWater = false;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
};
