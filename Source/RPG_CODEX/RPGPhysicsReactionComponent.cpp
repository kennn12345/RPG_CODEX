#include "RPGPhysicsReactionComponent.h"

#include "RPG_CODEX.h"
#include "RPGCombatStateComponent.h"
#include "RPGPowerSettings.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "PhysicsControlComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"

namespace
{
	TAutoConsoleVariable<int32> CVarPhysicsReactionDebug(
		TEXT("rpg.PhysicsReactionDebug"), -1,
		TEXT("Physics hit reaction debug drawing: -1 = with rpg.Debug (default), 0 = off, 1 = on (when the component's bDrawDebug is also on)."));

	/** Leaf to root, as PhysicsControl wants: each limb takes its start bone's bodies not already in an earlier limb. */
	TArray<FPhysicsControlLimbSetupData> MannequinLimbs()
	{
		auto Limb = [](const TCHAR* Name, const TCHAR* Start, bool bIncludeParent = false)
		{
			FPhysicsControlLimbSetupData Data;
			Data.LimbName = Name;
			Data.StartBone = Start;
			Data.bIncludeParentBone = bIncludeParent;
			Data.bCreateWorldSpaceControls = false;
			return Data;
		};
		return {
			Limb(TEXT("ArmLeft"), TEXT("clavicle_l")),
			Limb(TEXT("ArmRight"), TEXT("clavicle_r")),
			Limb(TEXT("LegLeft"), TEXT("thigh_l")),
			Limb(TEXT("LegRight"), TEXT("thigh_r")),
			Limb(TEXT("Head"), TEXT("neck_01")),
			Limb(TEXT("Spine"), TEXT("spine_01"), true),	// with the pelvis
		};
	}

	const TCHAR* StateName(ERPGPhysicsReactionState State)
	{
		switch (State)
		{
		case ERPGPhysicsReactionState::Powered:	return TEXT("POWERED");
		case ERPGPhysicsReactionState::Limp:	return TEXT("LIMP");
		default:								return TEXT("ANIMATED");
		}
	}

	/** Blending into physics from part-way through a return (a hit landing while the pose fades back). */
	constexpr float BlendInTime = 0.06f;
	/** Frames an impulse may wait for its body to start simulating before it is dropped. */
	constexpr int32 MaxPendingFrames = 4;
}

URPGPhysicsReactionComponent::URPGPhysicsReactionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Pre-physics, after the PhysicsControl component (a tick prerequisite set in BeginPlay) has applied this frame's
	// body modifiers, so a queued impulse lands on a body that is already simulating.
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

USkeletalMeshComponent* URPGPhysicsReactionComponent::GetMesh() const
{
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner())) return Character->GetMesh();
	return GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}

void URPGPhysicsReactionComponent::BeginPlay()
{
	Super::BeginPlay();
	bSetUp = SetUpControls();
	if (!bSetUp)
		UE_LOG(LogRPGInput, Warning, TEXT("Physics reaction on %s: no skeletal mesh with a physics asset, reactions are off."), *GetOwner()->GetActorNameOrLabel());
}

