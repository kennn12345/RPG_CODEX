#include "RPGCombatTestDummy.h"

#include "RPGCharacterStatsComponent.h"
#include "RPGCombatStateComponent.h"
#include "RPGPhysicsReactionComponent.h"
#include "RPG_CODEX.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/BlendSpace.h"
#include "Engine/World.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "UObject/ConstructorHelpers.h"

ARPGCombatTestDummy::ARPGCombatTestDummy()
{
	PrimaryActorTick.bCanEverTick = true;

	// Same footprint as the player's capsule: blocks walking into it, but ignores the camera so the boom doesn't pop.
	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(34.f, 90.f);
	Capsule->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	RootComponent = Capsule;

	// The mesh carries the hit bodies (its physics asset), found by the melee sweeps (Pawn object type). Physics
	// collision only so the bodies can simulate in a reaction: they block the world (the floor) and nothing else, so
	// they can't snag the player's capsule, the camera or the dummy's own capsule.
	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Capsule);
	Mesh->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -90.f), FRotator(0.f, -90.f, 0.f));
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionObjectType(ECC_Pawn);
	Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Mesh->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshAsset(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	if (MeshAsset.Succeeded()) Mesh->SetSkeletalMeshAsset(MeshAsset.Object);
	static ConstructorHelpers::FObjectFinder<UPhysicsAsset> Bodies(TEXT("/Game/Characters/Mannequins/Rigs/PA_Mannequin.PA_Mannequin"));
	if (Bodies.Succeeded()) Mesh->SetPhysicsAsset(Bodies.Object);
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Idle(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));
	if (Idle.Succeeded()) IdleAnimation = Idle.Object;
	static ConstructorHelpers::FObjectFinder<UBlendSpace> Locomotion(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/BS_Idle_Walk_Run.BS_Idle_Walk_Run"));
	if (Locomotion.Succeeded()) LocomotionBlendSpace = Locomotion.Object;

	PhysicsReaction = CreateDefaultSubobject<URPGPhysicsReactionComponent>(TEXT("PhysicsReaction"));

	// The Power System's standard test bandit: 180 lb, low-moderate Strength and Durability, moderate everything else,
	// minimal magic.
	Stats = CreateDefaultSubobject<URPGCharacterStatsComponent>(TEXT("Stats"));
	Stats->StartingStats.MaxHealth = 100.f;
	Stats->StartingStats.Strength = 3.f;
	Stats->StartingStats.Mass = 82.f;
	Stats->StartingStats.Durability = 40.f;
	Stats->StartingStats.Stability = 50.f;
	Stats->StartingStats.StaggerResistance = 50.f;
	Stats->StartingStats.Recovery = 50.f;
	Stats->StartingStats.MagicPower = 0.f;
	Stats->StartingStats.MagicControl = 0.f;
}

void ARPGCombatTestDummy::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	PlayIdle();   // so it stands in its idle pose in the editor too, not the T-pose
}

void ARPGCombatTestDummy::BeginPlay()
{
	Super::BeginPlay();
	MeshRestLocation = Mesh->GetRelativeLocation();
	MeshRestRotation = Mesh->GetRelativeRotation();
	Home = GetActorLocation();
	HomeRotation = GetActorRotation();
	PauseLeft = FMath::FRandRange(0.5f, PauseMax);
	PlayIdle();
	if (LocomotionBlendSpace)
	{
		// the axis that takes the speed (the mannequin's blend space is Direction x Speed)
		SpeedAxis = 0;
		for (int32 I = 0; I < 3; ++I)
			if (LocomotionBlendSpace->GetBlendParameter(I).DisplayName.Contains(TEXT("Speed"))) { SpeedAxis = I; break; }
		const FBlendParameter& Axis = LocomotionBlendSpace->GetBlendParameter(SpeedAxis);
		UE_LOG(LogRPGInput, Log, TEXT("%s: wander %s, locomotion %s axis '%s' %.0f..%.0f, walk %.0f cm/s"), *GetActorNameOrLabel(),
			bWander ? TEXT("on") : TEXT("off"), *LocomotionBlendSpace->GetName(), *Axis.DisplayName, Axis.Min, Axis.Max, WalkSpeed);
	}
}

