#include "RPGNpcCharacter.h"

#include "RPGAttributeSet.h"
#include "RPGBodyMass.h"
#include "RPGHitReactionComponent.h"
#include "RPGBodyImpactComponent.h"
#include "RPGCharacterStatsComponent.h"
#include "RPGCombatStateComponent.h"
#include "RPGHitCameraShake.h"
#include "RPGNpcAIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "RPGPhysicsReactionComponent.h"
#include "RPGPowerSettings.h"
#include "RPGPowerSystem.h"
#include "RPG_CODEX.h"
#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace
{
	bool ConsoleFlag(const TCHAR* Name, bool bDefault)
	{
		const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name);
		return Var ? Var->GetInt() > 0 : bDefault;
	}
	const FName ReactionSlot(TEXT("DefaultSlot"));
}

ARPGNpcCharacter::ARPGNpcCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	AIControllerClass = ARPGNpcAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	Stats = CreateDefaultSubobject<URPGCharacterStatsComponent>(TEXT("Stats"));
	PhysicsReaction = CreateDefaultSubobject<URPGPhysicsReactionComponent>(TEXT("PhysicsReaction"));
	HitReaction = CreateDefaultSubobject<URPGHitReactionComponent>(TEXT("HitReaction"));
	// Physics bodies only for knockdowns / knockouts (whole-body ragdoll); ordinary hits use HitReaction.
	PhysicsReaction->bHitReactions = false;
	// Realistic give, not flailing: legs stay on the animation (leg hits step instead), firm muscles, the struck part
	// only half as weak, physics never more than half the pose, a gentle stagger wobble and a smooth recovery.
	PhysicsReaction->bKinematicLegsWhenPowered = true;
	PhysicsReaction->MotorStrength = 70.f;
	PhysicsReaction->StruckLimbStrengthScale = 0.5f;
	PhysicsReaction->PoweredMaxBlend = 0.5f;
	PhysicsReaction->StaggerMotorScale = 0.85f;
	PhysicsReaction->ReturnBlendTime = 0.45f;

	BodyImpact = CreateDefaultSubobject<URPGBodyImpactComponent>(TEXT("BodyImpact"));
}

void ARPGNpcCharacter::BeginPlay()
{
	Super::BeginPlay();
	Home = GetActorLocation();
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		// GASP's motion matching reads acceleration: path following must accelerate like a player, not set velocity.
		Move->GetNavMovementProperties()->bUseAccelerationForPaths = true;
		// This actor ticks first (a push sets this frame's velocity before movement runs).
		Move->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick);
		Move->bRequestedMoveUseAcceleration = true;
	}
	if (USkeletalMeshComponent* Body = GetMesh())
	{
		// GASP's character mesh ignores visibility traces, so spells (and other traces) only found the capsule and passed
		// through. The body takes them: they hit the actual bone.
		Body->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		// Realistic weight for every body part (the baseline for momentum and hit reactions).
		RPGBodyMass::Apply(Body, RPGBodyMass::TotalKgFor(BodyWeightLbs));
	}
	if (PhysicsReaction) PhysicsReaction->OnLimpEnding.AddUObject(this, &ARPGNpcCharacter::HandleLimpEnding);
	if (BodyImpact) BodyImpact->OnKnockedDown.AddUObject(this, &ARPGNpcCharacter::OnRagdoll);   // run into hard: down like a knockdown
}

void ARPGNpcCharacter::ApplyWalkIntent()
{
	// GASP's character keeps its input intent in a Blueprint struct (fields with generated suffixes).
	FStructProperty* InputState = FindFProperty<FStructProperty>(GetClass(), TEXT("CharacterInputState"));
	if (!InputState) return;
	for (TFieldIterator<FBoolProperty> It(InputState->Struct); It; ++It)
	{
		if (It->GetName().StartsWith(TEXT("WantsToWalk_")))
			It->SetPropertyValue_InContainer(InputState->ContainerPtrToValuePtr<void>(this), bWalk);
		// Pushed: keep facing (strafe) so the body steps back / sideways instead of turning to walk away.
		if (It->GetName().StartsWith(TEXT("WantsToStrafe_")))
			It->SetPropertyValue_InContainer(InputState->ContainerPtrToValuePtr<void>(this), bShoving);
	}
}

void ARPGNpcCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bShoving)
	{
		// The push carries the body, slowing at ShoveBraking; motion matching steps it along. Ends when spent.
		ShoveVelocity = ShoveVelocity.GetSafeNormal() * FMath::Max(0.f, float(ShoveVelocity.Size()) - ShoveBraking * DeltaTime);
		if (UCharacterMovementComponent* Move = GetCharacterMovement(); Move && Move->IsMovingOnGround())
			Move->Velocity = FVector(ShoveVelocity.X, ShoveVelocity.Y, Move->Velocity.Z);
		if (ShoveVelocity.IsNearlyZero(1.f) || GetWorld()->GetTimeSeconds() >= ShoveUntil) { bShoving = false; ShoveVelocity = FVector::ZeroVector; }
	}
	ApplyWalkIntent();

	// A reaction step ended: back to full root motion.
	if (ActiveReaction)
	{
		const UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
		if (!Anim || !Anim->Montage_IsPlaying(ActiveReaction))
		{
			ActiveReaction = nullptr;
			SetAnimRootMotionTranslationScale(1.f);
		}
	}

	if (URPGCombatStateComponent::IsDebugDisplayEnabled() || !DisplayLabel.IsEmpty())
	{
		const URPGAttributeSet* A = Stats ? Stats->GetAttributes() : nullptr;
		FString Text = DisplayLabel.IsEmpty() ? GetActorNameOrLabel() : DisplayLabel;
		if (A) Text += FString::Printf(TEXT("\nHealth %.0f / %.0f%s"), A->GetHealth(), A->GetMaxHealth(), bKnockedOut ? TEXT("  KO") : TEXT(""));
		DrawDebugString(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 110.f), Text, nullptr, FColor::White, 0.f, true);
	}
}

bool ARPGNpcCharacter::IsBusy() const
{
	const UWorld* World = GetWorld();
	return bDown || ActiveReaction != nullptr || (World && World->GetTimeSeconds() < BusyUntil)
		|| (PhysicsReaction && PhysicsReaction->GetReactionState() == ERPGPhysicsReactionState::Limp);
}

void ARPGNpcCharacter::StopForReaction(float Seconds)
{
	if (const UWorld* World = GetWorld()) BusyUntil = FMath::Max(BusyUntil, World->GetTimeSeconds() + Seconds);
	// Stop walking but keep the momentum: the body slows to a stop under its own braking instead of halting in one
	// frame (which snapped the motion-matched pose from walking to idle right as the hit landed).
	if (const AAIController* AI = Cast<AAIController>(GetController()))
		if (UPathFollowingComponent* Path = AI->GetPathFollowingComponent())
			Path->AbortMove(*this, FPathFollowingResultFlags::UserAbort, FAIRequestID::CurrentRequest, EPathFollowingVelocityMode::Keep);
}

void ARPGNpcCharacter::ReceiveMeleeHit_Implementation(const FRPGMeleeHitInfo& Hit)
{
	++HitCount;
	ReceivePowerHit(Hit);
}

void ARPGNpcCharacter::ReceivePowerHit(const FRPGMeleeHitInfo& Hit)
{
	const FRPGPowerInput Input = URPGPowerSystem::MakeMeleeInput(Hit);
	FRPGPowerResult Result = URPGPowerSystem::Calculate(Input);

	// Damage -> Health (GAS). Reaching 0 knocks it out: it goes down whatever the hit's own reaction was.
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
	StopForReaction(HitPause);

	// The body: give on the springs at the struck bone, or a whole-body ragdoll for knockdowns / launches.
	if (PhysicsReaction) PhysicsReaction->HandlePowerHit(Hit, Result);
	if (PhysicsReaction && PhysicsReaction->GetReactionState() == ERPGPhysicsReactionState::Limp) OnRagdoll();
	else ReactToHit(Hit.HitBone, Hit.ImpactLocation, Hit.StrikeDirection.IsNearlyZero() && Hit.Attacker ? GetActorLocation() - Hit.Attacker->GetActorLocation() : Hit.StrikeDirection,
		Result.Reaction == ERPGPowerReaction::Stagger ? HitSpeedStagger : HitSpeedFlinch, Hit.Attacker);
	if (!bDown)
	{
		// The push: the Power System's displacement velocity, plus the attacker's momentum over this body's weight.
		FVector Push = FVector(Result.DisplacementVelocity) * ShoveScale;
		if (Hit.Attacker) Push += Hit.Attacker->GetVelocity() * MomentumTransfer * RPGBodyMass::TotalKgFor(0.f) / RPGBodyMass::TotalKgFor(BodyWeightLbs);
		Shove(Push);
	}
	PlayImpactFeel(Hit.Attacker, Result.Reaction);

	const URPGAttributeSet* A = Stats ? Stats->GetAttributes() : nullptr;
	UE_LOG(LogRPGInput, Log, TEXT("%s\n  %s Health %.0f / %.0f%s"), *URPGPowerSystem::DescribeResult(Input, Result),
		*(DisplayLabel.IsEmpty() ? GetActorNameOrLabel() : DisplayLabel), A ? A->GetHealth() : 0.f, A ? A->GetMaxHealth() : 0.f,
		bKnockedOut ? TEXT("  KNOCKED OUT") : TEXT(""));
}