bool URPGPhysicsReactionComponent::SetUpControls()
{
	USkeletalMeshComponent* Mesh = GetMesh();
	if (!Mesh || !Mesh->GetPhysicsAsset()) return false;

	// Bodies can only simulate with physics collision on; the owner's collision responses stay as they are.
	if (!CollisionEnabledHasPhysics(Mesh->GetCollisionEnabled()))
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	// The mesh stays where its owner puts it (on the capsule) instead of following the ragdoll's pelvis; the simulated
	// bodies are in world space and reach the pose through the physics blend. So a ragdoll can fade back to the
	// animation in place. (Moving the owner to where the ragdoll lies is a job for the later get-up work.)
	Mesh->PhysicsTransformUpdateMode = EPhysicsTransformUpdateMode::ComponentTransformIsKinematic;

	PhysicsControl = GetOwner()->FindComponentByClass<UPhysicsControlComponent>();
	if (!PhysicsControl)
	{
		PhysicsControl = NewObject<UPhysicsControlComponent>(GetOwner(), TEXT("PhysicsReactionControl"));
		PhysicsControl->SetupAttachment(Mesh);
		PhysicsControl->RegisterComponent();
	}
	PrimaryComponentTick.AddPrerequisite(PhysicsControl, PhysicsControl->PrimaryComponentTick);

	const TMap<FName, FPhysicsControlLimbBones> LimbBones = PhysicsControl->GetLimbBonesFromSkeletalMesh(Mesh, MannequinLimbs());
	if (LimbBones.IsEmpty()) return false;

	// One body modifier per body: kinematic on the animation with zero blend weight, i.e. no effect until a reaction.
	const FPhysicsControlModifierData Animated(EPhysicsMovementType::Kinematic, ECollisionEnabled::QueryAndPhysics, 1.f, 0.f);
	for (const TPair<FName, FPhysicsControlLimbBones>& Limb : LimbBones)
		for (const FName Bone : Limb.Value.BoneNames)
		{
			const FName Modifier = PhysicsControl->CreateBodyModifier(Mesh, Bone, Limb.Key, Animated);
			if (Modifier.IsNone()) continue;
			BoneModifiers.Add(Bone, Modifier);
			BoneLimbs.Add(Bone, Limb.Key);
			AllModifiers.Add(Modifier);
		}

	// Motors: each body driven toward its animated orientation relative to its parent body. Off until a reaction.
	FPhysicsControlData Motor;
	Motor.AngularStrength = MotorStrength;
	Motor.AngularDampingRatio = MotorDampingRatio;
	Motor.LinearStrength = 0.f;		// the joints hold the bodies together; only rotation is driven
	Motor.bUseSkeletalAnimation = true;
	Motor.bEnabled = false;
	FPhysicsControlNames Controls;
	const TMap<FName, FPhysicsControlNames> ByLimb = PhysicsControl->CreateControlsFromLimbBones(Controls, LimbBones, EPhysicsControlType::ParentSpace, Motor);
	for (const TPair<FName, FPhysicsControlNames>& Limb : ByLimb) LimbControls.Add(Limb.Key, Limb.Value.Names);
	AllControls = Controls.Names;
	PhysicsControl->SetControlsEnabled(AllControls, false);
	UE_LOG(LogRPGInput, Log, TEXT("Physics reaction on %s: %d bodies, %d motors, limbs %s"), *GetOwner()->GetActorNameOrLabel(),
		AllModifiers.Num(), AllControls.Num(), *FString::JoinBy(LimbControls, TEXT(", "), [](const auto& Pair) { return Pair.Key.ToString(); }));
	return AllModifiers.Num() > 0;
}

FName URPGPhysicsReactionComponent::GetLimbForBone(FName Bone) const
{
	const USkeletalMeshComponent* Mesh = GetMesh();
	while (Bone != NAME_None)
	{
		if (const FName* Limb = BoneLimbs.Find(Bone)) return *Limb;
		Bone = Mesh ? Mesh->GetParentBone(Bone) : NAME_None;
	}
	return NAME_None;
}

float URPGPhysicsReactionComponent::GetStruckChainDeviation() const
{
	USkeletalMeshComponent* Mesh = GetMesh();
	if (!Mesh || !PhysicsControl || State == ERPGPhysicsReactionState::Animated) return 0.f;
	float Deviation = 0.f;
	for (const TPair<FName, FName>& Pair : BoneLimbs)
		if (Pair.Value == LastReaction.Limb || Pair.Key == LastReaction.HitBone)
			Deviation = FMath::Max(Deviation, float(FVector::Dist(Mesh->GetBoneLocation(Pair.Key), PhysicsControl->GetCachedBonePosition(Mesh, Pair.Key))));
	return Deviation;
}

int32 URPGPhysicsReactionComponent::GetNumSimulatingBodies() const
{
	int32 Count = 0;
	if (const USkeletalMeshComponent* Mesh = GetMesh())
		for (const TPair<FName, FName>& Pair : BoneModifiers)
			if (const FBodyInstance* Body = Mesh->GetBodyInstance(Pair.Key); Body && Body->IsInstanceSimulatingPhysics()) ++Count;
	return Count;
}

void URPGPhysicsReactionComponent::SetLimbMotorScale(FName Limb, float Scale)
{
	const TArray<FName>* Controls = LimbControls.Find(Limb);
	if (!PhysicsControl || !Controls) return;
	FPhysicsControlMultiplier Multiplier;
	Multiplier.AngularStrengthMultiplier = Scale;
	PhysicsControl->SetControlMultipliers(*Controls, Multiplier, true, true, false);
}