void ARPGCombatTestDummy::PlayIdle()
{
	if (!Mesh) return;
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	// Wandering: the locomotion blend space, driven by speed each frame (0 = its idle). Otherwise the plain idle.
	bPlayingLocomotion = bWander && LocomotionBlendSpace;
	if (bPlayingLocomotion) Mesh->PlayAnimation(LocomotionBlendSpace, true);
	else if (IdleAnimation) Mesh->PlayAnimation(IdleAnimation, true);
}

void ARPGCombatTestDummy::PickWanderTarget()
{
	// Somewhere else within the radius, at least a short walk away.
	const FVector Here = GetActorLocation();
	for (int32 Try = 0; Try < 8; ++Try)
	{
		const FVector2D P = FMath::RandPointInCircle(WanderRadius);
		const FVector Candidate(Home.X + P.X, Home.Y + P.Y, Here.Z);
		if (FVector::Dist2D(Candidate, Here) >= 120.f) { WanderTarget = Candidate; bHasTarget = true; return; }
	}
	WanderTarget = FVector(Home.X, Home.Y, Here.Z);
	bHasTarget = true;
}

bool ARPGCombatTestDummy::FindFloor(const FVector& At, float& OutFloorZ) const
{
	// Whatever the capsule itself stands on (the same collision it walks with): a narrow sphere swept down from the
	// capsule's middle, so raised slabs and steps count as floor, not only the ground under them.
	const float Half = Capsule->GetScaledCapsuleHalfHeight();
	const float Radius = 12.f;
	FHitResult Floor;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RPGDummyFloor), false, this);
	FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
	if (!GetWorld()->SweepSingleByChannel(Floor, At, At - FVector(0.f, 0.f, Half + MaxStepHeight + 40.f), FQuat::Identity,
		Capsule->GetCollisionObjectType(), FCollisionShape::MakeSphere(Radius), Params, Responses) || Floor.bStartPenetrating)
		return false;
	OutFloorZ = Floor.ImpactPoint.Z;
	return true;
}

void ARPGCombatTestDummy::SnapToFloor(float DeltaTime)
{
	// Small rises and drops (up to a step) it simply follows; off anything higher it falls under gravity.
	FVector L = GetActorLocation();
	float FloorZ;
	const bool bFloor = FindFloor(L, FloorZ);
	const float Rest = bFloor ? FloorZ + Capsule->GetScaledCapsuleHalfHeight() + 1.f : -1e9f;   // 1 cm clear of the floor
	if (L.Z - Rest <= MaxStepHeight + 0.5f && FallSpeed <= 0.f)
	{
		if (bFloor) { L.Z = Rest; SetActorLocation(L, false, nullptr, ETeleportType::None); }
		return;
	}
	FallSpeed = FMath::Min(FallSpeed + 980.f * DeltaTime, 2000.f);
	L.Z = FMath::Max(Rest, L.Z - FallSpeed * DeltaTime);
	SetActorLocation(L, true);
	if (L.Z <= Rest + 0.01f) FallSpeed = 0.f;   // landed
}

bool ARPGCombatTestDummy::TryStepUp(const FVector& Move)
{
	// Blocked by something low (a slab edge, a step): if there is floor up to MaxStepHeight higher just ahead, step onto it.
	const FVector Ahead = GetActorLocation() + Move.GetSafeNormal2D() * (Capsule->GetScaledCapsuleRadius() + 6.f);
	float FloorZ;
	const float Bottom = GetActorLocation().Z - Capsule->GetScaledCapsuleHalfHeight();
	if (!FindFloor(Ahead + FVector(0.f, 0.f, MaxStepHeight), FloorZ) || FloorZ - Bottom > MaxStepHeight || FloorZ - Bottom < 0.5f) return false;
	const FVector Up = GetActorLocation() + FVector(0.f, 0.f, FloorZ - Bottom + 1.f);
	if (!SetActorLocation(Up, true)) return false;
	FHitResult Block;
	SetActorLocation(GetActorLocation() + Move, true, &Block);
	return !Block.bBlockingHit;
}

