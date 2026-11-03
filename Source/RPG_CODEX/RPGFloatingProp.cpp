#include "RPGFloatingProp.h"

#include "Components/StaticMeshComponent.h"
#include "RPGBuoyancyComponent.h"

ARPGFloatingProp::ARPGFloatingProp()
{
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetSimulatePhysics(true);
	Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	Mesh->SetLinearDamping(0.1f);
	Mesh->SetAngularDamping(0.8f);
	Buoyancy = CreateDefaultSubobject<URPGBuoyancyComponent>(TEXT("Buoyancy"));
}
