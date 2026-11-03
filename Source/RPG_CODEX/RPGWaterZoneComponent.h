#pragma once

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "RPGWaterWaves.h"
#include "RPGWaterZoneComponent.generated.h"

class ACharacter;
class UDecalComponent;
class UMaterialInterface;
class UPostProcessComponent;

// A body of water as a box whose TOP face is the resting water surface (no blocking collision - only overlaps).
// - Surface height = box top + the shared Gerstner waves (RPGWaterWaves.h), queried by buoyancy and characters.
// - Characters inside: wading slows them as the water gets deeper; once the head is under, they walk along the bottom
//   (slower, lower gravity - floatier jumps) with their normal walk animations. There is no swimming (no swim clips).
//   Originals are restored when they leave. Only Walking/Falling characters are affected (flight etc. untouched).
// - Underwater look: a post-process (tint, fog, light rays) applied while the camera is below the wavy surface.
// - Optional caustics: a decal projected down onto the bed inside the box.
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class RPG_CODEX_API URPGWaterZoneComponent : public UBoxComponent
{
	GENERATED_BODY()

public:
	URPGWaterZoneComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	FRPGWaveSettings Waves;

	// Current (cm/s, world) - pushes buoyant objects; 0 for still water.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	FVector FlowVelocity = FVector::ZeroVector;

	// Feet this deep (cm) start slowing a character; WadeSlowestFactor is reached when the water is at the chest.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water|Characters")
	float WadeStartDepth = 35.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water|Characters", meta = (ClampMin = "0.05", ClampMax = "1"))
	float WadeSlowestFactor = 0.6f;

	// Head under water: walking along the bottom at this fraction of normal speed, with this gravity scale.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water|Characters", meta = (ClampMin = "0.05", ClampMax = "1"))
	float UnderwaterSpeedFactor = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water|Characters", meta = (ClampMin = "0.05", ClampMax = "1"))
	float UnderwaterGravityScale = 0.35f;

	// Post-process material blended in while the camera is inside the zone (M_UnderwaterPP).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water|Look")
	TObjectPtr<UMaterialInterface> UnderwaterMaterial;

	// Deferred decal material projected onto the bed (M_WaterCaustics); receives SurfaceZ.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water|Look")
	TObjectPtr<UMaterialInterface> CausticMaterial;

	UFUNCTION(BlueprintPure, Category = "Water")
	float GetSurfaceHeight(const FVector& WorldLocation) const;

	// Water depth above WorldLocation (cm, <= 0 when above the surface or outside the zone in XY).
	UFUNCTION(BlueprintPure, Category = "Water")
	float GetImmersion(const FVector& WorldLocation) const;

	bool ContainsXY(const FVector& WorldLocation) const;

	// The zone holding WorldLocation (in XY, below the top + max wave), or null.
	static URPGWaterZoneComponent* FindZone(const UWorld* World, const FVector& WorldLocation);

	// 0 = dry .. 1 = slowest wading, 2 = head under (for tests / debug).
	UFUNCTION(BlueprintPure, Category = "Water")
	static int32 GetCharacterWaterState(const ACharacter* Character);

	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UFUNCTION()
	void OnBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep);
	UFUNCTION()
	void OnEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex);

	void Release(ACharacter* Character);

	UPROPERTY(Transient)
	TArray<TObjectPtr<ACharacter>> Characters;

	UPROPERTY(Transient)
	TObjectPtr<UPostProcessComponent> Underwater;

	UPROPERTY(Transient)
	TObjectPtr<UDecalComponent> Caustics;
};