void URPGPhysicsReactionComponent::SetAllMotorScale(float Scale)
{
	for (const TPair<FName, TArray<FName>>& Limb : LimbControls) SetLimbMotorScale(Limb.Key, Scale);
}

void URPGPhysicsReactionComponent::SetPhysicsBlend(float Weight)
{
	PhysicsBlend = Weight;
	if (PhysicsControl) PhysicsControl->SetBodyModifiersPhysicsBlendWeight(AllModifiers, Weight, true, false);
}

float URPGPhysicsReactionComponent::GetStrikeStrength(const FRPGMeleeHitInfo& Hit) const
{
	const float Speed = Hit.LimbSpeed > 0.f ? FMath::Clamp(Hit.LimbSpeed / ReferenceStrikeSpeed, 0.4f, 1.6f) : 1.f;
	return FMath::Lerp(1.f, Speed, StrikeSpeedInfluence) * (1.f + PlaceholderChargeScale * FMath::Clamp(Hit.ChargePercent, 0.f, 1.f));
}

void URPGPhysicsReactionComponent::HandleMeleeHit(const FRPGMeleeHitInfo& Hit)
{
	if (!bReactionsEnabled || !bSetUp) return;
	USkeletalMeshComponent* Mesh = GetMesh();
	if (!Mesh) return;

	const bool bLimp = State == ERPGPhysicsReactionState::Limp || (Hit.bCharged && Hit.ChargePercent >= LimpChargeThreshold);
	bWaitForSettle = false;
	if (bLimp) EnterFullRagdoll(LimpTime);
	else EnterPoweredReaction(Hit.HitBone);

	const FName Bone = FindImpulseBody(Hit.HitBone, bLimp);
	if (Bone == NAME_None) return;
	const FBodyInstance* Body = Mesh->GetBodyInstance(Bone);
	const float Mass = Body ? Body->GetBodyMass() : 10.f;

	// TEMPORARY placeholder magnitude (see PlaceholderImpulseSpeed), scaled by how hard the strike was; the direction is
	// the real StrikeDirection.
	const float Strength = GetStrikeStrength(Hit);
	float Magnitude = PlaceholderImpulseSpeed * Mass * Strength;
	if (bLimp) Magnitude *= PlaceholderLimpImpulseScale;
	FVector Direction = Hit.StrikeDirection.GetSafeNormal();
	if (Direction.IsNearlyZero() && Hit.Attacker) Direction = (Hit.ImpactLocation - Hit.Attacker->GetActorLocation()).GetSafeNormal();

	LastReaction.HitBone = Hit.HitBone;
	LastReaction.AttackName = Hit.AttackName;
	LastReaction.Strength = Strength;
	ApplyReactionImpulse(Bone, Hit.ImpactLocation, Direction, Magnitude);
}

FName URPGPhysicsReactionComponent::FindImpulseBody(FName Bone, bool bLimp) const
{
	const USkeletalMeshComponent* Mesh = GetMesh();
	if (!Mesh) return NAME_None;

	// The hit body, or the nearest body above it.
	while (Bone != NAME_None && !BoneModifiers.Contains(Bone)) Bone = Mesh->GetParentBone(Bone);
	if (Bone == NAME_None) return NAME_None;
	// The anchor stays on the animation in POWERED: push the first body above it in its limb instead (spine_02 on the
	// mannequin; spine_01 has no body).
	if (!bLimp && Bone == AnchorBone)
		for (const TPair<FName, FName>& Pair : BoneModifiers)
		{
			if (Pair.Key == AnchorBone || BoneLimbs.FindRef(Pair.Key) != BoneLimbs.FindRef(AnchorBone)) continue;
			FName Parent = Mesh->GetParentBone(Pair.Key);
			while (Parent != NAME_None && !BoneModifiers.Contains(Parent)) Parent = Mesh->GetParentBone(Parent);
			if (Parent == AnchorBone) { Bone = Pair.Key; break; }
		}
	return Bone;
}

