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
	/** All 12 GASP get-ups: how the body lies at the start (0 face down, 1 on its back, 2 left side down, 3 right side
	 *  down), what kind (stand up, crouch scramble, roll then stand, up into a run), and where the clip's first frame lies
	 *  relative to its root: pelvis X / Y (cm) and the pelvis-to-head direction (degrees) - measured from the clips. */
	enum class EGetUpKind : uint8 { Stand, Crouch, Roll, Run };
	struct FGetUp { const TCHAR* Name; int32 Lying; EGetUpKind Kind; FVector Start; };
	const FGetUp GetUps[] = {
		{ TEXT("stand_F"), 0, EGetUpKind::Stand, FVector(2.8f, -10.8f, 84.6f) },
		{ TEXT("stand_B"), 1, EGetUpKind::Stand, FVector(0.1f, 3.0f, -92.1f) },
		{ TEXT("stand_L"), 2, EGetUpKind::Stand, FVector(-5.9f, -5.5f, -7.0f) },
		{ TEXT("stand_R"), 3, EGetUpKind::Stand, FVector(-4.7f, -13.6f, 160.6f) },
		{ TEXT("crouch_F"), 0, EGetUpKind::Crouch, FVector(0.3f, -5.6f, 91.7f) },
		{ TEXT("crouch_B"), 1, EGetUpKind::Crouch, FVector(5.7f, 6.2f, -93.2f) },
		{ TEXT("crouch_L"), 2, EGetUpKind::Crouch, FVector(-7.9f, -11.8f, -4.6f) },
		{ TEXT("crouch_R"), 3, EGetUpKind::Crouch, FVector(8.5f, -15.1f, 169.2f) },
		{ TEXT("roll_L"), 0, EGetUpKind::Roll, FVector(18.0f, 0.3f, 173.8f) },
		{ TEXT("roll_R"), 0, EGetUpKind::Roll, FVector(-1.6f, 0.9f, 6.0f) },
		{ TEXT("run_F"), 0, EGetUpKind::Run, FVector(0.0f, -9.6f, 91.1f) },
		{ TEXT("run_B"), 1, EGetUpKind::Run, FVector(-0.8f, 13.7f, -88.8f) },
	};

	/** Which get-up: wanting to move -> up into a run (or the crouch scramble); otherwise a mix (face down: stand or roll
	 *  either way; any side: now and then the crouch scramble). */
	const FGetUp& PickGetUp(int32 Lying, bool bWantsToMove)
	{
		TArray<const FGetUp*> Pool;
		auto Add = [&](EGetUpKind Kind, int32 Weight) { for (const FGetUp& G : GetUps) if (G.Lying == Lying && G.Kind == Kind) for (int32 W = 0; W < Weight; ++W) Pool.Add(&G); };
		if (bWantsToMove)
		{
			Add(EGetUpKind::Run, 1);
			if (Pool.Num() == 0) Add(EGetUpKind::Crouch, 1);
		}
		if (Pool.Num() == 0)
		{
			Add(EGetUpKind::Stand, 4);
			Add(EGetUpKind::Roll, 2);   // each way
			Add(EGetUpKind::Crouch, 1);
		}
		return Pool.Num() ? *Pool[FMath::RandRange(0, Pool.Num() - 1)] : GetUps[0];
	}
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
		// No teleport flag: a physics teleport would carry the simulated body along with the capsule, which then moved the
		// hips, which moved the capsule... and threw the body off without end.
		Character->GetCapsuleComponent()->SetWorldLocation(Pelvis + FVector(0.f, 0.f, 20.f), false, nullptr, ETeleportType::None);
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
	// Holding a direction while down: get up into a run.
	StandUp(Character, 1.f, Duration, Character->GetPendingMovementInputVector().SizeSquared() > 0.04f || Character->GetLastMovementInputVector().SizeSquared() > 0.04f);
	bDown = false;
}

UAnimMontage* URPGBodyImpactComponent::StandUp(ACharacter* Character, float Rate, float& OutDuration, bool bWantsToMove)
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
	const FGetUp& Pick = PickGetUp(bOnSide ? (bLeftDown ? 2 : 3) : (bFaceDown ? 0 : 1), bWantsToMove);

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

	const FString Path = FString::Printf(TEXT("/Game/Characters/UEFN_Mannequin/Animations/Ragdoll/M_ragdoll_getup_%s.M_ragdoll_getup_%s"), Pick.Name, Pick.Name);
	UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, *Path);
	UAnimInstance* Anim = Body->GetAnimInstance();
	if (!Clip || !Anim) return nullptr;
	UAnimMontage* Montage = Anim->PlaySlotAnimationAsDynamicMontage(Clip, TEXT("DefaultSlot"), 0.f, 0.35f, Rate);
	OutDuration = Clip->GetPlayLength() / FMath::Max(Rate, 0.1f);
	UE_LOG(LogRPGInput, Log, TEXT("%s gets up %s (%s) at %.2fx"), *Character->GetActorNameOrLabel(),
		bOnSide ? (bLeftDown ? TEXT("from its left side") : TEXT("from its right side")) : (bFaceDown ? TEXT("from face down") : TEXT("from its back")), *Clip->GetName(), Rate);
	return Montage;
}
