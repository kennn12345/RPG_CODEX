#include "RPGWaterZoneComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/DecalComponent.h"
#include "Components/PostProcessComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	TArray<TWeakObjectPtr<URPGWaterZoneComponent>> GZones;

	// One entry per character currently inside any zone (zones can overlap, e.g. river pieces).
	struct FWaterCharacterState { int32 Zones = 0; float Gravity = 1.f; int32 State = 0; };
	TMap<TWeakObjectPtr<ACharacter>, FWaterCharacterState> GCharacters;
}

URPGWaterZoneComponent::URPGWaterZoneComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SetCollisionObjectType(ECC_WorldDynamic);
	SetCollisionResponseToAllChannels(ECR_Overlap);
	SetGenerateOverlapEvents(true);
	SetCanEverAffectNavigation(false);
	SetHiddenInGame(true);
	ShapeColor = FColor(40, 170, 220);
	BoxExtent = FVector(500.f, 500.f, 150.f);
}

void URPGWaterZoneComponent::OnRegister()
{
	Super::OnRegister();
	GZones.AddUnique(this);
	OnComponentBeginOverlap.AddUniqueDynamic(this, &URPGWaterZoneComponent::OnBeginOverlap);
	OnComponentEndOverlap.AddUniqueDynamic(this, &URPGWaterZoneComponent::OnEndOverlap);

	AActor* Owner = GetOwner();
	if (!Owner) return;
	if (!Underwater && UnderwaterMaterial)
	{
		// Uses this box as its volume (a post-process component takes its parent shape's bounds).
		Underwater = NewObject<UPostProcessComponent>(Owner, NAME_None, RF_Transient);
		Underwater->SetupAttachment(this);
		Underwater->bUnbound = false;
		Underwater->Priority = 10.f;
		Underwater->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, UnderwaterMaterial));
		Underwater->RegisterComponent();
	}
	if (!Caustics && CausticMaterial)
	{
		const FVector Ext = GetScaledBoxExtent();
		Caustics = NewObject<UDecalComponent>(Owner, NAME_None, RF_Transient);
		Caustics->SetupAttachment(this);
		Caustics->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));   // project straight down
		Caustics->DecalSize = FVector(Ext.Z, BoxExtent.Y, BoxExtent.X);
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(CausticMaterial, this);
		Mid->SetScalarParameterValue(TEXT("SurfaceZ"), GetComponentLocation().Z + Ext.Z);
		Caustics->SetDecalMaterial(Mid);
		Caustics->RegisterComponent();
	}
}

void URPGWaterZoneComponent::OnUnregister()
{
	for (int32 i = Characters.Num() - 1; i >= 0; --i) Release(Characters[i]);
	Characters.Reset();
	GZones.Remove(this);
	Super::OnUnregister();
}

float URPGWaterZoneComponent::GetSurfaceHeight(const FVector& WorldLocation) const
{
	const float Top = GetComponentLocation().Z + GetScaledBoxExtent().Z;
	const UWorld* World = GetWorld();
	return Top + RPGWaterWaves::Height(Waves, FVector2D(WorldLocation), World ? World->GetTimeSeconds() : 0.f);
}

bool URPGWaterZoneComponent::ContainsXY(const FVector& WorldLocation) const
{
	const FVector Local = GetComponentTransform().InverseTransformPosition(WorldLocation);
	return FMath::Abs(Local.X) <= BoxExtent.X && FMath::Abs(Local.Y) <= BoxExtent.Y;
}

float URPGWaterZoneComponent::GetImmersion(const FVector& WorldLocation) const
{
	return ContainsXY(WorldLocation) ? GetSurfaceHeight(WorldLocation) - WorldLocation.Z : 0.f;
}