void URPGPhysicsReactionComponent::HandlePowerHit(const FRPGMeleeHitInfo& Hit, const FRPGPowerResult& Result)
{
	if (!bReactionsEnabled || !bSetUp) return;
	USkeletalMeshComponent* Mesh = GetMesh();
	if (!Mesh) return;

	const bool bLimp = Result.Reaction == ERPGPowerReaction::Knockdown || Result.Reaction == ERPGPowerReaction::Launch
		|| State == ERPGPhysicsReactionState::Limp;

	if (bLimp)
	{
		// A launch stays limp for its whole flight (up and back down) before the usual LimpTime on the ground.
		const float Gravity = URPGPowerSettings::Get()->GetRagdollGravity();
		const float FlightTime = Result.DisplacementVelocity.Z > 0.f ? 2.f * float(Result.DisplacementVelocity.Z) / Gravity : 0.f;
		EnterFullRagdoll(LimpTime + FlightTime);
		bWaitForSettle = true;

		// The whole body moves (a launch or a sliding fall), not only the struck part.
		if (!Result.DisplacementVelocity.IsNearlyZero())
		{
			bVelocityPending = true;
			PendingVelocity = Result.DisplacementVelocity;
			PendingVelocityPoint = Hit.ImpactLocation;
			PendingVelocityFrames = 0;
			ApplyPendingVelocity();
		}
	}
	else
	{
		EnterPoweredReaction(Hit.HitBone);
	}

	// The struck part still takes a local push, so the hit reads on the body (and a knockdown topples away from it).
	const FName Bone = FindImpulseBody(Hit.HitBone, bLimp);
	if (Bone == NAME_None) return;
	const FBodyInstance* Body = Mesh->GetBodyInstance(Bone);
	const float Mass = Body ? Body->GetBodyMass() : 10.f;
	const float Speed = Result.Reaction == ERPGPowerReaction::Flinch ? FlinchImpulseSpeed : StaggerImpulseSpeed;

	FVector Direction = Hit.StrikeDirection.GetSafeNormal();
	if (Direction.IsNearlyZero() && Hit.Attacker) Direction = (Hit.ImpactLocation - Hit.Attacker->GetActorLocation()).GetSafeNormal();

	LastReaction.HitBone = Hit.HitBone;
	LastReaction.AttackName = Hit.AttackName;
	LastReaction.Strength = Result.RelativePower;
	ApplyReactionImpulse(Bone, Hit.ImpactLocation, Direction, Speed * Mass);
}

void URPGPhysicsReactionComponent::ApplyPendingVelocity()
{
	USkeletalMeshComponent* Mesh = GetMesh();
	if (!bVelocityPending || !Mesh) return;
	const FBodyInstance* Root = Mesh->GetBodyInstance(AnchorBone);
	if (Root && Root->IsInstanceSimulatingPhysics())
	{
		// The whip: parts near the impact get more than the average velocity, far parts less, weighted so the body's
		// overall (mass-weighted average) velocity is exactly PendingVelocity. The body folds around the hit.
		const URPGPowerSettings* S = URPGPowerSettings::Get();
		TArray<TPair<FBodyInstance*, float>> Weighted;
		float TotalMass = 0.f, WeightedMass = 0.f;
		for (FBodyInstance* Body : Mesh->Bodies)
		{
			if (!Body || !Body->IsInstanceSimulatingPhysics()) continue;
			const float Mass = Body->GetBodyMass();
			const float Near = 1.f - FMath::Clamp(float(FVector::Dist(Body->GetCOMPosition(), PendingVelocityPoint)) / S->RagdollWhipRadius, 0.f, 1.f);
			const float Weight = 1.f + S->RagdollWhip * (2.f * Near - 1.f);
			Weighted.Emplace(Body, Weight);
			TotalMass += Mass;
			WeightedMass += Mass * Weight;
		}
		const float Normalize = WeightedMass > KINDA_SMALL_NUMBER ? TotalMass / WeightedMass : 1.f;
		for (const TPair<FBodyInstance*, float>& Pair : Weighted)
			Pair.Key->SetLinearVelocity(PendingVelocity * (Pair.Value * Normalize), false);
		bVelocityPending = false;
	}
	else if (++PendingVelocityFrames > MaxPendingFrames)
	{
		bVelocityPending = false;
	}
}

