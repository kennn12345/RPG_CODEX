#include "RPGMeleeHitComponent.h"

#include "RPG_CODEX.h"
#include "RPGCombatStateComponent.h"
#include "RPGMeleeTarget.h"
#include "RPGStrikeContactComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarMeleeDebug(
		TEXT("rpg.MeleeDebug"), -1,
		TEXT("Melee hit sweeps debug drawing: -1 = with rpg.Debug (default), 0 = off, 1 = on (when the component's bDrawDebugTraces is also on)."));

	FColor LimbColor(ERPGStrikeLimb Limb)
	{
		switch (Limb)
		{
		case ERPGStrikeLimb::RightHand:	return FColor(255, 60, 40);
		case ERPGStrikeLimb::LeftHand:	return FColor(60, 120, 255);
		case ERPGStrikeLimb::RightFoot:	return FColor(255, 170, 0);
		case ERPGStrikeLimb::LeftFoot:	return FColor(0, 220, 220);
		default:						return FColor::White;
		}
	}

	bool IsFoot(ERPGStrikeLimb Limb)
	{
		return Limb == ERPGStrikeLimb::RightFoot || Limb == ERPGStrikeLimb::LeftFoot;
	}
}

URPGMeleeHitComponent::URPGMeleeHitComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// After the mesh has evaluated this frame's pose, so the limb points are where the animation put them.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

USkeletalMeshComponent* URPGMeleeHitComponent::GetMesh() const
{
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner())) return Character->GetMesh();
	return GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}

bool URPGMeleeHitComponent::GetLimbPoints(ERPGStrikeLimb Limb, TArray<FVector>& OutPoints) const
{
	OutPoints.Reset();
	const USkeletalMeshComponent* Mesh = GetMesh();
	if (!Mesh || Limb == ERPGStrikeLimb::None) return false;
	const bool bRight = Limb == ERPGStrikeLimb::RightHand || Limb == ERPGStrikeLimb::RightFoot;
	FName Root, Tip;
	if (IsFoot(Limb))
	{
		Root = bRight ? FName(TEXT("foot_r")) : FName(TEXT("foot_l"));		// ankle, over the heel
		Tip = bRight ? FName(TEXT("ball_r")) : FName(TEXT("ball_l"));		// ball of the foot
	}
	else
	{
		Root = bRight ? FName(TEXT("hand_r")) : FName(TEXT("hand_l"));		// wrist
		Tip = bRight ? FName(TEXT("middle_01_r")) : FName(TEXT("middle_01_l"));	// knuckles
	}
	if (Mesh->GetBoneIndex(Root) == INDEX_NONE || Mesh->GetBoneIndex(Tip) == INDEX_NONE) return false;
	const FVector A = Mesh->GetBoneLocation(Root);
	const FVector B = Mesh->GetBoneLocation(Tip);
	// Fist: the knuckles and the middle of the fist (the wrist itself never lands). Foot: heel and ball, the sole.
	OutPoints.Add(IsFoot(Limb) ? A : FMath::Lerp(A, B, 0.55f));
	OutPoints.Add(B);
	return true;
}

bool URPGMeleeHitComponent::ShouldDrawDebug() const
{
	const int32 Mode = CVarMeleeDebug.GetValueOnGameThread();
	return bDrawDebugTraces && (Mode > 0 || (Mode < 0 && URPGCombatStateComponent::IsDebugDisplayEnabled()));
}

void URPGMeleeHitComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	URPGCombatStateComponent* State = GetOwner() ? GetOwner()->FindComponentByClass<URPGCombatStateComponent>() : nullptr;
	UWorld* World = GetWorld();
	TArray<FVector> Points;
	if (!State || !World || !GetLimbPoints(State->StrikeLimb, Points))
	{
		LastPoints.Reset();
		LastLimb = ERPGStrikeLimb::None;
		return;
	}
	// The limb is tracked every frame (cheap), so the first frame of a window already sweeps from last frame.
	const bool bHaveLast = LastLimb == State->StrikeLimb && LastPoints.Num() == Points.Num();
	const TArray<FVector> From = bHaveLast ? LastPoints : Points;
	// The strike contact holds the limb at a target's surface: the strike's direction and speed are the animation's.
	const URPGStrikeContactComponent* Contact = GetOwner()->FindComponentByClass<URPGStrikeContactComponent>();
	const FVector Correction = Contact ? Contact->GetCorrection(State->StrikeLimb) : FVector::ZeroVector;
	const FVector CorrectionChange = bHaveLast ? Correction - LastCorrection : FVector::ZeroVector;
	LastPoints = Points;
	LastLimb = State->StrikeLimb;
	LastCorrection = Correction;
	if (!State->bStrikeHitWindowOpen) return;

	const bool bNewStrike = State->StrikeId != CurrentStrikeId;
	if (bNewStrike)
	{
		CurrentStrikeId = State->StrikeId;
		HitThisStrike.Reset();
	}
	const float Radius = IsFoot(State->StrikeLimb) ? FootRadius : FistRadius;
	const bool bDraw = ShouldDrawDebug();
	const FColor Color = LimbColor(State->StrikeLimb);
	if (bDraw && bNewStrike)
		DrawDebugString(World, Points.Last() + FVector(0, 0, 12), UEnum::GetDisplayValueAsText(State->StrikeLimb).ToString(), nullptr, Color, DebugDrawTime, true);

	// The strike contact holds the limb at a surface (it can be the knee / shin of a close kick, before the foot gets
	// there): that is the strike landing, on that body.
	{
		UPrimitiveComponent* Touched = nullptr;
		FName Bone;
		FVector Point, Normal;
		if (Contact && Contact->HasStrikingContact(State->StrikeLimb) && Contact->GetContact(State->StrikeLimb, Touched, Bone, Point, Normal)
			&& Touched->GetOwner() && !HitThisStrike.Contains(Touched->GetOwner()))
		{
			HitThisStrike.Add(Touched->GetOwner());
			FHitResult Hit(Touched->GetOwner(), Touched, Point, Normal);
			Hit.BoneName = Bone;
			Hit.ImpactNormal = Normal;
			FVector Travel = Points[0] - From[0] - CorrectionChange;
			ReportHit(Hit, Points[0], Travel, State);
		}
	}

	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RPGMeleeHit), false, GetOwner());
	for (int32 I = 0; I < Points.Num(); ++I)
	{
		if (bDraw)
		{
			// The swept volume: a capsule from last frame's sphere to this frame's.
			const FVector Path = Points[I] - From[I];
			const FQuat Along = Path.IsNearlyZero() ? FQuat::Identity : FRotationMatrix::MakeFromZ(Path).ToQuat();
			DrawDebugCapsule(World, (From[I] + Points[I]) * 0.5f, Path.Size() * 0.5f + Radius, Radius, Along, Color, false, DebugDrawTime);
		}
		TArray<FHitResult> Hits;
		World->SweepMultiByObjectType(Hits, From[I], Points[I], FQuat::Identity, Objects, FCollisionShape::MakeSphere(Radius), Params);
		for (const FHitResult& Hit : Hits)
		{
			AActor* Target = Hit.GetActor();
			if (!Target || Target == GetOwner() || HitThisStrike.Contains(Target)) continue;
			HitThisStrike.Add(Target);
			// Kicks: the strike direction is the leg's travel (the ankle, point 0), not whichever sphere touched first -
			// the ball of the foot is often first, while it is still turning up to put the sole on the target.
			FVector Travel = Points[I] - From[I] - CorrectionChange;
			if (IsFoot(State->StrikeLimb) && (Points[0] - From[0] - CorrectionChange).SizeSquared() > 0.25f) Travel = Points[0] - From[0] - CorrectionChange;
			ReportHit(Hit, Points[I], Travel, State);
		}
	}
}

