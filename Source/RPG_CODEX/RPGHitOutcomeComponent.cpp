#include "RPGHitOutcomeComponent.h"

#include "RPGAnimInstance.h"
#include "RPGAttributeSet.h"
#include "RPGBodyImpactComponent.h"
#include "RPGCharacterStatsComponent.h"
#include "RPGCombatStateComponent.h"
#include "RPGHitCameraShake.h"
#include "RPGNpcCharacter.h"
#include "RPGPhysicsReactionComponent.h"
#include "RPGPowerSettings.h"
#include "RPGPowerSystem.h"
#include "RPGPrototypeAnimationComponent.h"
#include "RPG_CODEX.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

namespace
{
	/** GASP's ragdoll reach poses (UEFN mannequin): arms out toward the fall. Forward, back, left, right. */
	const TCHAR* ReachClipNames[] = { TEXT("M_ragdoll_reach_forward"), TEXT("M_ragdoll_reach_back"), TEXT("M_ragdoll_reach_left"), TEXT("M_ragdoll_reach_right") };

	bool IsBodyShotBone(FName Bone)
	{
		const FString Name = Bone.ToString().ToLower();
		return Name == TEXT("pelvis") || Name == TEXT("spine_01") || Name == TEXT("spine_02") || Name == TEXT("spine_03");
	}
}

URPGHitOutcomeComponent::URPGHitOutcomeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void URPGHitOutcomeComponent::BeginPlay()
{
	Super::BeginPlay();
	for (const TCHAR* Name : ReachClipNames)
		ReachClips.Add(LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/Characters/UEFN_Mannequin/Animations/Ragdoll/%s.%s"), Name, Name)));
}

ACharacter* URPGHitOutcomeComponent::GetCharacter() const { return Cast<ACharacter>(GetOwner()); }

URPGPhysicsReactionComponent* URPGHitOutcomeComponent::GetReaction() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<URPGPhysicsReactionComponent>() : nullptr;
}

bool URPGHitOutcomeComponent::IsKnockedOut() const
{
	URPGCharacterStatsComponent* Stats = URPGCharacterStatsComponent::FindStats(GetOwner());
	const URPGAttributeSet* A = Stats ? Stats->GetAttributes() : nullptr;
	if (const ARPGNpcCharacter* Npc = Cast<ARPGNpcCharacter>(GetOwner()); Npc && Npc->bKnockedOut) return true;
	return A && A->GetMaxHealth() > 0.f && A->GetHealth() <= A->GetMaxHealth() * LimpHealthShare;
}

float URPGHitOutcomeComponent::GetSituation(const FVector& StrikeDirection, bool* bOutFromBehind) const
{
	const URPGPowerSettings* S = URPGPowerSettings::Get();
	const ACharacter* Character = GetCharacter();
	float Situation = 1.f;
	bool bFromBehind = false;
	if (Character)
	{
		const float Speed = float(Character->GetVelocity().Size2D());
		// Planted in the fighting stance: set to take it.
		if (const URPGPrototypeAnimationComponent* Proto = Character->FindComponentByClass<URPGPrototypeAnimationComponent>(); Proto && Proto->IsInCombat() && Speed < 150.f)
			Situation *= S->StanceSituation;
		// Caught mid-attack: committed, weight on the front foot.
		if (const URPGCombatStateComponent* Combat = Character->FindComponentByClass<URPGCombatStateComponent>(); Combat && Combat->AttackPhase != ERPGAttackPhase::None)
			Situation *= S->AttackingSituation;
		if (Speed > 250.f) Situation *= S->RunningSituation;
		// Struck in the back: the force travels the way it faces.
		bFromBehind = FVector::DotProduct(StrikeDirection.GetSafeNormal2D(), Character->GetActorForwardVector().GetSafeNormal2D()) > 0.5f;
		if (bFromBehind) Situation *= S->FromBehindSituation;
	}
	if (bOutFromBehind) *bOutFromBehind = bFromBehind;
	return Situation;
}

void URPGHitOutcomeComponent::PrepareInput(FRPGPowerInput& Input) const
{
	Input.Balance = Balance;
	Input.Situation = GetSituation(Input.StrikeDirection);
	if (TestSeed != 0) Input.RandomSeed = TestSeed;
}

void URPGHitOutcomeComponent::NoteHit(const FRPGPowerResult& Result)
{
	const URPGPowerSettings* S = URPGPowerSettings::Get();
	Balance = FMath::Clamp(Balance - Result.ReactionLevel * S->BalanceDrainPerLevel, 0.f, 1.f);
	LastHitTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	LastResult = Result;
	++HitCount;
}