void URPGPhysicsReactionComponent::EnterPoweredReaction(FName HitBone)
{
	if (!bSetUp || !PhysicsControl) return;
	// A hit while already ragdolled keeps it limp.
	if (State == ERPGPhysicsReactionState::Limp && !bReturning) return;

	const bool bFromAnimation = State == ERPGPhysicsReactionState::Animated;
	State = ERPGPhysicsReactionState::Powered;
	StateTime = 0.f;
	bReturning = false;
	StruckLimb = GetLimbForBone(HitBone);

	PhysicsControl->SetBodyModifiersMovementType(AllModifiers, EPhysicsMovementType::Simulated, true, false);
	if (const FName* Anchor = BoneModifiers.Find(AnchorBone))
		PhysicsControl->SetBodyModifierMovementType(*Anchor, EPhysicsMovementType::Kinematic, true, false);
	// Current motor settings (so they can be tuned while playing), then the struck limb weakened.
	for (const FName& Name : AllControls)
	{
		FPhysicsControlData Data;
		if (!PhysicsControl->GetControlData(Name, Data)) continue;
		Data.AngularStrength = MotorStrength;
		Data.AngularDampingRatio = MotorDampingRatio;
		Data.bEnabled = true;
		PhysicsControl->SetControlData(Name, Data, true, false);
	}
	SetAllMotorScale(1.f);
	SetLimbMotorScale(StruckLimb, StruckLimbStrengthScale);
	// From the animation the bodies start exactly on the pose, so full weight at once; part-way through a fade back
	// it blends in quickly instead of popping to the physics pose.
	if (bFromAnimation) SetPhysicsBlend(1.f);

	LastReaction = FRPGPhysicsReactionInfo();
	LastReaction.State = State;
	LastReaction.HitBone = HitBone;
	LastReaction.Limb = StruckLimb;
}

void URPGPhysicsReactionComponent::EnterFullRagdoll(float InLimpTime)
{
	if (!bSetUp || !PhysicsControl) return;
	const bool bFromAnimation = State == ERPGPhysicsReactionState::Animated;
	const bool bWasLimp = State == ERPGPhysicsReactionState::Limp;
	State = ERPGPhysicsReactionState::Limp;
	StateTime = 0.f;
	bReturning = false;
	LimpDuration = InLimpTime;

	PhysicsControl->SetBodyModifiersMovementType(AllModifiers, EPhysicsMovementType::Simulated, true, false);
	PhysicsControl->SetControlsEnabled(AllControls, false);
	if (bFromAnimation) SetPhysicsBlend(1.f);

	if (!bWasLimp)
	{
		LastReaction = FRPGPhysicsReactionInfo();
		LastReaction.Limb = NAME_None;
	}
	LastReaction.State = State;
}

void URPGPhysicsReactionComponent::ApplyReactionImpulse(FName HitBone, FVector ImpactPoint, FVector Direction, float Magnitude)
{
	LastReaction.ImpactPoint = ImpactPoint;
	LastReaction.ImpulseDirection = Direction.GetSafeNormal();
	LastReaction.ImpulseMagnitude = Magnitude;
	LastReaction.bImpulseApplied = false;
	if (LastReaction.Limb == NAME_None) LastReaction.Limb = GetLimbForBone(HitBone);

	bImpulsePending = true;
	PendingBone = HitBone;
	PendingPoint = ImpactPoint;
	PendingImpulse = LastReaction.ImpulseDirection * Magnitude;
	PendingFrames = 0;
	// Already simulating (a hit during a reaction): no need to wait a frame.
	ApplyPendingImpulse();

	UE_LOG(LogRPGInput, Log, TEXT("PHYSICS REACTION: %s on %s  bone [%s] limb %s  impact (%.0f, %.0f, %.0f)  direction (%.2f, %.2f, %.2f)  impulse %.0f kg cm/s (TEMPORARY)  state %s"),
		*LastReaction.AttackName, *GetOwner()->GetActorNameOrLabel(), *HitBone.ToString(), *LastReaction.Limb.ToString(),
		ImpactPoint.X, ImpactPoint.Y, ImpactPoint.Z, LastReaction.ImpulseDirection.X, LastReaction.ImpulseDirection.Y, LastReaction.ImpulseDirection.Z,
		Magnitude, StateName(State));

	if (ShouldDrawDebug())
	{
		UWorld* World = GetWorld();
		DrawDebugSphere(World, ImpactPoint, 4.f, 10, FColor::Yellow, false, DebugDrawTime, 0, 1.f);
		DrawDebugDirectionalArrow(World, ImpactPoint, ImpactPoint + LastReaction.ImpulseDirection * 45.f, 12.f, FColor::Orange, false, DebugDrawTime, 0, 2.f);
		DrawDebugString(World, ImpactPoint + LastReaction.ImpulseDirection * 50.f,
			FString::Printf(TEXT("[%s] %s  impulse %.0f (TEMP)"), *HitBone.ToString(), StateName(State), Magnitude), nullptr, FColor::Orange, DebugDrawTime, true);
		if (GEngine && URPGCombatStateComponent::IsDebugDisplayEnabled())
			GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Orange, FString::Printf(TEXT("REACTION %s: [%s] %s, impulse %.0f kg cm/s (TEMPORARY)"),
				StateName(State), *HitBone.ToString(), *LastReaction.Limb.ToString(), Magnitude));
	}
}

