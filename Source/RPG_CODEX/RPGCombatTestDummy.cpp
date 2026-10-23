#include "RPGCombatTestDummy.h"

#include "RPGAttributeSet.h"
#include "RPGCharacterStatsComponent.h"
#include "RPGCombatStateComponent.h"
#include "RPGHitCameraShake.h"
#include "RPGPhysicsReactionComponent.h"
#include "RPGPowerSettings.h"
#include "RPGPowerSystem.h"
#include "RPG_CODEX.h"
#include "AbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/BlendSpace.h"
#include "Engine/World.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "UObject/ConstructorHelpers.h"

static TAutoConsoleVariable<int32> CVarPowerDebug(
	TEXT("rpg.PowerDebug"), 1,
	TEXT("Power System readout on screen for every hit on a combat test dummy (1 = on, 0 = off). Always logged."));

static TAutoConsoleVariable<int32> CVarHitStop(
	TEXT("rpg.HitStop"), 1,
	TEXT("Hit-stop on Power System hits: attacker and target freeze for a moment on impact (1 = on, 0 = off)."));

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

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshAsset(TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin"));
	if (MeshAsset.Succeeded()) Mesh->SetSkeletalMeshAsset(MeshAsset.Object);
	static ConstructorHelpers::FObjectFinder<UPhysicsAsset> Bodies(TEXT("/Game/Characters/UEFN_Mannequin/Rigs/PA_UEFN_Mannequin.PA_UEFN_Mannequin"));
	if (Bodies.Succeeded()) Mesh->SetPhysicsAsset(Bodies.Object);
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Idle(TEXT("/Game/Characters/UEFN_Mannequin/Animations/Idle/M_Relaxed_Stand_Idle_Loop.M_Relaxed_Stand_Idle_Loop"));
	if (Idle.Succeeded()) IdleAnimation = Idle.Object;
	static ConstructorHelpers::FObjectFinder<UBlendSpace> Locomotion(TEXT("/Game/RPG/Combat/GASP/BS_GASP_Dummy_IdleWalk.BS_GASP_Dummy_IdleWalk"));
	if (Locomotion.Succeeded()) LocomotionBlendSpace = Locomotion.Object;
	GetUpFaceDown = FSoftObjectPath(TEXT("/Game/Characters/UEFN_Mannequin/Animations/Ragdoll/M_ragdoll_getup_stand_F.M_ragdoll_getup_stand_F"));
	GetUpFaceUp = FSoftObjectPath(TEXT("/Game/Characters/UEFN_Mannequin/Animations/Ragdoll/M_ragdoll_getup_stand_B.M_ragdoll_getup_stand_B"));
	GetUpLeftSide = FSoftObjectPath(TEXT("/Game/Characters/UEFN_Mannequin/Animations/Ragdoll/M_ragdoll_getup_stand_L.M_ragdoll_getup_stand_L"));
	GetUpRightSide = FSoftObjectPath(TEXT("/Game/Characters/UEFN_Mannequin/Animations/Ragdoll/M_ragdoll_getup_stand_R.M_ragdoll_getup_stand_R"));
	StaggerBackAnimation = FSoftObjectPath(TEXT("/Game/RPG/Combat/GASP/M_Dummy_StaggerBack.M_Dummy_StaggerBack"));

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
	if (PhysicsReaction) PhysicsReaction->OnLimpEnding.AddUObject(this, &ARPGCombatTestDummy::HandleLimpEnding);
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

UAnimSequence* ARPGCombatTestDummy::PickStaggerClip(const FVector& WorldPush, bool bHeavy) const
{
	// Push direction in the dummy's own frame: 0 = straight ahead, +90 = to its right, 180 = straight back.
	const FVector Local = GetActorTransform().InverseTransformVectorNoScale(FVector(WorldPush.X, WorldPush.Y, 0.f));
	if (Local.IsNearlyZero()) return nullptr;
	const float Angle = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));
	static const TCHAR* Sectors[] = { TEXT("F"), TEXT("FR"), TEXT("R"), TEXT("BR"), TEXT("B"), TEXT("BL"), TEXT("L"), TEXT("FL") };
	const int32 Sector = (FMath::RoundToInt(FMath::Fmod(Angle + 360.f, 360.f) / 45.f)) % 8;
	const bool bLeftFoot = FMath::RandBool();
	// GASP names: Stop_<direction>_<foot>; sideways stops are LL / LR and RL / RR (direction letter, then foot).
	FString Dir = Sectors[Sector];
	if (Dir == TEXT("L") || Dir == TEXT("R")) Dir += bLeftFoot ? TEXT("L") : TEXT("R");
	const TCHAR* Gait = bHeavy ? TEXT("Run") : TEXT("Walk");
	const FString Name = FString::Printf(TEXT("M_Neutral_%s_Stop_%s_%s"), Gait, *Dir, bLeftFoot ? TEXT("Lfoot") : TEXT("Rfoot"));
	const FString Path = FString::Printf(TEXT("/Game/Characters/UEFN_Mannequin/Animations/%s/%s.%s"), Gait, *Name, *Name);
	return LoadObject<UAnimSequence>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
}