void URPGMeleeHitComponent::ReportHit(const FHitResult& Hit, const FVector& End, const FVector& Travel, URPGCombatStateComponent* State)
{
	AActor* Target = Hit.GetActor();
	const float DeltaTime = GetWorld()->GetDeltaSeconds();
	FRPGMeleeHitInfo Info;
	Info.AttackName = State->GetStrikeName();
	Info.Limb = State->StrikeLimb;
	Info.bKick = IsFoot(State->StrikeLimb);
	Info.bCharged = State->bStrikeCharged;
	Info.ChargePercent = State->LastReleaseChargePercent;
	Info.AttackPhase = State->AttackPhase;
	Info.TimeFromImpact = State->StrikeTimeFromImpact;
	Info.StrikeId = State->StrikeId;
	Info.Attacker = GetOwner();
	Info.HitActor = Target;
	Info.HitComponent = Hit.GetComponent();
	Info.HitBone = Hit.BoneName;
	// Already overlapping when the window opened: there is no swept contact point, use the limb itself.
	Info.ImpactLocation = Hit.bStartPenetrating ? End : FVector(Hit.ImpactPoint);
	Info.ImpactNormal = Hit.ImpactNormal;
	Info.StrikeDirection = Travel.GetSafeNormal();
	Info.AimDirection = State->AttackAimDirection;
	Info.AimPoint = State->AttackAimPoint;
	Info.AimYaw = State->AttackAimYaw;
	Info.AimPitch = State->AttackAimPitch;
	Info.LimbSpeed = DeltaTime > 0.f ? Travel.Size() / DeltaTime : 0.f;
	Info.Distance = FVector::Dist2D(GetOwner()->GetActorLocation(), Target->GetActorLocation());

	if (bPrintHits)
	{
		const FString Message = FString::Printf(
			TEXT("HIT: %s -> %s [%s]  aim %+.0f / %+.0f deg  release charge %.0f%%  (AttackCharge %.0f%%, KickCharge %.0f%%)  phase %s  limb %s  %+.3fs from impact frame  at (%.0f, %.0f, %.0f)  distance %.0f cm  limb speed %.0f cm/s"),
			*Info.AttackName, *Target->GetActorNameOrLabel(), *Info.HitBone.ToString(), Info.AimYaw, Info.AimPitch, Info.ChargePercent * 100.f,
			State->AttackChargePercent * 100.f, State->KickChargePercent * 100.f,
			*UEnum::GetDisplayValueAsText(Info.AttackPhase).ToString(), *UEnum::GetDisplayValueAsText(Info.Limb).ToString(),
			Info.TimeFromImpact, Info.ImpactLocation.X, Info.ImpactLocation.Y, Info.ImpactLocation.Z, Info.Distance, Info.LimbSpeed);
		UE_LOG(LogRPGInput, Log, TEXT("%s"), *Message);
		if (GEngine && URPGCombatStateComponent::IsDebugDisplayEnabled()) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, Message);
	}
	if (ShouldDrawDebug())
	{
		DrawDebugSphere(GetWorld(), Info.ImpactLocation, 5.f, 12, FColor::Green, false, DebugDrawTime, 0, 1.5f);
		DrawDebugString(GetWorld(), Info.ImpactLocation + FVector(0, 0, 8), FString::Printf(TEXT("%s [%s]"), *Info.AttackName, *Info.HitBone.ToString()),
			nullptr, FColor::Green, DebugDrawTime, true);
	}

	if (Target->Implements<URPGMeleeTarget>()) IRPGMeleeTarget::Execute_ReceiveMeleeHit(Target, Info);
	OnMeleeHit.Broadcast(Info);
}