void URPGPhysicsReactionComponent::ApplyPendingImpulse()
{
	USkeletalMeshComponent* Mesh = GetMesh();
	if (!bImpulsePending || !Mesh) return;
	FBodyInstance* Body = Mesh->GetBodyInstance(PendingBone);
	if (Body && Body->IsInstanceSimulatingPhysics())
	{
		Body->AddImpulseAtPosition(PendingImpulse, PendingPoint);
		bImpulsePending = false;
		LastReaction.bImpulseApplied = true;
		++ImpulsesApplied;
	}
	else if (++PendingFrames > MaxPendingFrames)
	{
		// e.g. the anchor (kinematic in POWERED) was hit: the motors' yield comes from the bodies above it instead.
		bImpulsePending = false;
		UE_LOG(LogRPGInput, Verbose, TEXT("Physics reaction: [%s] is not simulating, impulse dropped."), *PendingBone.ToString());
	}
}

void URPGPhysicsReactionComponent::ReturnToAnimated(float BlendTime)
{
	if (State == ERPGPhysicsReactionState::Animated || bReturning) return;
	bVelocityPending = false;
	if (State == ERPGPhysicsReactionState::Limp)
	{
		OnLimpEnding.Broadcast();
		if (State == ERPGPhysicsReactionState::Animated) return;   // a listener already finished it (e.g. an instant reset)
	}
	bReturning = true;
	ReturnTime = 0.f;
	ReturnDuration = BlendTime >= 0.f ? BlendTime : ReturnBlendTime;
	ReturnStartBlend = PhysicsBlend;
	if (ReturnDuration <= 0.f) FinishReturn();
}

void URPGPhysicsReactionComponent::FinishReturn()
{
	// The pose is the animation's now (zero blend), so the bodies can be snapped back onto it unseen.
	State = ERPGPhysicsReactionState::Animated;
	bReturning = false;
	StruckLimb = NAME_None;
	bImpulsePending = false;
	if (!PhysicsControl) return;
	SetPhysicsBlend(0.f);
	PhysicsControl->SetControlsEnabled(AllControls, false);
	SetAllMotorScale(1.f);
	PhysicsControl->SetBodyModifiersMovementType(AllModifiers, EPhysicsMovementType::Kinematic, true, false);
	PhysicsControl->ResetBodyModifiersToCachedBoneTransforms(AllModifiers, EResetToCachedTargetBehavior::ResetDuringUpdateControls, true, false);
}

void URPGPhysicsReactionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bSetUp) return;
	ApplyPendingImpulse();
	ApplyPendingVelocity();

	// Power System ragdolls fall with the heavier ragdoll gravity (the launch speeds are worked out with it).
	const float ExtraGravity = URPGPowerSettings::Get()->GetRagdollGravity() - URPGPowerSettings::Get()->Gravity;
	if (State == ERPGPhysicsReactionState::Limp && bWaitForSettle && ExtraGravity > 0.f)
		if (USkeletalMeshComponent* LimpMesh = GetMesh())
			for (FBodyInstance* Body : LimpMesh->Bodies)
				if (Body && Body->IsInstanceSimulatingPhysics())
					Body->AddForce(FVector(0.f, 0.f, -ExtraGravity), true, true);   // acceleration: same for every mass

	USkeletalMeshComponent* Mesh = GetMesh();
	if (State != ERPGPhysicsReactionState::Animated)
	{
		StateTime += DeltaTime;
		if (bReturning)
		{
			ReturnTime += DeltaTime;
			const float Alpha = ReturnDuration > 0.f ? FMath::Clamp(ReturnTime / ReturnDuration, 0.f, 1.f) : 1.f;
			SetPhysicsBlend(ReturnStartBlend * (1.f - FMath::SmoothStep(0.f, 1.f, Alpha)));
			if (Alpha >= 1.f) FinishReturn();
		}
		else
		{
			if (PhysicsBlend < 1.f) SetPhysicsBlend(FMath::Min(1.f, PhysicsBlend + DeltaTime / BlendInTime));
			if (State == ERPGPhysicsReactionState::Powered)
			{
				// Struck limb: yields, then its motors recover to full strength, then the pose fades back.
				const float Recover = PoweredRecoverTime > 0.f ? FMath::Clamp((StateTime - PoweredYieldTime) / PoweredRecoverTime, 0.f, 1.f) : 1.f;
				SetLimbMotorScale(StruckLimb, FMath::Lerp(StruckLimbStrengthScale, 1.f, Recover * Recover));
				if (StateTime >= PoweredYieldTime + PoweredRecoverTime) ReturnToAnimated(ReturnBlendTime);
			}
			else if (LimpDuration > 0.f && StateTime >= LimpDuration)
			{
				const FBodyInstance* Pelvis = Mesh ? Mesh->GetBodyInstance(AnchorBone) : nullptr;
				const bool bSettled = !bWaitForSettle || !Pelvis || Pelvis->GetUnrealWorldVelocity().Size() <= LimpSettleSpeed;
				if (bSettled || StateTime >= LimpDuration + LimpMaxSettleWait) ReturnToAnimated(LimpReturnBlendTime);
			}
		}
		LastReaction.PeakDeviation = FMath::Max(LastReaction.PeakDeviation, GetStruckChainDeviation());
	}
	LastReaction.State = State;

	if (Mesh && ShouldDrawDebug())
	{
		const FString Text = bReturning
			? FString::Printf(TEXT("%s -> ANIMATED %.0f%%"), StateName(State), 100.f * (1.f - PhysicsBlend))
			: State == ERPGPhysicsReactionState::Powered
			? FString::Printf(TEXT("POWERED  [%s] %s  yield %.0f cm"), *LastReaction.HitBone.ToString(), *StruckLimb.ToString(), LastReaction.PeakDeviation)
			: FString(StateName(State));
		DrawDebugString(GetWorld(), Mesh->GetBoneLocation(TEXT("head")) + FVector(0.f, 0.f, 35.f), Text, nullptr,
			State == ERPGPhysicsReactionState::Animated ? FColor::Silver : FColor::Orange, 0.f, true, 1.1f);
	}
}

bool URPGPhysicsReactionComponent::ShouldDrawDebug() const
{
	const int32 Mode = CVarPhysicsReactionDebug.GetValueOnGameThread();
	return bDrawDebug && (Mode > 0 || (Mode < 0 && URPGCombatStateComponent::IsDebugDisplayEnabled()));
}

void URPGPhysicsReactionComponent::DebugPoweredHit()
{
	USkeletalMeshComponent* Mesh = GetMesh();
	if (!Mesh || !GetWorld() || !GetWorld()->IsGameWorld()) return;
	FRPGMeleeHitInfo Hit;
	Hit.AttackName = TEXT("Debug Hit");
	Hit.HitBone = TEXT("spine_05");
	Hit.ImpactLocation = Mesh->GetBoneLocation(Hit.HitBone) + GetOwner()->GetActorForwardVector() * 12.f;
	Hit.StrikeDirection = -GetOwner()->GetActorForwardVector();
	HandleMeleeHit(Hit);
}

void URPGPhysicsReactionComponent::DebugFullRagdoll()
{
	USkeletalMeshComponent* Mesh = GetMesh();
	if (!Mesh || !GetWorld() || !GetWorld()->IsGameWorld()) return;
	FRPGMeleeHitInfo Hit;
	Hit.AttackName = TEXT("Debug Ragdoll");
	Hit.bCharged = true;
	Hit.ChargePercent = 1.f;
	Hit.HitBone = TEXT("spine_05");
	Hit.ImpactLocation = Mesh->GetBoneLocation(Hit.HitBone) + GetOwner()->GetActorForwardVector() * 12.f;
	Hit.StrikeDirection = -GetOwner()->GetActorForwardVector();
	HandleMeleeHit(Hit);
}
