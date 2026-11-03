#include "RPGCharacterContactComponent.h"
#include "RPGBodyImpactComponent.h"
#include "RPGPhysicsReactionComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/UnrealType.h"

namespace
{
	bool ApproachingFlightImpact(const ACharacter* A, const ACharacter* B)
	{
		const auto* MA = A->GetCharacterMovement();
		const auto* MB = B->GetCharacterMovement();
		if (!(MA && MA->IsFlying()) && !(MB && MB->IsFlying())) return false;
		const auto* Impact = A->FindComponentByClass<URPGBodyImpactComponent>();
		if (!Impact) Impact = B->FindComponentByClass<URPGBodyImpactComponent>();
		return Impact && FVector::DotProduct(A->GetVelocity() - B->GetVelocity(), (B->GetActorLocation() - A->GetActorLocation()).GetSafeNormal()) >= Impact->MinBumpSpeed;
	}

	bool CapsulesClear(const ACharacter* A, const ACharacter* B)
	{
		const UCapsuleComponent* CA = A->GetCapsuleComponent();
		const UCapsuleComponent* CB = B->GetCapsuleComponent();
		const FVector D = CA->GetComponentLocation() - CB->GetComponentLocation();
		const float RA = CA->GetScaledCapsuleRadius(), RB = CB->GetScaledCapsuleRadius();
		const float SegmentSum = CA->GetScaledCapsuleHalfHeight() - RA + CB->GetScaledCapsuleHalfHeight() - RB;
		const double Z = FMath::Max(0., FMath::Abs(D.Z) - SegmentSum);
		return D.SizeSquared2D() + Z * Z > FMath::Square(RA + RB + 3.f);
	}
}

URPGCharacterContactComponent::URPGCharacterContactComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void URPGCharacterContactComponent::BeginPlay()
{
	Super::BeginPlay();
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->GetCapsuleComponent()->SetCollisionResponseToChannel(GroundChannel, ECR_Ignore);
		Character->GetCapsuleComponent()->CanCharacterStepUpOn = ECB_No;
		Character->GetMesh()->SetCollisionResponseToChannel(GroundChannel, ECR_Ignore);
		Character->GetMesh()->CanCharacterStepUpOn = ECB_No;
		Character->GetMesh()->AddTickPrerequisiteComponent(this);
		ConfigureGroundQueries(Character);
	}
}

bool URPGCharacterContactComponent::IsRecovering(const AActor* Actor)
{
	if (!Actor) return false;
	if (const auto* Body = Actor->FindComponentByClass<URPGBodyImpactComponent>(); Body && Body->IsRecovering()) return true;
	const auto* Physics = Actor->FindComponentByClass<URPGPhysicsReactionComponent>();
	return Physics && (Physics->GetReactionState() == ERPGPhysicsReactionState::Limp || Physics->IsReturning());
}

void URPGCharacterContactComponent::ConfigureGroundQueries(ACharacter* Character)
{
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	TArray<UAnimInstance*> Instances;
	if (Mesh->GetAnimInstance()) Instances.Add(Mesh->GetAnimInstance());
	for (UAnimInstance* Instance : static_cast<const USkeletalMeshComponent*>(Mesh)->GetLinkedAnimInstances()) Instances.AddUnique(Instance);
	if (Mesh->GetPostProcessInstance()) Instances.AddUnique(Mesh->GetPostProcessInstance());
	for (UAnimInstance* Instance : Instances)
	{
		if (!Instance || ConfiguredInstances.Contains(Instance)) continue;
		// Change live node settings only after the preceding pose evaluation has completed.
		Mesh->HandleExistingParallelEvaluationTask(true, true);
		const uint8 Channel = uint8(UEngineTypes::ConvertToTraceType(GroundChannel));
		for (TFieldIterator<FStructProperty> It(Instance->GetClass()); It; ++It)
		{
			if (It->Struct->GetFName() != TEXT("AnimNode_FootPlacement")) continue;
			const FStructProperty* Settings = FindFProperty<FStructProperty>(It->Struct, TEXT("TraceSettings"));
			if (!Settings) continue;
			void* Node = It->ContainerPtrToValuePtr<void>(Instance);
			void* Trace = Settings->ContainerPtrToValuePtr<void>(Node);
			bool bConfigured = true;
			for (const TCHAR* Name : {TEXT("SimpleTraceChannel"), TEXT("ComplexTraceChannel")})
			{
				const FByteProperty* Property = FindFProperty<FByteProperty>(Settings->Struct, Name);
				if (Property) Property->SetPropertyValue_InContainer(Trace, Channel);
				else bConfigured = false;
			}
			if (bConfigured) ++ConfiguredFootNodes;
		}
		ConfiguredInstances.Add(Instance);
	}
	for (auto It = ConfiguredInstances.CreateIterator(); It; ++It) if (!It->IsValid()) It.RemoveCurrent();
}

