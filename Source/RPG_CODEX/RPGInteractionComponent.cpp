#include "RPGInteractionComponent.h"

#include "RPGInteractable.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

AActor* URPGInteractionComponent::FindBestInteractable() const
{
	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return nullptr;
	}

	const FVector Origin = Owner->GetActorLocation();

	// "In front" means in front of the camera, so it works in both first and third person.
	FVector ViewForward = Owner->GetActorForwardVector();
	if (const APawn* Pawn = Cast<APawn>(Owner))
	{
		if (Pawn->GetController())
		{
			ViewForward = Pawn->GetControlRotation().Vector();
		}
	}
	ViewForward = ViewForward.GetSafeNormal2D();

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RPGFindInteractable), false, Owner);
	World->OverlapMultiByObjectType(Overlaps, Origin, FQuat::Identity,
		FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects),
		FCollisionShape::MakeSphere(InteractRange), Params);

	AActor* BestActor = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Candidate = Overlap.GetActor();
		if (!Candidate || !Candidate->Implements<URPGInteractable>())
		{
			continue;
		}

		if (!IRPGInteractable::Execute_CanInteract(Candidate, const_cast<AActor*>(Owner)))
		{
			continue;
		}

		const FVector ToCandidate = Candidate->GetActorLocation() - Origin;
		if (FVector::DotProduct(ToCandidate.GetSafeNormal2D(), ViewForward) < MinFacingDot)
		{
			continue;
		}

		const float DistanceSq = ToCandidate.SizeSquared();
		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			BestActor = Candidate;
		}
	}

	return BestActor;
}