void URPGHitOutcomeComponent::NoteBump(ERPGPowerReaction Reaction)
{
	// A bump rocks it like a hit of about this many levels.
	const float Level = Reaction == ERPGPowerReaction::Knockback ? 6.f : Reaction == ERPGPowerReaction::Stagger ? 4.f : 1.f;
	Balance = FMath::Clamp(Balance - Level * URPGPowerSettings::Get()->BalanceDrainPerLevel, 0.f, 1.f);
	LastHitTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

void URPGHitOutcomeComponent::StartBrace(const FVector& FallDirection, float Weight, float Hold, float Fade)
{
	const ACharacter* Character = GetCharacter();
	if (!Character || ReachClips.Num() < 4) return;
	// Which way it falls, relative to its body: forward / back, blended toward left / right.
	const FVector Dir = FallDirection.GetSafeNormal2D();
	const float Forward = float(FVector::DotProduct(Dir, Character->GetActorForwardVector()));
	const float Right = float(FVector::DotProduct(Dir, Character->GetActorRightVector()));
	UAnimSequence* Main = Forward >= 0.f ? ReachClips[0] : ReachClips[1];
	UAnimSequence* Side = Right >= 0.f ? ReachClips[3] : ReachClips[2];
	const float Total = FMath::Abs(Forward) + FMath::Abs(Right);
	BraceAlpha = Total > KINDA_SMALL_NUMBER ? FMath::Abs(Right) / Total : 0.f;
	BraceA = Main;
	BraceB = Side;
	BraceTarget = FMath::Clamp(Weight, 0.f, 1.f);
	BraceHold = Hold;
	BraceFade = FMath::Max(Fade, 0.05f);
	if (!BraceA) { BraceA = BraceB; BraceB = nullptr; }
}

void URPGHitOutcomeComponent::StartBend(float Degrees, float Hold, float Fade)
{
	BendTarget = FMath::Max(BendTarget * (BendHold > 0.f ? 1.f : 0.f), Degrees);
	BendHold = Hold;
	BendFade = FMath::Max(Fade, 0.05f);
}

void URPGHitOutcomeComponent::BeginFall(const FVector& FallDirection, bool bKnockedOut)
{
	++FallCount;
	URPGPhysicsReactionComponent* Reaction = GetReaction();
	if (Reaction) Reaction->SetRagdollBrace(!bKnockedOut);
	if (bKnockedOut)
	{
		// Out cold: no muscle, no reaching.
		++LimpFallCount;
		bOutCold = true;
		bFallBrace = false;
		BraceTarget = BraceWeight = 0.f;
		BraceHold = 0.f;
	}
	else
	{
		StartBrace(FallDirection, 1.f, 0.f, 0.3f);
		bFallBrace = true;
		BraceWeight = 1.f;
	}
	FallStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (const AActor* Owner = GetOwner()) PlayImpactSound(Owner, Owner->GetActorLocation(), 0.8f);
}

void URPGHitOutcomeComponent::PlayImpactSound(const AActor* Owner, const FVector& Location, float Volume)
{
	static TWeakObjectPtr<USoundBase> Sound;
	if (!Sound.IsValid())
		Sound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Audio/Foley/MetaSounds/Presets/MSS_FoleySound_Ragdoll.MSS_FoleySound_Ragdoll"));
	if (Owner && Sound.IsValid()) UGameplayStatics::PlaySoundAtLocation(Owner, Sound.Get(), Location, Volume);
}

void URPGHitOutcomeComponent::Shake(float Scale) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr)
		Controller->ClientStartCameraShake(URPGHitCameraShake::StaticClass(), Scale);
}

void URPGHitOutcomeComponent::Push(FVector Velocity)
{
	ACharacter* Character = GetCharacter();
	UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	Velocity.Z = 0.f;
	if (!Move || !Move->IsMovingOnGround()) return;
	Move->Velocity += Velocity;
}

void URPGHitOutcomeComponent::Fall(const FVector& Velocity, const FVector& Point)
{
	// (The body impact component prepares the braced fall itself, once its ragdoll exists.)
	if (URPGBodyImpactComponent* Impact = GetOwner()->FindComponentByClass<URPGBodyImpactComponent>())
		Impact->KnockDown(Velocity, Point);
	else if (URPGPhysicsReactionComponent* Reaction = GetReaction())
	{
		BeginFall(Velocity, IsKnockedOut());
		Reaction->Launch(Velocity, Point, Reaction->LimpTime);
	}
}