void URPGCharacterContactComponent::ReleasePair(ACharacter* Character, const FContactPair& Pair)
{
	ACharacter* Other = Pair.Other.Get();
	if (!Other || !Character) return;
	if (Pair.bAddedOwnIgnore) Character->GetCapsuleComponent()->IgnoreActorWhenMoving(Other, false);
	if (Pair.bAddedOtherIgnore) Other->GetCapsuleComponent()->IgnoreActorWhenMoving(Character, false);
}

void URPGCharacterContactComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	for (const FContactPair& Pair : Pairs) ReleasePair(Cast<ACharacter>(GetOwner()), Pair);
	Pairs.Reset();
	Super::EndPlay(Reason);
}

void URPGCharacterContactComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Tick)
{
	Super::TickComponent(DeltaTime, TickType, Tick);
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || !GetWorld()) return;
	ConfigureGroundQueries(Character);
	for (int32 I = Pairs.Num() - 1; I >= 0; --I)
	{
		ACharacter* Other = Pairs[I].Other.Get();
		if (!Other || (CapsulesClear(Character, Other) && ((!IsRecovering(Character) && !IsRecovering(Other)) || ApproachingFlightImpact(Character, Other))))
		{
			ReleasePair(Character, Pairs[I]);
			Pairs.RemoveAtSwap(I);
		}
	}
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn); Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RPGRecoveryContact), false, Character);
	TArray<FOverlapResult> Nearby;
	const FVector Center = IsRecovering(Character) ? Character->GetMesh()->GetBoneLocation(TEXT("pelvis")) : Character->GetActorLocation();
	GetWorld()->OverlapMultiByObjectType(Nearby, Center, FQuat::Identity, Objects, FCollisionShape::MakeSphere(250.f), Params);
	for (const FOverlapResult& Hit : Nearby)
	{
		ACharacter* Other = Cast<ACharacter>(Hit.GetActor());
		if (!Other || Other == Character || (!IsRecovering(Character) && !IsRecovering(Other))) continue;
		// Passive recovery protection must not swallow the game's deliberate flying body-impact route.
		if (ApproachingFlightImpact(Character, Other) && CapsulesClear(Character, Other)) continue;
		// Exactly one component owns each pair and restores only the ignore entries it added.
		if (Other->FindComponentByClass<URPGCharacterContactComponent>() && Other->GetUniqueID() < Character->GetUniqueID()) continue;
		if (Pairs.ContainsByPredicate([Other](const FContactPair& Pair) { return Pair.Other.Get() == Other; })) continue;
		FContactPair& Pair = Pairs.AddDefaulted_GetRef(); Pair.Other = Other;
		Pair.bAddedOwnIgnore = !Character->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(Other);
		Pair.bAddedOtherIgnore = !Other->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(Character);
		if (Pair.bAddedOwnIgnore) Character->GetCapsuleComponent()->IgnoreActorWhenMoving(Other, true);
		if (Pair.bAddedOtherIgnore) Other->GetCapsuleComponent()->IgnoreActorWhenMoving(Character, true);
	}
}