void ARPGCombatTestDummy::PlayIdle()
{
	if (!Mesh) return;
	GetUpTimeLeft = 0.f;
	bStaggering = false;
	if (!GetUpTilt.IsIdentity()) Mesh->SetRelativeRotation(MeshRestRotation);   // a slope get-up cut short
	GetUpTilt = FQuat::Identity;
	Mesh->SetPlayRate(1.f);
	if (UAnimInstance* Anim = Mesh->GetAnimInstance()) Anim->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	// Wandering: the locomotion blend space, driven by speed each frame (0 = its idle). Otherwise the plain idle.
	bPlayingLocomotion = bWander && LocomotionBlendSpace;
	if (bPlayingLocomotion) Mesh->PlayAnimation(LocomotionBlendSpace, true);
	else if (IdleAnimation) Mesh->PlayAnimation(IdleAnimation, true);
}

void ARPGCombatTestDummy::PickWanderTarget()
{
	// Somewhere else within the radius, at least a short walk away, that it can walk straight to: a capsule swept
	// there (lifted a step, so slopes and steps don't count) must not hit a wall. So it never keeps picking spots behind
	// the wall or corner it just walked into.
	const FVector Here = GetActorLocation();
	const float Lift = MaxStepHeight + 2.f;
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), FMath::Max(Capsule->GetScaledCapsuleHalfHeight() - Lift, Capsule->GetScaledCapsuleRadius()));
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RPGDummyWander), false, this);
	FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
	for (int32 Try = 0; Try < 16; ++Try)
	{
		const FVector2D P = FMath::RandPointInCircle(WanderRadius);
		const FVector Candidate(Home.X + P.X, Home.Y + P.Y, Here.Z);
		if (FVector::Dist2D(Candidate, Here) < 120.f) continue;
		const FVector Up(0.f, 0.f, Lift);
		const FVector End = Here + (Candidate - Here).GetClampedToMaxSize(600.f);   // only the first few metres: slopes change Z further out
		if (GetWorld()->SweepTestByChannel(Here + Up, End + Up, FQuat::Identity, Capsule->GetCollisionObjectType(), Shape, Params, Responses)) continue;
		WanderTarget = Candidate; bHasTarget = true; return;
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

	if (GetUpTimeLeft > 0.f)
	{
		GetUpTimeLeft -= DeltaTime;
		if (!GetUpTilt.IsIdentity())
		{
			// Slope get-up: tilted with the ground at first, upright by GetUpUprightAt (turning about the feet).
			const float Done = GetUpDuration > 0.f ? 1.f - GetUpTimeLeft / GetUpDuration : 1.f;
			const FQuat Tilt = FQuat::Slerp(GetUpTilt, FQuat::Identity, FMath::SmoothStep(0.f, GetUpUprightAt, Done));
			Mesh->SetWorldRotation(Tilt * (GetActorQuat() * MeshRestRotation.Quaternion()));
			if (GetUpTimeLeft <= 0.f) { GetUpTilt = FQuat::Identity; Mesh->SetRelativeRotation(MeshRestRotation); }
		}
		if (GetUpTimeLeft <= 0.f)
		{
			// The step-back clip moved its root away from the actor: move the actor there as the idle takes over.
			const FVector RootOffset = bStaggering ? Mesh->GetComponentTransform().TransformVectorNoScale(Mesh->GetSocketTransform(TEXT("root"), RTS_Component).GetLocation()) : FVector::ZeroVector;
			bStaggering = false;
			PlayIdle();
			if (!RootOffset.IsNearlyZero())
			{
				const FVector Before = GetActorLocation();
				SetActorLocation(Before + FVector(RootOffset.X, RootOffset.Y, 0.f), true);
				ShoveOffset += GetActorLocation() - Before;
				SnapToFloor(DeltaTime);
			}
		}
	}
	const bool bBusy = GetUpTimeLeft > 0.f;   // getting up or staggering: no walking
	if (bStaggering)
		if (UAnimInstance* Anim = Mesh->GetAnimInstance())
		{
			// The step-back clip's own root motion moves the dummy, frame by frame, so the body goes where the feet step.
			const FRootMotionMovementParams Root = Anim->ConsumeExtractedRootMotion(1.f);
			if (Root.bHasRootMotion)
			{
				const FVector Move = Mesh->ConvertLocalRootMotionToWorld(Root.GetRootMotionTransform()).GetTranslation();
				const FVector Before = GetActorLocation();
				SetActorLocation(Before + FVector(Move.X, Move.Y, 0.f), true);
				ShoveOffset += GetActorLocation() - Before;
				SnapToFloor(DeltaTime);
			}
		}

	if (!bBusy && bWander != bPlayingLocomotion && (bWander ? LocomotionBlendSpace != nullptr : true))
	{
		// switched on / off at runtime (e.g. by a test): swap to the matching animation and stop where it stands
		Speed = 0.f; bHasTarget = false;
		PlayIdle();
	}
	if (!bBusy && bWander) UpdateWander(DeltaTime);

	if (!ShoveVelocity.IsNearlyZero())
	{
		// Slides back from a hit, slowing to a stop; blocked by walls and the player like its walking.
		const FVector Before = GetActorLocation();
		FHitResult Block;
		SetActorLocation(Before + ShoveVelocity * DeltaTime, true, &Block);
		ShoveOffset += GetActorLocation() - Before;
		const float Slower = FMath::Max(0.f, float(ShoveVelocity.Size()) - URPGPowerSettings::Get()->ShoveDeceleration * DeltaTime);
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

	const bool bDebugText = URPGCombatStateComponent::IsDebugDisplayEnabled();
	if (bShowHitText && (bDebugText || !DisplayLabel.IsEmpty()))
	{
		FString Text = DisplayLabel.IsEmpty() ? FString(TEXT("Combat Test Dummy")) : DisplayLabel;
		if (bDebugText && HitCount > 0)
			Text += FString::Printf(TEXT("\nHits: %d\n%s [%s]  %+.3fs"), HitCount, *LastHit.AttackName, *LastHit.HitBone.ToString(), LastHit.TimeFromImpact);
		if (const URPGAttributeSet* A = Stats ? Stats->GetAttributes() : nullptr)
			Text += FString::Printf(TEXT("\nHealth %.0f / %.0f%s"), A->GetHealth(), A->GetMaxHealth(), bKnockedOut ? TEXT("  KO") : TEXT(""));
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
	if (bUsePowerSystem && PhysicsReaction && PhysicsReaction->bReactionsEnabled)
	{
		ReceivePowerHit(Hit);
		return;
	}
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

void ARPGCombatTestDummy::ReceivePowerHit(const FRPGMeleeHitInfo& Hit)
{
	const FRPGPowerInput Input = URPGPowerSystem::MakeMeleeInput(Hit);
	FRPGPowerResult Result = URPGPowerSystem::Calculate(Input);

	// Damage channel -> Health (GAS). Reaching 0 knocks it out: it goes down whatever the hit's own reaction was.
	UAbilitySystemComponent* AbilitySystem = Stats ? Stats->GetAbilitySystem() : nullptr;
	if (AbilitySystem && Stats->GetAttributes())
	{
		const float Health = AbilitySystem->GetNumericAttribute(URPGAttributeSet::GetHealthAttribute());
		AbilitySystem->SetNumericAttributeBase(URPGAttributeSet::GetHealthAttribute(), Health - Result.Damage);
		if (Health - Result.Damage <= 0.f && !bKnockedOut)
		{
			bKnockedOut = true;
			if (Result.Reaction == ERPGPowerReaction::Flinch || Result.Reaction == ERPGPowerReaction::Stagger)
				Result.Reaction = ERPGPowerReaction::Knockdown;
		}
	}
	LastPowerResult = Result;

	// Reaction and displacement channels -> physics. A ragdoll carries its own velocity; standing, it slides back.
	PhysicsReaction->HandlePowerHit(Hit, Result);
	// Standing hits (flinch / stagger) don't slide the dummy: a stagger steps back on its own feet (below).
	ShoveVelocity = FVector::ZeroVector;
	PlayImpactFeel(Hit.Attacker, Result.Reaction);
	// Stagger: steps back (sliding with the shove) and stands dazed a moment, the body wobbling on weak motors.
	UAnimSequence* StaggerBack = nullptr;
	if (Result.Reaction == ERPGPowerReaction::Stagger)
	{
		const FVector Push = Hit.StrikeDirection.IsNearlyZero() && Hit.Attacker ? GetActorLocation() - Hit.Attacker->GetActorLocation() : Hit.StrikeDirection;
		StaggerBack = PickStaggerClip(Push, Result.Displacement >= StaggerHeavyDisplacement);
		if (!StaggerBack) StaggerBack = StaggerBackAnimation.LoadSynchronous();
	}
	if (StaggerBack && PhysicsReaction->GetReactionState() != ERPGPhysicsReactionState::Limp)
	{
		Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Mesh->PlayAnimation(StaggerBack, false);
		if (UAnimInstance* Anim = Mesh->GetAnimInstance()) Anim->SetRootMotionMode(ERootMotionMode::RootMotionFromEverything);
		bStaggering = true;
		bPlayingLocomotion = false;
		Speed = 0.f;
		bHasTarget = false;
		GetUpTimeLeft = StaggerBack->GetPlayLength() + StaggerDazedTime;
	}

	// Readout: everything the Power System worked out for this hit.
	const URPGAttributeSet* A = Stats ? Stats->GetAttributes() : nullptr;
	const FString Readout = URPGPowerSystem::DescribeResult(Input, Result)
		+ FString::Printf(TEXT("\n  %s Health %.0f / %.0f%s"), DisplayLabel.IsEmpty() ? *GetActorNameOrLabel() : *DisplayLabel,
			A ? A->GetHealth() : 0.f, A ? A->GetMaxHealth() : 0.f, bKnockedOut ? TEXT("  KNOCKED OUT") : TEXT(""));
	UE_LOG(LogRPGInput, Log, TEXT("%s"), *Readout);
	if (GEngine && CVarPowerDebug.GetValueOnGameThread() > 0)
		GEngine->AddOnScreenDebugMessage(int32(GetUniqueID()), 8.f, FColor::Yellow, Readout);   // one line block per dummy, replaced each hit
}

void ARPGCombatTestDummy::PlayImpactFeel(AActor* Attacker, ERPGPowerReaction Reaction)
{
	const URPGPowerSettings* S = URPGPowerSettings::Get();
	float StopTime = 0.f, Shake = 0.f;
	switch (Reaction)
	{
		case ERPGPowerReaction::Stagger:	StopTime = S->HitStopStagger;	Shake = S->ShakeStagger;	break;
		case ERPGPowerReaction::Knockdown:	StopTime = S->HitStopKnockdown;	Shake = S->ShakeKnockdown;	break;
		case ERPGPowerReaction::Launch:		StopTime = S->HitStopLaunch;	Shake = S->ShakeLaunch;		break;
		default: break;
	}

	// Hit-stop: both nearly freeze for a moment. The timer runs on world time, so it ends on time even though the
	// dummy and attacker themselves are slowed.
	if (StopTime > 0.f && CVarHitStop.GetValueOnGameThread() > 0)
	{
		if (AActor* Previous = HitStopAttacker.Get()) Previous->CustomTimeDilation = 1.f;
		HitStopAttacker = Attacker;
		// The dummy itself only freezes when it ragdolls: standing on reaction motors, a slowed dummy stops steering
		// its bodies while physics runs on, and it flops for that moment.
		if (PhysicsReaction->GetReactionState() != ERPGPhysicsReactionState::Powered) CustomTimeDilation = S->HitStopTimeDilation;
		if (Attacker) Attacker->CustomTimeDilation = S->HitStopTimeDilation;
		GetWorldTimerManager().SetTimer(HitStopTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			CustomTimeDilation = 1.f;
			if (AActor* Held = HitStopAttacker.Get()) Held->CustomTimeDilation = 1.f;
			HitStopAttacker = nullptr;
		}), StopTime, false);
	}

	// Camera jolt for the player who landed it.
	const APawn* Pawn = Cast<APawn>(Attacker);
	if (APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr)
		if (Shake > 0.f) Controller->ClientStartCameraShake(URPGHitCameraShake::StaticClass(), Shake);
}

void ARPGCombatTestDummy::HandleLimpEnding()
{
	// Legacy hits (bUsePowerSystem off) keep the old behaviour: the body fades back to where the dummy stood.
	if (bResetting || !Mesh || !bUsePowerSystem) return;

	// Stand up where the body lies: move the actor (capsule) under the pelvis. The ragdoll bodies stay where they are
	// in the world, and the fade back to the animation then happens at the new spot instead of snapping home.
	const FVector Pelvis = Mesh->GetBoneLocation(TEXT("pelvis"));
	float FloorZ = 0.f;
	FVector Target = Home;   // fell off the world: back to its spot
	const bool bFloor = FindFloor(FVector(Pelvis.X, Pelvis.Y, Pelvis.Z + 60.f), FloorZ);
	if (bFloor)
		Target = FVector(Pelvis.X, Pelvis.Y, FloorZ + Capsule->GetScaledCapsuleHalfHeight() + 1.f);
	UE_LOG(LogRPGInput, Log, TEXT("%s gets up: pelvis (%.0f, %.0f, %.0f), %.0f cm from where it stood, floor %s -> (%.0f, %.0f, %.0f)"),
		*GetActorNameOrLabel(), Pelvis.X, Pelvis.Y, Pelvis.Z, FVector::Dist2D(Pelvis, GetActorLocation()),
		bFloor ? *FString::Printf(TEXT("at Z %.0f"), FloorZ) : TEXT("NOT FOUND (back home)"), Target.X, Target.Y, Target.Z);

	ShoveVelocity = FVector::ZeroVector;
	FallSpeed = 0.f;
	bHasTarget = false;

	if (!bFloor)
	{
		// Fell off the world: reappear at its spot at once, instead of fading back up from wherever the body fell.
		bResetting = true;
		PhysicsReaction->ReturnToAnimated(0.f);
		bResetting = false;
		SetActorLocationAndRotation(Home, HomeRotation, false, nullptr, ETeleportType::ResetPhysics);
		ShoveOffset = FVector::ZeroVector;
	}
	else
	{
		const FVector Before = GetActorLocation();
		// How the body lies (the chest's front and the hips' left, from the spine and thighs): face down, on its back,
		// or on a side, whichever is nearest. Then turn and place the actor so that get-up clip's first frame lies where
		// the body does; the fade back to animation then blends into that frame.
		const FVector Left = (Mesh->GetBoneLocation(TEXT("thigh_l")) - Mesh->GetBoneLocation(TEXT("thigh_r"))).GetSafeNormal();
		const FVector Front = FVector::CrossProduct(Mesh->GetBoneLocation(TEXT("spine_05")) - Pelvis, Left).GetSafeNormal();
		const bool bOnSide = FMath::Abs(Left.Z) > FMath::Abs(Front.Z);
		const bool bFaceDown = !bOnSide && Front.Z < 0.f;
		const bool bLeftDown = bOnSide && Left.Z < 0.f;
		const TSoftObjectPtr<UAnimSequence>& Pick = bOnSide ? (bLeftDown ? GetUpLeftSide : GetUpRightSide) : (bFaceDown ? GetUpFaceDown : GetUpFaceUp);
		UAnimSequence* GetUp = Pick.LoadSynchronous();
		if (GetUp)
		{
			const FVector Start = bOnSide ? (bLeftDown ? GetUpLeftSideStart : GetUpRightSideStart) : (bFaceDown ? GetUpFaceDownStart : GetUpFaceUpStart);
			const FVector Head = Mesh->GetBoneLocation(TEXT("head")) - Pelvis;
			const float MeshYaw = FMath::RadiansToDegrees(FMath::Atan2(Head.Y, Head.X)) - Start.Z;
			const FVector Offset = FRotator(0.f, MeshYaw, 0.f).RotateVector(FVector(Start.X, Start.Y, 0.f));
			Target.X = Pelvis.X - Offset.X;
			Target.Y = Pelvis.Y - Offset.Y;
			SetActorLocationAndRotation(Target, FRotator(0.f, MeshYaw - Mesh->GetRelativeRotation().Yaw, 0.f), false, nullptr, ETeleportType::None);
			Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			Mesh->PlayAnimation(GetUp, false);
			// Hurt bodies get up slower.
			const URPGAttributeSet* A = Stats ? Stats->GetAttributes() : nullptr;
			const float HealthLeft = bKnockedOut || !A || A->GetMaxHealth() <= 0.f ? 0.f : FMath::Clamp(A->GetHealth() / A->GetMaxHealth(), 0.f, 1.f);
			const float Rate = FMath::Lerp(GetUpHurtRate, GetUpHealthyRate, HealthLeft);
			Mesh->SetPlayRate(Rate);
			// On a slope: start tilted to the ground under the body, straightening as it stands.
			GetUpTilt = FQuat::Identity;
			FHitResult Ground;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(RPGDummyGetUpSlope), false, this);
			if (GetWorld()->LineTraceSingleByChannel(Ground, Pelvis + FVector(0.f, 0.f, 60.f), Pelvis - FVector(0.f, 0.f, 200.f), ECC_Visibility, Params))
			{
				const FVector Normal = Ground.ImpactNormal;
				const float Slope = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Normal.Z, -1.f, 1.f)));
				if (Slope > 2.f)
					GetUpTilt = FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(FVector::UpVector, Normal), FMath::Min(1.f, GetUpMaxTilt / Slope));
			}
			bPlayingLocomotion = false;
			Speed = 0.f;
			GetUpDuration = GetUpTimeLeft = GetUp->GetPlayLength() / Rate;
			UE_LOG(LogRPGInput, Log, TEXT("%s gets up %s (%s) at %.2fx, slope tilt %.0f deg"), *GetActorNameOrLabel(),
				bOnSide ? (bLeftDown ? TEXT("from its left side") : TEXT("from its right side")) : (bFaceDown ? TEXT("from face down") : TEXT("from its back")),
				*GetUp->GetName(), Rate, FMath::RadiansToDegrees(GetUpTilt.GetAngle()));
		}
		else SetActorLocation(Target, false, nullptr, ETeleportType::None);
		ShoveOffset += GetActorLocation() - Before;
	}

	if (bKnockedOut)
	{
		SetHealthToMax();
		bKnockedOut = false;
	}
}

void ARPGCombatTestDummy::SetHealthToMax()
{
	UAbilitySystemComponent* AbilitySystem = Stats ? Stats->GetAbilitySystem() : nullptr;
	if (AbilitySystem && Stats->GetAttributes())
		AbilitySystem->SetNumericAttributeBase(URPGAttributeSet::GetHealthAttribute(),
			AbilitySystem->GetNumericAttribute(URPGAttributeSet::GetMaxHealthAttribute()));
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
	bResetting = true;
	if (PhysicsReaction && HasActorBegunPlay()) PhysicsReaction->ReturnToAnimated(0.f);
	bResetting = false;
	if (Mesh && HasActorBegunPlay()) Mesh->SetRelativeLocationAndRotation(MeshRestLocation, MeshRestRotation);
	if (HasActorBegunPlay()) SetHealthToMax();
	bKnockedOut = false;
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