void ARPGNpcCharacter::ReceiveEnergyHit_Implementation(const FRPGEnergyImpact& Impact)
{
	++HitCount;
	StopForReaction(HitPause + Impact.Charge);
	UAbilitySystemComponent* AbilitySystem = Stats ? Stats->GetAbilitySystem() : nullptr;
	if (AbilitySystem && Stats->GetAttributes())
	{
		const float Health = AbilitySystem->GetNumericAttribute(URPGAttributeSet::GetHealthAttribute());
		AbilitySystem->SetNumericAttributeBase(URPGAttributeSet::GetHealthAttribute(), FMath::Max(0.f, Health - Impact.Damage));
		if (Health <= Impact.Damage) bKnockedOut = true;
	}
	if (!PhysicsReaction) return;
	if (bKnockedOut)
	{
		PhysicsReaction->EnterFullRagdoll(2.f);
		PhysicsReaction->HandleEnergyHit(Impact);
		OnRagdoll();
		return;
	}
	// The struck part takes the spell's force at the point it hit, the way it was travelling (capped by that part's weight).
	{
		const FBodyInstance* Body = GetMesh() ? GetMesh()->GetBodyInstance(Impact.Hit.BoneName) : nullptr;
		const float Mass = Body ? FMath::Max(0.5f, Body->GetBodyMass()) : 5.f;
		ReactToHit(Impact.Hit.BoneName, Impact.Hit.ImpactPoint, Impact.Direction, FMath::Lerp(180.f, 450.f, FMath::Clamp(Impact.Charge, 0.f, 1.f)), Impact.Attacker,
			Impact.Impulse > 0.f ? FMath::Min(Impact.Impulse, Mass * 450.f) : -1.f);
	}
	// The push on the whole body: the spell's impulse along its travel, plus the caster's momentum.
	FVector Push = Impact.Direction.GetSafeNormal() * Impact.Impulse * EnergyShovePerImpulse;
	if (Impact.Attacker) Push += Impact.Attacker->GetVelocity() * MomentumTransfer * RPGBodyMass::TotalKgFor(0.f) / RPGBodyMass::TotalKgFor(BodyWeightLbs);
	Shove(Push);
}

void ARPGNpcCharacter::Shove(FVector Velocity)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const UWorld* World = GetWorld();
	Velocity.Z = 0.f;
	if (bDown || !Move || !World || Velocity.Size() < MinShove) return;
	Velocity = Velocity.GetClampedToMaxSize(MaxShove);
	// The body moves; with no walking input it brakes to a stop, and motion matching picks the steps for that motion
	// (the size, angle and speed of every push differ, so do the steps). It keeps facing the way it faced (strafing).
	// (GASP rewrites braking and friction every frame, so the push is driven here, before movement, each frame - see Tick.)
	ShoveVelocity = (ShoveVelocity + Velocity).GetClampedToMaxSize(MaxShove);
	bShoving = true;
	ShoveUntil = World->GetTimeSeconds() + ShoveTime;
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		AI->ClearFocus(EAIFocusPriority::Gameplay);
		AI->SetControlRotation(GetActorRotation());
	}
	StopForReaction(ShoveTime + 0.3f);
	UE_LOG(LogRPGInput, Log, TEXT("%s pushed %.0f cm/s (%.0f deg from facing)"), *GetActorNameOrLabel(), Velocity.Size(),
		FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(float(FVector::DotProduct(Velocity.GetSafeNormal2D(), GetActorForwardVector())), -1.f, 1.f))));
}

