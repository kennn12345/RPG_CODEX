#include "RPGBodyImpactComponent.h"

#include "RPGBodyMass.h"
#include "RPGMovementAbilityComponent.h"
#include "RPGNpcCharacter.h"
#include "RPGPhysicsReactionComponent.h"
#include "RPG_CODEX.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	/** GASP get-ups, by how the body lies, and where each clip's first frame lies relative to its root: pelvis X / Y
	 *  (cm) and the pelvis-to-head direction (degrees), measured from the clips. */
	struct FGetUp { const TCHAR* Path; FVector Start; };
	const FGetUp GetUpFaceDown = { TEXT("/Game/Characters/UEFN_Mannequin/Animations/Ragdoll/M_ragdoll_getup_stand_F.M_ragdoll_getup_stand_F"), FVector(2.8f, -10.7f, 84.6f) };
	const FGetUp GetUpFaceUp = { TEXT("/Game/Characters/UEFN_Mannequin/Animations/Ragdoll/M_ragdoll_getup_stand_B.M_ragdoll_getup_stand_B"), FVector(0.1f, 3.0f, -92.1f) };
	const FGetUp GetUpLeftSide = { TEXT("/Game/Characters/UEFN_Mannequin/Animations/Ragdoll/M_ragdoll_getup_stand_L.M_ragdoll_getup_stand_L"), FVector(-5.9f, -5.5f, -7.0f) };
	const FGetUp GetUpRightSide = { TEXT("/Game/Characters/UEFN_Mannequin/Animations/Ragdoll/M_ragdoll_getup_stand_R.M_ragdoll_getup_stand_R"), FVector(-4.7f, -13.6f, 160.6f) };
}

URPGBodyImpactComponent::URPGBodyImpactComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;   // after movement: LastVelocity is this frame's, read by next frame's hits
}

void URPGBodyImpactComponent::BeginPlay()
{
	Super::BeginPlay();
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		// GASP's characters let other characters pass through them; bodies collide here (and report it).
		Character->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Character->GetCapsuleComponent()->OnComponentHit.AddDynamic(this, &URPGBodyImpactComponent::OnCapsuleHit);
	}
	// NPCs stand themselves up (their own reactions); anyone else (the player) is stood up here.
	bOwnLimpHandler = !GetOwner()->IsA<ARPGNpcCharacter>();
}

URPGPhysicsReactionComponent* URPGBodyImpactComponent::GetReaction(bool bCreate)
{
	URPGPhysicsReactionComponent* Reaction = GetOwner()->FindComponentByClass<URPGPhysicsReactionComponent>();
	if (!Reaction && bCreate)
	{
		Reaction = NewObject<URPGPhysicsReactionComponent>(GetOwner(), TEXT("RPGImpactRagdoll"));
		Reaction->bHitReactions = false;   // only the whole-body ragdoll
		Reaction->RegisterComponent();
	}
	if (Reaction && !Reaction->OnLimpEnding.IsBoundToObject(this))
		Reaction->OnLimpEnding.AddUObject(this, &URPGBodyImpactComponent::HandleLimpEnding);
	return Reaction;
}

void URPGBodyImpactComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character) return;
	LastVelocity = Character->GetVelocity();
	// Down (and not an NPC, which keeps its capsule where it fell): the capsule follows the body, so the camera does.
	if (bDown && bOwnLimpHandler)
	{
		const FVector Pelvis = Character->GetMesh()->GetBoneLocation(TEXT("pelvis"));
		Character->GetCapsuleComponent()->SetWorldLocation(Pelvis + FVector(0.f, 0.f, 20.f), false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void URPGBodyImpactComponent::OnCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	ACharacter* Other = Cast<ACharacter>(OtherActor);
	URPGBodyImpactComponent* OtherImpact = Other ? Other->FindComponentByClass<URPGBodyImpactComponent>() : nullptr;
	const UWorld* World = GetWorld();
	if (!OtherImpact || bDown || OtherImpact->IsDown() || !World || World->GetTimeSeconds() - LastImpactTime < 0.3f) return;

	// Closing speed along the line between the two (the hit normal points back at us).
	const FVector Normal = -Hit.ImpactNormal.GetSafeNormal();
	const FVector Relative = LastVelocity - Other->GetVelocity();
	const float Closing = float(FVector::DotProduct(Relative, Normal));
	if (Closing < KnockDownSpeed) return;
	LastImpactTime = OtherImpact->LastImpactTime = World->GetTimeSeconds();

	// Momentum along the line of impact, with a little bounce: the struck body takes (1+e) m1/(m1+m2) of the closing
	// speed, the mover keeps (m1 - e m2)/(m1+m2) of it. Sideways motion mostly carries on.
	const float M1 = RPGBodyMass::TotalKgFor(BodyWeightLbs), M2 = RPGBodyMass::TotalKgFor(OtherImpact->BodyWeightLbs);
	const float Struck = (1.f + Restitution) * M1 / (M1 + M2) * Closing;
	const float Kept = (M1 - Restitution * M2) / (M1 + M2) * Closing;
	const FVector Sideways = Relative - Normal * Closing;
	const FVector Up(0.f, 0.f, Closing * Lift);
	OtherImpact->KnockDown(Other->GetVelocity() + Normal * Struck + Sideways * 0.3f + Up, Hit.ImpactPoint);
	UE_LOG(LogRPGInput, Log, TEXT("BODY IMPACT: %s into %s at %.0f cm/s -> struck %.0f cm/s, mover keeps %.0f cm/s%s"), *GetOwner()->GetActorNameOrLabel(),
		*Other->GetActorNameOrLabel(), Closing, Struck, Kept, Closing >= SelfKnockDownSpeed ? TEXT(", mover goes down too") : TEXT(""));

	ACharacter* Self = Cast<ACharacter>(GetOwner());
	const FVector After = LastVelocity - Normal * (Closing - Kept);
	if (Closing >= SelfKnockDownSpeed) KnockDown(After + Up * 0.5f, Hit.ImpactPoint);
	else if (Self && Self->GetCharacterMovement()) Self->GetCharacterMovement()->Velocity = After;   // carries on, slowed by the impact
}

void URPGBodyImpactComponent::KnockDown(const FVector& Velocity, const FVector& Point)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	URPGPhysicsReactionComponent* Reaction = GetReaction(true);
	if (!Character || !Reaction || bDown) return;
	bDown = true;
	// Out of the air, no walking: the body is physical now.
	if (URPGMovementAbilityComponent* Abilities = GetOwner()->FindComponentByClass<URPGMovementAbilityComponent>()) Abilities->StopFlight();
	if (UCharacterMovementComponent* Move = Character->GetCharacterMovement()) { Move->StopMovementImmediately(); Move->DisableMovement(); }
	if (bOwnLimpHandler) Character->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	// Stays limp for its flight (up and down again) and then DownTime once it settles.
	const float Gravity = 980.f * 1.6f;
	Reaction->Launch(Velocity, Point, DownTime + FMath::Max(0.f, 2.f * float(Velocity.Z) / Gravity));
	OnKnockedDown.Broadcast();
}

