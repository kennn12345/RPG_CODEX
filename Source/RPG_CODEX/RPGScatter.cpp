#include "RPGScatter.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "RPGWaterZoneComponent.h"

ARPGScatter::ARPGScatter()
{
	PrimaryActorTick.bCanEverTick = false;
	Instances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Instances"));
	RootComponent = Instances;
	Instances->SetMobility(EComponentMobility::Static);
}

namespace
{
	float DistanceToPath2D(const FVector2D& P, const TArray<FVector>& Path)
	{
		float Best = TNumericLimits<float>::Max();
		for (int32 i = 0; i + 1 < Path.Num(); ++i)
		{
			const FVector2D A(Path[i]), B(Path[i + 1]), AB = B - A;
			const float T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / FMath::Max(AB.SizeSquared(), 1.f), 0.f, 1.f);
			Best = FMath::Min(Best, FVector2D::Distance(P, A + AB * T));
		}
		return Best;
	}
}

void ARPGScatter::Rebuild()
{
	Instances->ClearInstances();
	Placed = 0;
	Instances->SetStaticMesh(Mesh);
	if (Material) Instances->SetMaterial(0, Material);
	Instances->SetCollisionEnabled(bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	Instances->SetCanEverAffectNavigation(bCollision);
	if (CullDistance > 0.f) Instances->SetCullDistances(FMath::RoundToInt(CullDistance * 0.8f), FMath::RoundToInt(CullDistance));
	UWorld* World = GetWorld();
	if (!Mesh || !World || Count <= 0) return;

	FRandomStream Rng(Seed);
	FCollisionQueryParams Query(TEXT("RPGScatter"), true, this);
	const FVector Origin = GetActorLocation();
	TArray<FTransform> Out;
	Out.Reserve(Count);
	for (int32 Attempt = 0; Attempt < Count * 6 && Out.Num() < Count; ++Attempt)
	{
		const FVector2D XY(Origin.X + Rng.FRandRange(-HalfExtent.X, HalfExtent.X), Origin.Y + Rng.FRandRange(-HalfExtent.Y, HalfExtent.Y));
		if (ExcludeRadius > 0.f && ExcludePath.Num() > 1 && DistanceToPath2D(XY, ExcludePath) < ExcludeRadius) continue;
		if (ExcludeCircles.ContainsByPredicate([&XY](const FVector& C) { return FVector2D::DistSquared(XY, FVector2D(C)) < C.Z * C.Z; })) continue;
		if (PatchSize > 0.f && FMath::PerlinNoise2D(XY / PatchSize + FVector2D(Seed * 0.37f, Seed * 0.71f)) * 0.5f + 0.5f < 1.f - PatchCoverage) continue;
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, FVector(XY, Origin.Z + 50000.f), FVector(XY, Origin.Z - 50000.f), ECC_Visibility, Query)) continue;
		if (Hit.ImpactNormal.Z < MinUpNormal || Hit.ImpactPoint.Z < MinGroundZ) continue;
		if (bSkipWater)
		{
			if (Hit.GetActor() && Hit.GetActor()->FindComponentByClass<URPGWaterZoneComponent>()) continue;   // landed on a water surface
			const FVector Probe = Hit.ImpactPoint + FVector(0.f, 0.f, WaterMargin);
			const URPGWaterZoneComponent* Zone = URPGWaterZoneComponent::FindZone(World, Probe);
			if (Zone && Zone->GetImmersion(Probe) > 0.f) continue;
		}
		const FVector Up = bAlignToSurface ? FVector(Hit.ImpactNormal) : FVector::UpVector;
		const FQuat Yaw(FVector::UpVector, Rng.FRandRange(0.f, 2.f * PI));
		const FQuat Rot = FQuat::FindBetweenNormals(FVector::UpVector, Up) * Yaw;
		const float S = Rng.FRandRange(ScaleRange.X, ScaleRange.Y);
		Out.Add(FTransform(Rot, Hit.ImpactPoint - Up * SinkDepth * S, FVector(S)));
	}
	Instances->AddInstances(Out, false, /*bWorldSpace*/ true);
	Placed = Out.Num();
}