void ARPGCombatTestDummy::UpdateWander(float DeltaTime)
{
	// Stand still while reacting to a hit (and a moment after), or while paused at a spot.
	const bool bReacting = PhysicsReaction && PhysicsReaction->GetReactionState() != ERPGPhysicsReactionState::Animated;
	float Want = 0.f;
	if (bReacting) PauseLeft = FMath::Max(PauseLeft, HitPause);
	else if (PauseLeft > 0.f) PauseLeft -= DeltaTime;
	else
	{
		if (!bHasTarget) PickWanderTarget();
		const FVector ToTarget = (WanderTarget - GetActorLocation()) * FVector(1.f, 1.f, 0.f);
		const float Dist = ToTarget.Size();
		// Brake to arrive on the spot: the speed it can still stop from.
		const float StopSpeed = FMath::Sqrt(2.f * WalkAcceleration * FMath::Max(Dist - 5.f, 0.f));
		Want = Dist < 10.f ? 0.f : FMath::Min(WalkSpeed, StopSpeed);
		// Turn toward it; walk only roughly facing it (turns on the spot first if it's well off to the side).
		const float TargetYaw = ToTarget.Rotation().Yaw;
		const float Yaw = GetActorRotation().Yaw;
		const float Delta = FRotator::NormalizeAxis(TargetYaw - Yaw);
		const float Step = FMath::Clamp(Delta, -TurnRate * DeltaTime, TurnRate * DeltaTime);
		if (Dist >= 10.f) SetActorRotation(FRotator(0.f, Yaw + Step, 0.f));
		if (FMath::Abs(Delta) > 60.f) Want = FMath::Min(Want, 30.f);
		if (Dist < 10.f && Speed < 5.f)
		{
			bHasTarget = false;
			PauseLeft = FMath::FRandRange(PauseMin, FMath::Max(PauseMin, PauseMax));
		}
	}
	Speed = FMath::FInterpConstantTo(Speed, Want, DeltaTime, WalkAcceleration * (Want < Speed ? 1.5f : 1.f));

	if (Speed > 0.5f)
	{
		FHitResult Block;
		const FVector Move = GetActorForwardVector() * Speed * DeltaTime;
		SetActorLocation(GetActorLocation() + Move, true, &Block);
		if (Block.bBlockingHit && !TryStepUp(Move * (1.f - Block.Time)))   // only the part of the step not yet taken
		{
			// Walked into something (a wall, the player): stop, pause briefly, then head somewhere else.
			Speed = 0.f; bHasTarget = false; PauseLeft = FMath::FRandRange(0.5f, 1.2f);
		}
		SnapToFloor(DeltaTime);
	}
	if (Speed <= 0.5f && FallSpeed > 0.f) SnapToFloor(DeltaTime);   // still dropping after stopping at an edge
	if (bPlayingLocomotion)
		if (UAnimSingleNodeInstance* Single = Mesh->GetSingleNodeInstance())
		{
			FVector Position = FVector::ZeroVector;   // direction 0 = straight ahead
			Position[SpeedAxis] = Speed;
			Single->SetBlendSpacePosition(Position);
		}
}

void ARPGCombatTestDummy::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bWander != bPlayingLocomotion && (bWander ? LocomotionBlendSpace != nullptr : true))
	{
		// switched on / off at runtime (e.g. by a test): swap to the matching animation and stop where it stands
		Speed = 0.f; bHasTarget = false;
		PlayIdle();
	}
	if (bWander) UpdateWander(DeltaTime);

	if (!ShoveVelocity.IsNearlyZero())
	{
		// Slides back from a hit, slowing to a stop; blocked by walls and the player like its walking.
		const FVector Before = GetActorLocation();
		FHitResult Block;
		SetActorLocation(Before + ShoveVelocity * DeltaTime, true, &Block);
		ShoveOffset += GetActorLocation() - Before;
		const float Slower = FMath::Max(0.f, float(ShoveVelocity.Size()) - ShoveDeceleration * DeltaTime);
		ShoveVelocity = Block.bBlockingHit ? FVector::ZeroVector : ShoveVelocity.GetSafeNormal() * Slower;
	}

	if (FlinchElapsed >= 0.f)
	{
		FlinchElapsed += DeltaTime;
		const float Alpha = FMath::Clamp(FlinchElapsed / FlinchTime, 0.f, 1.f);
		// Quick tip away from the hit, eased back to rest; pivots at the feet (the mesh origin).
		const float Degrees = FlinchDegrees * FMath::Sin(PI * FMath::Sqrt(Alpha));
		const FQuat Rest = GetActorQuat() * MeshRestRotation.Quaternion();
		Mesh->SetWorldRotation(FQuat(FlinchAxis, FMath::DegreesToRadians(Degrees)) * Rest);
		if (Alpha >= 1.f)
		{
			Mesh->SetRelativeLocationAndRotation(MeshRestLocation, MeshRestRotation);
			FlinchElapsed = -1.f;
		}
	}

	if (bShowHitText && URPGCombatStateComponent::IsDebugDisplayEnabled())
	{
		const FString Text = HitCount > 0
			? FString::Printf(TEXT("Hits: %d\n%s [%s]  %+.3fs"), HitCount, *LastHit.AttackName, *LastHit.HitBone.ToString(), LastHit.TimeFromImpact)
			: FString(TEXT("Combat Test Dummy"));
		DrawDebugString(GetWorld(), FVector(0.f, 0.f, 115.f), Text, this, FColor::White, 0.f, true, 1.1f);
	}
}