void URPGBodyImpactComponent::HandleLimpEnding()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || !bDown) return;
	if (!bOwnLimpHandler) { bDown = false; return; }   // an NPC stands itself up
	Character->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	float Duration = 0.f;
	StandUp(Character, 1.f, Duration);
	bDown = false;
}

UAnimMontage* URPGBodyImpactComponent::StandUp(ACharacter* Character, float Rate, float& OutDuration)
{
	OutDuration = 0.f;
	USkeletalMeshComponent* Body = Character ? Character->GetMesh() : nullptr;
	if (!Body) return nullptr;
	// How the body lies: the chest's front and the hips' left (spine and thighs) - face down, on its back, or on a side.
	const FVector Pelvis = Body->GetBoneLocation(TEXT("pelvis"));
	const FVector Left = (Body->GetBoneLocation(TEXT("thigh_l")) - Body->GetBoneLocation(TEXT("thigh_r"))).GetSafeNormal();
	const FVector Front = FVector::CrossProduct(Body->GetBoneLocation(TEXT("spine_05")) - Pelvis, Left).GetSafeNormal();
	const bool bOnSide = FMath::Abs(Left.Z) > FMath::Abs(Front.Z);
	const bool bFaceDown = !bOnSide && Front.Z < 0.f;
	const bool bLeftDown = bOnSide && Left.Z < 0.f;
	const FGetUp& Pick = bOnSide ? (bLeftDown ? GetUpLeftSide : GetUpRightSide) : (bFaceDown ? GetUpFaceDown : GetUpFaceUp);

	// Turn and place the capsule so the clip's first frame lies on the body (the fade back to animation blends into it).
	FHitResult Ground;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RPGStandUp), false, Character);
	const bool bGround = Character->GetWorld()->LineTraceSingleByChannel(Ground, Pelvis + FVector(0.f, 0.f, 80.f), Pelvis - FVector(0.f, 0.f, 300.f), ECC_Visibility, Params);
	const FVector Head = Body->GetBoneLocation(TEXT("head")) - Pelvis;
	const float MeshYaw = FMath::RadiansToDegrees(FMath::Atan2(Head.Y, Head.X)) - Pick.Start.Z;
	const FVector Offset = FRotator(0.f, MeshYaw, 0.f).RotateVector(FVector(Pick.Start.X, Pick.Start.Y, 0.f));
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float FloorZ = bGround ? float(Ground.ImpactPoint.Z) : float(Pelvis.Z) - 10.f;
	Character->SetActorLocationAndRotation(FVector(Pelvis.X - Offset.X, Pelvis.Y - Offset.Y, FloorZ + HalfHeight + 1.f),
		FRotator(0.f, MeshYaw - Body->GetRelativeRotation().Yaw, 0.f), false, nullptr, ETeleportType::None);
	if (AController* Controller = Character->GetController())
		if (Controller->IsPlayerController()) Controller->SetControlRotation(FRotator(Controller->GetControlRotation().Pitch, Character->GetActorRotation().Yaw, 0.f));
	if (UCharacterMovementComponent* Move = Character->GetCharacterMovement()) Move->SetMovementMode(MOVE_Walking);

	UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, Pick.Path);
	UAnimInstance* Anim = Body->GetAnimInstance();
	if (!Clip || !Anim) return nullptr;
	UAnimMontage* Montage = Anim->PlaySlotAnimationAsDynamicMontage(Clip, TEXT("DefaultSlot"), 0.f, 0.35f, Rate);
	OutDuration = Clip->GetPlayLength() / FMath::Max(Rate, 0.1f);
	UE_LOG(LogRPGInput, Log, TEXT("%s gets up %s (%s) at %.2fx"), *Character->GetActorNameOrLabel(),
		bOnSide ? (bLeftDown ? TEXT("from its left side") : TEXT("from its right side")) : (bFaceDown ? TEXT("from face down") : TEXT("from its back")), *Clip->GetName(), Rate);
	return Montage;
}
