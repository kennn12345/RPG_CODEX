#include "RPGStylizedRiver.h"

#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "RPGWaterZoneComponent.h"

ARPGStylizedRiver::ARPGStylizedRiver()
{
	PrimaryActorTick.bCanEverTick = false;
	Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
	RootComponent = Spline;
}

void ARPGStylizedRiver::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Rebuild();
}

void ARPGStylizedRiver::Rebuild()
{
	// Pieces are construction-script components: the engine clears them before each rerun.
	TArray<USplineMeshComponent*> Old;
	GetComponents(Old);
	for (USplineMeshComponent* Piece : Old)
		if (Piece->CreationMethod == EComponentCreationMethod::UserConstructionScript) Piece->DestroyComponent();
	TArray<URPGWaterZoneComponent*> OldZones;
	GetComponents(OldZones);
	for (URPGWaterZoneComponent* Z : OldZones)
		if (Z->CreationMethod == EComponentCreationMethod::UserConstructionScript) Z->DestroyComponent();
	if (!WaterMesh || !Spline) return;

	const FVector MeshSize = WaterMesh->GetBoundingBox().GetSize();
	const float MeshWidth = FMath::Max(1.f, MeshSize.Y);
	const float PieceUV = MeshSize.X / 500.f;   // flow UV spans 1 per 5 m, so the next piece continues where this one ends
	// A strip lying along -X starts at its high-V end, so its V runs backwards.
	const bool bReversed = WaterMesh->GetBoundingBox().Max.X <= KINDA_SMALL_NUMBER;
	const float Length = Spline->GetSplineLength();
	const int32 Count = FMath::Max(1, FMath::RoundToInt(Length / PieceLength));
	const float Step = Length / Count;
	for (int32 i = 0; i < Count; ++i)
	{
		const float D0 = Step * i, D1 = Step * (i + 1);
		USplineMeshComponent* Piece = NewObject<USplineMeshComponent>(this, NAME_None, RF_Transactional);
		Piece->CreationMethod = EComponentCreationMethod::UserConstructionScript;
		Piece->SetMobility(EComponentMobility::Movable);
		Piece->SetStaticMesh(WaterMesh);
		if (WaterMaterial) Piece->SetMaterial(0, WaterMaterial);
		Piece->SetForwardAxis(ESplineMeshAxis::X, false);
		Piece->SetSplineUpDir(FVector::UpVector, false);
		Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Piece->SetCastShadow(false);
		// Default (saved) data: the runtime-only SetCustomPrimitiveDataFloat copy is not carried into play.
		Piece->SetDefaultCustomPrimitiveDataFloat(0, PieceUV * (bReversed ? i + 1 : i));   // flow V = data0 + data1 * UV V: no seams
		Piece->SetDefaultCustomPrimitiveDataFloat(1, bReversed ? -1.f : 1.f);
		Piece->SetupAttachment(Spline);
		const FVector P0 = Spline->GetLocationAtDistanceAlongSpline(D0, ESplineCoordinateSpace::Local);
		const FVector P1 = Spline->GetLocationAtDistanceAlongSpline(D1, ESplineCoordinateSpace::Local);
		const FVector T0 = Spline->GetTangentAtDistanceAlongSpline(D0, ESplineCoordinateSpace::Local).GetSafeNormal() * Step;
		const FVector T1 = Spline->GetTangentAtDistanceAlongSpline(D1, ESplineCoordinateSpace::Local).GetSafeNormal() * Step;
		Piece->SetStartAndEnd(P0, T0, P1, T1, false);
		Piece->SetStartScale(FVector2D(Width * Spline->GetScaleAtDistanceAlongSpline(D0).Y / MeshWidth, 1.f), false);
		Piece->SetEndScale(FVector2D(Width * Spline->GetScaleAtDistanceAlongSpline(D1).Y / MeshWidth, 1.f), false);
		Piece->UpdateMesh();
		Piece->RegisterComponent();
		AddInstanceComponent(Piece);
		// AddInstanceComponent changes this to Instance. Keep generated pieces
		// identifiable so construction reruns replace them instead of duplicating.
		Piece->CreationMethod = EComponentCreationMethod::UserConstructionScript;

		if (!bCreateWaterZones) continue;
		// Water zone for this piece: top at the water surface, current along the river.
		const FVector Along = P1 - P0;
		const float HalfWidth = 0.5f * Width * 0.5f * (Spline->GetScaleAtDistanceAlongSpline(D0).Y + Spline->GetScaleAtDistanceAlongSpline(D1).Y);
		URPGWaterZoneComponent* Zone = NewObject<URPGWaterZoneComponent>(this, NAME_None, RF_Transactional);
		Zone->CreationMethod = EComponentCreationMethod::UserConstructionScript;
		Zone->Waves = Waves;
		Zone->UnderwaterMaterial = UnderwaterMaterial;
		Zone->CausticMaterial = CausticMaterial;
		Zone->FlowVelocity = Spline->GetComponentTransform().TransformVectorNoScale(Along.GetSafeNormal()) * FlowSpeed;
		Zone->SetBoxExtent(FVector(0.5f * Along.Size() + 40.f, HalfWidth, 0.5f * ZoneDepth), false);
		Zone->SetupAttachment(Spline);
		Zone->SetRelativeLocationAndRotation(0.5f * (P0 + P1) - FVector(0.f, 0.f, 0.5f * ZoneDepth), FRotator(0.f, Along.Rotation().Yaw, 0.f));
		Zone->RegisterComponent();
		AddInstanceComponent(Zone);
		Zone->CreationMethod = EComponentCreationMethod::UserConstructionScript;
	}
}
