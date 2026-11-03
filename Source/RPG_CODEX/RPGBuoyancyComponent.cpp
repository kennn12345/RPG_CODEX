#include "RPGBuoyancyComponent.h"

#include "Components/PrimitiveComponent.h"
#include "RPGWaterZoneComponent.h"

URPGBuoyancyComponent::URPGBuoyancyComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void URPGBuoyancyComponent::BeginPlay()
{
	Super::BeginPlay();
	UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(GetOwner() ? GetOwner()->GetRootComponent() : nullptr);
	if (Pontoons.IsEmpty() && Root)
	{
		const FBox B = Root->CalcLocalBounds().GetBox();
		// the 8 corners (pulled in a little): lift then works however the body is turned (a log lying on its side,
		// a tipped barrel) and resists rolling.
		for (int32 i = 0; i < 8; ++i)
			Pontoons.Add(B.GetCenter() + B.GetExtent() * 0.8f * FVector(i & 1 ? 1 : -1, i & 2 ? 1 : -1, i & 4 ? 1 : -1));
	}
}

void URPGBuoyancyComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(GetOwner() ? GetOwner()->GetRootComponent() : nullptr);
	bInWater = false;
	if (!Root || !Root->IsSimulatingPhysics() || Pontoons.IsEmpty()) return;

	const float Mass = Root->GetMass();
	const float G = -GetWorld()->GetGravityZ();
	const float Share = 1.f / Pontoons.Num();
	const FTransform& X = Root->GetComponentTransform();
	for (const FVector& Local : Pontoons)
	{
		const FVector P = X.TransformPosition(Local);
		const URPGWaterZoneComponent* Zone = URPGWaterZoneComponent::FindZone(GetWorld(), P);
		if (!Zone) continue;
		const float Depth = Zone->GetSurfaceHeight(P) - P.Z;
		if (Depth <= 0.f) continue;
		bInWater = true;
		const float Sub = FMath::Min(Depth / PontoonDepth, 1.f);
		const FVector V = Root->GetPhysicsLinearVelocityAtPoint(P);
		FVector F(0.f, 0.f, Mass * G * Strength * Sub * Share);
		F.Z -= V.Z * Damping.X * Mass * Sub * Share;
		const FVector2D Rel = FVector2D(Zone->FlowVelocity) - FVector2D(V);
		F.X += (Rel.X * FlowDrag - V.X * Damping.Y) * Mass * Sub * Share;
		F.Y += (Rel.Y * FlowDrag - V.Y * Damping.Y) * Mass * Sub * Share;
		Root->AddForceAtLocation(F, P);
	}
}