void URPGHitOutcomeComponent::ApplyResult(const FRPGPowerResult& Result, const FVector& Direction, const FVector& Point, FName Bone)
{
	const FVector Dir = Direction.GetSafeNormal2D().IsNearlyZero() ? -GetOwner()->GetActorForwardVector() : Direction.GetSafeNormal2D();
	URPGBodyImpactComponent* Impact = GetOwner()->FindComponentByClass<URPGBodyImpactComponent>();
	URPGPhysicsReactionComponent* Reaction = GetReaction();
	const bool bDown = (Impact && Impact->IsDown()) || (Reaction && Reaction->GetReactionState() == ERPGPhysicsReactionState::Limp);
	if (bDown)
	{
		// On the ground it can still be hit: the blow goes into the lying body and keeps it down a little longer.
		if (Reaction)
		{
			Reaction->EnterFullRagdoll(Reaction->LimpTime);
			Reaction->ApplyReactionImpulse(Bone, Point, Dir, FMath::Max(Result.ReactionLevel, 1.f) * 600.f);
		}
		Shake(0.3f);
		return;
	}
	// A body shot doubles it over.
	if (IsBodyShotBone(Bone) && Result.Reaction != ERPGPowerReaction::Flinch)
		StartBend(Result.Reaction >= ERPGPowerReaction::Knockback ? 28.f : 18.f, 0.25f, 0.6f);

	const FVector Shove = (FVector(Result.DisplacementVelocity) * PushScale).GetClampedToMaxSize(MaxPush);
	switch (Result.Reaction)
	{
	case ERPGPowerReaction::Flinch:
		Shake(0.15f);
		break;
	case ERPGPowerReaction::Stagger:
		Push(Shove.IsNearlyZero() ? Dir * 200.f : Shove);
		Shake(0.3f);
		break;
	case ERPGPowerReaction::Knockback:
		Push(Dir * FMath::Max(KnockbackPush, float(Shove.Size())));
		Shake(0.5f);
		break;
	case ERPGPowerReaction::Catch:
		Push(Dir * FMath::Max(KnockbackPush, float(Shove.Size())));
		StartBrace(Dir, 0.85f, 0.6f, 0.5f);
		Shake(0.6f);
		break;
	default:   // Knockdown / Launch
	{
		const FVector FallVelocity = Result.DisplacementVelocity.IsNearlyZero() ? FVector(Dir * 300.f) : FVector(Result.DisplacementVelocity);
		Shake(Result.Reaction == ERPGPowerReaction::Launch ? 1.f : 0.8f);
		const bool bTriedToCatch = Result.bCatch && Result.Reaction == ERPGPowerReaction::Knockdown
			&& Result.ReactionLevel < URPGPowerSettings::Get()->KnockdownCertainLevel && !IsKnockedOut();
		if (bTriedToCatch && GetWorld())
		{
			// It tries to catch itself and falls while trying: stumbles with its arms out, then goes down.
			Push(Dir * KnockbackPush);
			StartBrace(Dir, 0.9f, CatchFailDelay + 0.2f, 0.3f);
			GetWorld()->GetTimerManager().SetTimer(CatchFailTimer, FTimerDelegate::CreateWeakLambda(this, [this, Dir, Point]()
			{
				const ACharacter* Character = GetCharacter();
				Fall((Character ? Character->GetVelocity() : FVector::ZeroVector) + Dir * 150.f, Point);
			}), CatchFailDelay, false);
		}
		else Fall(FallVelocity, Point);
		break;
	}
	}
}

void URPGHitOutcomeComponent::ReceiveMeleeHit(const FRPGMeleeHitInfo& Hit)
{
	FRPGPowerInput Input = URPGPowerSystem::MakeMeleeInput(Hit);
	PrepareInput(Input);
	FRPGPowerResult Result = URPGPowerSystem::Calculate(Input);
	if (URPGCharacterStatsComponent* Stats = URPGCharacterStatsComponent::FindStats(GetOwner()))
	{
		float Health = 0.f;
		if (Stats->GetStatByName(TEXT("Health"), Health)) Stats->SetStatBaseByName(TEXT("Health"), FMath::Max(0.f, Health - Result.Damage));
	}
	if (IsKnockedOut() && Result.Reaction < ERPGPowerReaction::Knockdown) Result.Reaction = ERPGPowerReaction::Knockdown;
	NoteHit(Result);
	bool bFromBehind = false;
	GetSituation(Input.StrikeDirection, &bFromBehind);
	bLastFromBehind = bFromBehind;
	UE_LOG(LogRPGInput, Log, TEXT("%s\n  (%s)"), *URPGPowerSystem::DescribeResult(Input, Result), *GetOwner()->GetActorNameOrLabel());
	ApplyResult(Result, Input.StrikeDirection, Hit.ImpactLocation, Hit.HitBone);
}