URPGWaterZoneComponent* URPGWaterZoneComponent::FindZone(const UWorld* World, const FVector& WorldLocation)
{
	for (const TWeakObjectPtr<URPGWaterZoneComponent>& Weak : GZones)
	{
		URPGWaterZoneComponent* Zone = Weak.Get();
		if (!Zone || Zone->GetWorld() != World || !Zone->ContainsXY(WorldLocation)) continue;
		const float Top = Zone->GetComponentLocation().Z + Zone->GetScaledBoxExtent().Z;
		const float Bottom = Top - 2.f * Zone->GetScaledBoxExtent().Z;
		if (WorldLocation.Z >= Bottom && WorldLocation.Z <= Top + 2.2f * Zone->Waves.Height + 50.f) return Zone;
	}
	return nullptr;
}

int32 URPGWaterZoneComponent::GetCharacterWaterState(const ACharacter* Character)
{
	const FWaterCharacterState* S = GCharacters.Find(const_cast<ACharacter*>(Character));
	return S ? S->State : 0;
}

void URPGWaterZoneComponent::OnBeginOverlap(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent* OtherComp, int32, bool, const FHitResult&)
{
	ACharacter* Character = Cast<ACharacter>(Other);
	if (!Character || OtherComp != Character->GetCapsuleComponent() || Characters.Contains(Character)) return;
	UCharacterMovementComponent* Move = Character->GetCharacterMovement();
	if (!Move) return;
	FWaterCharacterState& S = GCharacters.FindOrAdd(Character);
	if (S.Zones++ == 0) S.Gravity = Move->GravityScale;
	Move->AddTickPrerequisiteComponent(this);   // our speed cap runs before the character moves
	Characters.Add(Character);
}

void URPGWaterZoneComponent::OnEndOverlap(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent* OtherComp, int32)
{
	ACharacter* Character = Cast<ACharacter>(Other);
	if (Character && OtherComp == Character->GetCapsuleComponent()) Release(Character);
}

void URPGWaterZoneComponent::Release(ACharacter* Character)
{
	if (!Characters.Remove(Character) || !IsValid(Character)) return;
	UCharacterMovementComponent* Move = Character->GetCharacterMovement();
	if (Move) Move->RemoveTickPrerequisiteComponent(this);
	FWaterCharacterState* S = GCharacters.Find(Character);
	if (S && --S->Zones <= 0)
	{
		if (Move) Move->GravityScale = S->Gravity;
		GCharacters.Remove(Character);
	}
}

void URPGWaterZoneComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	for (int32 i = Characters.Num() - 1; i >= 0; --i)
	{
		ACharacter* Character = Characters[i];
		if (!IsValid(Character)) { Characters.RemoveAt(i); continue; }
		UCharacterMovementComponent* Move = Character->GetCharacterMovement();
		FWaterCharacterState* S = GCharacters.Find(Character);
		if (!Move || !S) continue;
		if (!Move->IsMovingOnGround() && !Move->IsFalling()) continue;   // flying etc: leave alone

		const float Half = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FVector Feet = Character->GetActorLocation() - FVector(0.f, 0.f, Half);
		const float Depth = GetImmersion(Feet);
		if (Depth <= 0.f && !ContainsXY(Feet)) continue;   // another zone may own this spot
		const float Body = 2.f * Half;
		float Factor = 1.f;
		int32 State = 0;
		if (Depth > Body * 0.92f)
		{
			Factor = UnderwaterSpeedFactor;
			State = 2;
		}
		else if (Depth > WadeStartDepth)
		{
			const float T = FMath::Clamp((Depth - WadeStartDepth) / FMath::Max(Body * 0.7f - WadeStartDepth, 1.f), 0.f, 1.f);
			Factor = FMath::Lerp(1.f, WadeSlowestFactor, T);
			State = 1;
		}
		S->State = State;
		Move->GravityScale = State == 2 ? S->Gravity * UnderwaterGravityScale : S->Gravity;

		// Cap horizontal speed (works with sprint / GASP gaits: it scales whatever the current max speed is).
		const float Cap = Move->GetMaxSpeed() * Factor;
		const FVector2D Horizontal(Move->Velocity);
		if (Factor < 1.f && Horizontal.Size() > Cap)
		{
			const FVector2D Scaled = Horizontal.GetSafeNormal() * Cap;
			Move->Velocity.X = Scaled.X;
			Move->Velocity.Y = Scaled.Y;
		}
	}
}