void ARPGCombatTestDummy::ReceiveMeleeHit_Implementation(const FRPGMeleeHitInfo& Hit)
{
	++HitCount;
	LastHit = Hit;
	// stop walking: it stands to take the hit and only moves on HitPause after the reaction ends
	Speed = 0.f;
	PauseLeft = FMath::Max(PauseLeft, HitPause);
	if (PhysicsReaction && PhysicsReaction->bReactionsEnabled)
	{
		PhysicsReaction->HandleMeleeHit(Hit);
		// Its balance: driven back a little along the strike, harder for a stronger hit (a ragdoll just falls).
		const FVector Push = Hit.StrikeDirection.GetSafeNormal2D();
		if (PhysicsReaction->GetReactionState() == ERPGPhysicsReactionState::Powered && !Push.IsNearlyZero())
			ShoveVelocity = Push * FMath::Min(ShoveSpeed * PhysicsReaction->GetStrikeStrength(Hit), MaxShoveSpeed);
		return;
	}
	if (!bFlinchOnHit) return;
	FVector Push = Hit.StrikeDirection.GetSafeNormal2D();
	if (Push.IsNearlyZero() && Hit.Attacker) Push = (GetActorLocation() - Hit.Attacker->GetActorLocation()).GetSafeNormal2D();
	if (Push.IsNearlyZero()) return;
	FlinchAxis = FVector::CrossProduct(FVector::UpVector, Push).GetSafeNormal();
	FlinchElapsed = 0.f;
}

void ARPGCombatTestDummy::ReceiveContactPush_Implementation(const FVector& PushVelocity, FName Bone)
{
	// Only while it stands and reacts (not a ragdoll), horizontally, and never slower than it is already sliding.
	if (!PhysicsReaction || PhysicsReaction->GetReactionState() == ERPGPhysicsReactionState::Limp) return;
	const FVector Push = FVector(PushVelocity.X, PushVelocity.Y, 0.f) * ContactPushShare;
	const FVector Dir = Push.GetSafeNormal();
	if (Dir.IsNearlyZero()) return;
	const float Along = float(FVector::DotProduct(ShoveVelocity, Dir));
	if (Push.Size() > Along) ShoveVelocity += Dir * (FMath::Min(float(Push.Size()), MaxShoveSpeed) - FMath::Max(Along, 0.f));
	Speed = 0.f;
}

void ARPGCombatTestDummy::ResetDummy()
{
	HitCount = 0;
	LastHit = FRPGMeleeHitInfo();
	FlinchElapsed = -1.f;
	ShoveVelocity = FVector::ZeroVector;
	if (!ShoveOffset.IsNearlyZero() && HasActorBegunPlay()) SetActorLocation(GetActorLocation() - ShoveOffset);
	ShoveOffset = FVector::ZeroVector;
	if (PhysicsReaction && HasActorBegunPlay()) PhysicsReaction->ReturnToAnimated(0.f);
	if (Mesh && HasActorBegunPlay()) Mesh->SetRelativeLocationAndRotation(MeshRestLocation, MeshRestRotation);
}

void ARPGCombatTestDummy::StopWanderingAtHome()
{
	bWander = false;
	Speed = 0.f; bHasTarget = false; PauseLeft = 0.f;
	ShoveVelocity = FVector::ZeroVector; ShoveOffset = FVector::ZeroVector;
	if (!HasActorBegunPlay()) return;
	SetActorLocationAndRotation(Home, HomeRotation, false, nullptr, ETeleportType::ResetPhysics);
	PlayIdle();
}
