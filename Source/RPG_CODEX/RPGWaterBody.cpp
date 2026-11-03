#include "RPGWaterBody.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "RPGWaterZoneComponent.h"

ARPGWaterBody::ARPGWaterBody()
{
	PrimaryActorTick.bCanEverTick = false;
	Zone = CreateDefaultSubobject<URPGWaterZoneComponent>(TEXT("Zone"));
	RootComponent = Zone;
	Surface = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Surface"));
	Surface->SetupAttachment(Zone);
	Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);   // characters can enter the water
	Surface->SetCastShadow(false);
	Surface->SetCanEverAffectNavigation(false);
}

void ARPGWaterBody::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Surface->SetStaticMesh(WaterMesh);
	if (WaterMaterial) Surface->SetMaterial(0, WaterMaterial);
	Surface->SetRelativeLocation(FVector(0.f, 0.f, Zone->GetUnscaledBoxExtent().Z));
	Surface->SetRelativeScale3D(SurfaceScale);
}