void ARPGNpcCharacter::ReactToHit(FName Bone, const FVector& Point, const FVector& Direction, float Speed, const AActor* Attacker, float MaxImpulse)
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!HitReaction || !Body) return;
	// The struck part's weight (the nearest bone with a physics body).
	float Mass = 5.f;
	for (FName B = Bone; B != NAME_None; B = Body->GetParentBone(B))
		if (const FBodyInstance* Instance = Body->GetBodyInstance(B)) { Mass = Instance->GetBodyMass(); break; }
	FVector Impulse = Direction.GetSafeNormal() * Speed * Mass;
	if (MaxImpulse > 0.f) Impulse = Impulse.GetClampedToMaxSize(MaxImpulse);
	// Momentum: an attacker moving into the blow carries some of their weight x speed into it.
	if (Attacker) Impulse += Attacker->GetVelocity() * RPGBodyMass::TotalKgFor(0.f) * MomentumTransfer;
	HitReaction->AddHit(Bone, Point, Impulse);
}

void ARPGNpcCharacter::PlayImpactFeel(AActor* Attacker, ERPGPowerReaction Reaction)
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
	// Hit-stop on the attacker (and on this body only when it ragdolls: slowed, its reaction springs would stop steering).
	if (StopTime > 0.f && ConsoleFlag(TEXT("rpg.HitStop"), true))
	{
		if (AActor* Previous = HitStopAttacker.Get()) Previous->CustomTimeDilation = 1.f;
		HitStopAttacker = Attacker;
		if (PhysicsReaction && PhysicsReaction->GetReactionState() == ERPGPhysicsReactionState::Limp) CustomTimeDilation = S->HitStopTimeDilation;
		if (Attacker) Attacker->CustomTimeDilation = S->HitStopTimeDilation;
		GetWorldTimerManager().SetTimer(HitStopTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			CustomTimeDilation = 1.f;
			if (AActor* Held = HitStopAttacker.Get()) Held->CustomTimeDilation = 1.f;
			HitStopAttacker = nullptr;
		}), StopTime, false);
	}
	const APawn* Pawn = Cast<APawn>(Attacker);
	if (APlayerController* AttackerController = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr)
		if (Shake > 0.f) AttackerController->ClientStartCameraShake(URPGHitCameraShake::StaticClass(), Shake);
}

void ARPGNpcCharacter::OnRagdoll()
{
	// Down: no walking or stepping while the body is physical.
	bDown = true;
	bShoving = false;
	ShoveVelocity = FVector::ZeroVector;
	if (UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
		if (ActiveReaction) Anim->Montage_Stop(0.1f, ActiveReaction);
	ActiveReaction = nullptr;
	SetAnimRootMotionTranslationScale(1.f);
	if (AController* C = GetController()) C->StopMovement();
	if (UCharacterMovementComponent* Move = GetCharacterMovement()) { Move->StopMovementImmediately(); Move->DisableMovement(); }
}

void ARPGNpcCharacter::HandleLimpEnding()
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!Body) return;
	if (!bDown) OnRagdoll();

	// Stand up where the body lies (the shared get-up, chosen by how the body lies). Hurt bodies get up slower.
	const URPGAttributeSet* A = Stats ? Stats->GetAttributes() : nullptr;
	const float HealthLeft = bKnockedOut || !A || A->GetMaxHealth() <= 0.f ? 0.f : FMath::Clamp(A->GetHealth() / A->GetMaxHealth(), 0.f, 1.f);
	float Duration = 1.f;
	// Now and then it scrambles straight up and off (a wandering NPC that wants to get moving).
	ActiveReaction = URPGBodyImpactComponent::StandUp(this, FMath::Lerp(GetUpHurtRate, GetUpHealthyRate, HealthLeft), Duration, bWander && FMath::FRand() < 0.3f);
	if (Duration <= 0.f) Duration = 1.f;
	bDown = false;
	StopForReaction(Duration + 0.5f);
	if (bKnockedOut)
	{
		if (UAbilitySystemComponent* AbilitySystem = Stats ? Stats->GetAbilitySystem() : nullptr)
			if (A)
				AbilitySystem->SetNumericAttributeBase(URPGAttributeSet::GetHealthAttribute(), A->GetMaxHealth());
		bKnockedOut = false;
	}
}
