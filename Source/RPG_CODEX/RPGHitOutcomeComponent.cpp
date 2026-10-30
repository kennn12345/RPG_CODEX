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
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

namespace
{
	/** GASP's ragdoll reach poses (UEFN mannequin): arms out toward the fall. Forward, back, left, right. */
	const TCHAR* ReachClipNames[] = { TEXT("M_ragdoll_reach_forward"), TEXT("M_ragdoll_reach_back"), TEXT("M_ragdoll_reach_left"), TEXT("M_ragdoll_reach_right") };

	/** The hand-keyed knee-down catches (Tools/ProtoAnim/kneecatch.py), in KneeClips order. */
	const TCHAR* KneeDirections[] = { TEXT("Fwd"), TEXT("Back") };
	const TCHAR* KneeOutcomes[] = { TEXT("Recover"), TEXT("Fall") };
	const TCHAR* KneeStrengths[] = { TEXT("Soft"), TEXT("Mid"), TEXT("Hard") };
	int32 KneeIndex(int32 Direction, int32 Outcome, int32 Strength) { return (Direction * 2 + Outcome) * 3 + Strength; }

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
	for (const TCHAR* Direction : KneeDirections)
		for (const TCHAR* Outcome : KneeOutcomes)
			for (const TCHAR* Strength : KneeStrengths)
			{
				const FString Name = FString::Printf(TEXT("RPG_Catch_Knee%s_%s_%s"), Direction, Outcome, Strength);
				KneeClips.Add(LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/RPG/Animations/SK_UEFN_Mannequin/%s.%s"), *Name, *Name)));
			}
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

float URPGHitOutcomeComponent::CatchIntensity(const FRPGPowerResult& Result, const FVector& FallDirection) const
{
	const URPGPowerSettings* S = URPGPowerSettings::Get();
	// Where the hit sits in the catch band (just knocked off balance .. nearly a straight knockdown)...
	const float Band = FMath::Max(S->KnockdownCertainLevel - S->KnockdownStartLevel, 0.1f);
	const float Force = (Result.ReactionLevel - S->KnockdownStartLevel) / Band;
	// ...plus the body's own momentum the same way (running into the fall carries it further).
	const ACharacter* Character = GetCharacter();
	const float Carried = Character ? FMath::Max(0.f, float(FVector::DotProduct(Character->GetVelocity(), FallDirection.GetSafeNormal2D()))) / 500.f : 0.f;
	return FMath::Clamp(Force + Carried, 0.f, 1.f);
}

UAnimMontage* URPGHitOutcomeComponent::TryKneeCatch(const FVector& FallDirection, bool bFalls, const FRPGPowerResult& Result)
{
	ACharacter* Character = GetCharacter();
	UAnimInstance* Anim = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (!Anim || KneeClips.Num() < 12 || TestKneeCatch < 0) return nullptr;
	const FVector Dir = FallDirection.GetSafeNormal2D();
	const float Along = float(FVector::DotProduct(Dir, Character->GetActorForwardVector().GetSafeNormal2D()));
	if (FMath::Abs(Along) < KneeCatchAlignment) return nullptr;
	if (TestKneeCatch == 0 && Result.HitZone != ERPGHitZone::Leg && FMath::FRand() >= KneeCatchShare) return nullptr;
	// The momentum chooses: soft / mid / hard by intensity thirds, and the rate within the third (0.94 .. 1.06).
	const float Intensity = CatchIntensity(Result, Dir);
	const int32 Strength = FMath::Clamp(int32(Intensity * 3.f), 0, 2);
	const float Within = Intensity * 3.f - float(Strength);
	const float Rate = FMath::Lerp(0.94f, 1.06f, FMath::Clamp(Within, 0.f, 1.f));
	const bool bForward = Along > 0.f;
	UAnimSequence* Clip = KneeClips[KneeIndex(bForward ? 0 : 1, bFalls ? 1 : 0, Strength)];
	if (!Clip) return nullptr;
	// The clip carries the body (root motion) and holds its own arms: no push, no brace overlay on top.
	BraceTarget = BraceWeight = 0.f; BraceHold = 0.f;
	UAnimMontage* Montage = Anim->PlaySlotAnimationAsDynamicMontage(Clip, TEXT("DefaultSlot"), 0.12f, bFalls ? 0.05f : 0.3f, Rate);
	if (!Montage) return nullptr;
	ActiveKneeCatch = Montage;
	++KneeCatchCount;
	LastKneeClip = Clip->GetFName();
	LastKneeRate = Rate;
	LastKneeIntensity = Intensity;
	// Square into the fall over KneeTurnTime: falling forward it faces the way it goes, falling back it faces away.
	KneeTurnFrom = float(Character->GetActorRotation().Yaw);
	KneeTurnTo = float((bForward ? Dir : -Dir).Rotation().Yaw);
	KneeTurnElapsed = 0.f;
	bHavePelvis = false;
	if (bFalls && GetWorld())
	{
		// It doesn't hold: as the clip runs out (tipping over / sitting back) the body goes down, still fighting it, with
		// the momentum its hips actually have then.
		GetWorld()->GetTimerManager().SetTimer(KneeFallTimer, FTimerDelegate::CreateWeakLambda(this, [this, Dir]()
		{
			ACharacter* Character = GetCharacter();
			if (!Character) return;
			++KneeFallCount;
			if (UAnimInstance* Anim = Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr) Anim->Montage_Stop(0.05f, ActiveKneeCatch);
			ActiveKneeCatch = nullptr;
			LastKneeFallVelocity = bHavePelvis ? PelvisVelocity + Dir * 60.f : Dir * 220.f;
			Fall(LastKneeFallVelocity, Character->GetMesh()->GetBoneLocation(TEXT("pelvis")));
		}), FMath::Max(0.05f, Clip->GetPlayLength() / Rate - 0.04f), false);
	}
	UE_LOG(LogRPGInput, Log, TEXT("%s: knee-down catch %s at %.2fx (intensity %.2f)"), *GetOwner()->GetActorNameOrLabel(), *Clip->GetName(), Rate, Intensity);
	return Montage;
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
		Shake(0.6f);
		if (TryKneeCatch(Dir, false, Result)) break;
		Push(Dir * FMath::Max(KnockbackPush, float(Shove.Size())));
		StartBrace(Dir, 0.85f, 0.6f, 0.5f);
		break;
	default:   // Knockdown / Launch
	{
		const FVector FallVelocity = Result.DisplacementVelocity.IsNearlyZero() ? FVector(Dir * 300.f) : FVector(Result.DisplacementVelocity);
		Shake(Result.Reaction == ERPGPowerReaction::Launch ? 1.f : 0.8f);
		const bool bTriedToCatch = Result.bCatch && Result.Reaction == ERPGPowerReaction::Knockdown
			&& Result.ReactionLevel < URPGPowerSettings::Get()->KnockdownCertainLevel && !IsKnockedOut();
		if (bTriedToCatch && TryKneeCatch(Dir, true, Result)) break;
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

	// A frame that took far too long while this body is reacting: say what it was in the middle of (the 2026-10-29 play
	// session slowed to ~17 s a frame after a push; this names the state if it ever happens again).
	const URPGPhysicsReactionComponent* SlowR = GetReaction();
	const bool bReacting = (SlowR && SlowR->GetReactionState() != ERPGPhysicsReactionState::Animated) || BraceWeight > 0.f || Bend > 0.f
		|| bFallBrace || ActiveKneeCatch != nullptr || Now - LastHitTime < 2.f;
	if (FApp::GetDeltaTime() > 0.3 && bReacting)
	{
		const URPGPhysicsReactionComponent* R = SlowR;
		const AActor* Owner = GetOwner();
		UE_LOG(LogRPGInput, Warning, TEXT("SLOW FRAME %.2f s: %s reaction %d returning %d braced %d brace %.2f bend %.1f fall-brace %d knee %s at %s vel %s"),
			FApp::GetDeltaTime(), *Owner->GetActorNameOrLabel(), R ? int32(R->GetReactionState()) : -1, R && R->IsReturning(), R && R->IsRagdollBraced(),
			BraceWeight, Bend, bFallBrace, ActiveKneeCatch ? *LastKneeClip.ToString() : TEXT("-"), *Owner->GetActorLocation().ToCompactString(),
			*Owner->GetVelocity().ToCompactString());
	}

	// A knee-down catch: turning square into the fall, and the hips' velocity (what a failed catch falls with).
	if (ActiveKneeCatch)
	{
		if (ACharacter* Character = GetCharacter())
		{
			if (KneeTurnElapsed < KneeTurnTime)
			{
				KneeTurnElapsed = FMath::Min(KneeTurnElapsed + DeltaTime, KneeTurnTime);
				const float A = FMath::SmoothStep(0.f, 1.f, KneeTurnElapsed / KneeTurnTime);
				FRotator R = Character->GetActorRotation();
				R.Yaw = KneeTurnFrom + FMath::FindDeltaAngleDegrees(KneeTurnFrom, KneeTurnTo) * A;
				Character->SetActorRotation(R);
			}
			const FVector Pelvis = Character->GetMesh()->GetBoneLocation(TEXT("pelvis"));
			if (bHavePelvis && DeltaTime > 0.f) PelvisVelocity = FMath::Lerp(PelvisVelocity, (Pelvis - LastPelvis) / DeltaTime, 0.5f);
			LastPelvis = Pelvis; bHavePelvis = true;
		}
		if (const UAnimInstance* Anim = GetCharacter() && GetCharacter()->GetMesh() ? GetCharacter()->GetMesh()->GetAnimInstance() : nullptr)
			if (!Anim->Montage_IsActive(ActiveKneeCatch)) ActiveKneeCatch = nullptr;
	}

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