void URPGHitOutcomeComponent::ReceiveEnergyHit(const FRPGEnergyImpact& Impact)
{
	FRPGPowerInput Input = URPGPowerSystem::MakeEnergyInput(Impact, GetOwner());
	PrepareInput(Input);
	FRPGPowerResult Result = URPGPowerSystem::Calculate(Input);
	Result.Damage = Impact.Damage;
	if (URPGCharacterStatsComponent* Stats = URPGCharacterStatsComponent::FindStats(GetOwner()))
	{
		float Health = 0.f;
		if (Stats->GetStatByName(TEXT("Health"), Health)) Stats->SetStatBaseByName(TEXT("Health"), FMath::Max(0.f, Health - Result.Damage));
	}
	if (IsKnockedOut() && Result.Reaction < ERPGPowerReaction::Knockdown) Result.Reaction = ERPGPowerReaction::Knockdown;
	NoteHit(Result);
	bool bFromBehind = false;
	GetSituation(Input.StrikeDirection, &bFromBehind);
	bLastFromBehind = bFromBehind;
	ApplyResult(Result, Impact.Direction, Impact.Hit.ImpactPoint, Impact.Hit.BoneName);
}

void URPGHitOutcomeComponent::ReceiveBump(const FVector& Velocity, ERPGPowerReaction Reaction)
{
	NoteBump(Reaction);
	LastResult = FRPGPowerResult();
	LastResult.Reaction = Reaction;
	const FVector Dir = Velocity.GetSafeNormal2D();
	if (Reaction == ERPGPowerReaction::Knockback) { Push(Dir * FMath::Max(KnockbackPush, float(Velocity.Size2D()))); Shake(0.4f); }
	else if (Reaction == ERPGPowerReaction::Stagger) { Push(Velocity); Shake(0.2f); }
	else Push(Velocity * 0.5f);
}

void URPGHitOutcomeComponent::WriteBrace()
{
	const ACharacter* Character = GetCharacter();
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	URPGAnimInstance* Anim = Mesh ? Cast<URPGAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
	if (!Anim) return;
	Anim->Brace.Weight = BraceWeight;
	Anim->Brace.A = BraceA;
	Anim->Brace.B = BraceB;
	Anim->Brace.Alpha = BraceAlpha;
	Anim->Brace.Bend = Bend;
}

void URPGHitOutcomeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const UWorld* World = GetWorld();
	if (!World) return;
	const float Now = World->GetTimeSeconds();
	const URPGPowerSettings* S = URPGPowerSettings::Get();

	// Balance comes back a moment after the last hit.
	if (Now - LastHitTime > S->BalanceRefillDelay) Balance = FMath::Min(1.f, Balance + DeltaTime / S->BalanceRefillTime);

	const bool bHadPose = BraceWeight > 0.f || Bend > 0.f;
	URPGPhysicsReactionComponent* Reaction = GetReaction();
	const bool bLimp = Reaction && Reaction->GetReactionState() == ERPGPhysicsReactionState::Limp && !Reaction->IsReturning();
	if (bFallBrace)
	{
		// Falling: the arms keep reaching (the ragdoll's motors drive toward this pose) until the get-up begins.
		if (bLimp) BraceWeight = 1.f;
		else if (Now - FallStartTime > 0.5f) { bFallBrace = false; BraceWeight = BraceTarget = 0.f; }
	}
	else if (BraceHold > 0.f)
	{
		BraceHold -= DeltaTime;
		BraceWeight = FMath::FInterpTo(BraceWeight, BraceTarget, DeltaTime, 14.f);
	}
	else BraceWeight = FMath::Max(0.f, BraceWeight - DeltaTime / BraceFade);

	if (BendHold > 0.f) { BendHold -= DeltaTime; Bend = FMath::FInterpTo(Bend, BendTarget, DeltaTime, 16.f); }
	else Bend = FMath::Max(0.f, Bend - DeltaTime * FMath::Max(BendTarget, 1.f) / BendFade);

	PeakBrace = FMath::Max(PeakBrace, BraceWeight);
	PeakBend = FMath::Max(PeakBend, Bend);
	if (bHadPose || BraceWeight > 0.f || Bend > 0.f) WriteBrace();

	// Knocked out cold (owners without reactions of their own): back to full health once it is up again.
	if (bOutCold && !bLimp && Now - FallStartTime > 1.f && !Cast<ARPGNpcCharacter>(GetOwner()))
	{
		if (URPGCharacterStatsComponent* Stats = URPGCharacterStatsComponent::FindStats(GetOwner()))
		{
			const URPGAttributeSet* A = Stats->GetAttributes();
			if (A) Stats->SetStatBaseByName(TEXT("Health"), A->GetMaxHealth());
		}
		bOutCold = false;
	}
}
