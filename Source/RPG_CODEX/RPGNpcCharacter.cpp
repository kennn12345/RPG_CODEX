#include "RPGNpcCharacter.h"

#include "RPGAttributeSet.h"
#include "RPGBodyMass.h"
#include "RPGHitReactionComponent.h"
#include "RPGReactivePhysicsComponent.h"
#include "RPGBodyImpactComponent.h"
#include "RPGHitOutcomeComponent.h"
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
	ReactivePhysics = CreateDefaultSubobject<URPGReactivePhysicsComponent>(TEXT("ReactivePhysics"));
	// Legacy PhysicsControl owns whole-body ragdoll; ReactivePhysics borrows upper-body modifiers for ordinary hits.
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
	HitOutcome = CreateDefaultSubobject<URPGHitOutcomeComponent>(TEXT("HitOutcome"));
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
		// Movement normally installs the opposite dependency; explicitly reverse it rather than forming a cycle.
		Move->bTickBeforeOwner = false;
		PrimaryActorTick.RemovePrerequisite(Move, Move->PrimaryComponentTick);
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
	if (PhysicsReaction) PhysicsReaction->OnLimpStarted.AddUObject(this, &ARPGNpcCharacter::OnRagdoll);
	if (BodyImpact) BodyImpact->OnKnockedDown.AddUObject(this, &ARPGNpcCharacter::OnRagdoll);   // flown into: down like a knockdown
	if (BodyImpact) BodyImpact->OnBumped.AddUObject(this, &ARPGNpcCharacter::ReceiveBump);     // run into: nudge / stagger / knockback
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
	UpdatePushObstacles();
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
	FRPGPowerInput Input = URPGPowerSystem::MakeMeleeInput(Hit);
	if (HitOutcome) HitOutcome->PrepareInput(Input);
	FRPGPowerResult Result = URPGPowerSystem::Calculate(Input);

	// Damage -> Health (GAS). Reaching 0 knocks it out: it goes down whatever the hit's own reaction was.
	UAbilitySystemComponent* AbilitySystem = Stats ? Stats->GetAbilitySystem() : nullptr;
	if (AbilitySystem && Stats->GetAttributes())
	{
		const float Health = AbilitySystem->GetNumericAttribute(URPGAttributeSet::GetHealthAttribute());
		AbilitySystem->SetNumericAttributeBase(URPGAttributeSet::GetHealthAttribute(), Health - Result.Damage);
		if (Health - Result.Damage <= 0.f && !bKnockedOut) bKnockedOut = true;
	}
	React(Hit, Input, Result, nullptr);
	PlayImpactFeel(Hit.Attacker, Result.Reaction);
}

void ARPGNpcCharacter::ReceiveEnergyHit_Implementation(const FRPGEnergyImpact& Impact)
{
	++HitCount;
	// The Power System decides the reaction from the charge (a tap staggers, half a charge knocks back, a full charge
	// knocks it off balance); the spell keeps its own damage.
	FRPGPowerInput Input = URPGPowerSystem::MakeEnergyInput(Impact, this);
	if (HitOutcome) HitOutcome->PrepareInput(Input);
	FRPGPowerResult Result = URPGPowerSystem::Calculate(Input);
	Result.Damage = Impact.Damage;
	UAbilitySystemComponent* AbilitySystem = Stats ? Stats->GetAbilitySystem() : nullptr;
	if (AbilitySystem && Stats->GetAttributes())
	{
		const float Health = AbilitySystem->GetNumericAttribute(URPGAttributeSet::GetHealthAttribute());
		Result.bWouldKill = Health > 0.f && Impact.Damage >= Health;
		AbilitySystem->SetNumericAttributeBase(URPGAttributeSet::GetHealthAttribute(), FMath::Max(0.f, Health - Impact.Damage));
		if (Health <= Impact.Damage) bKnockedOut = true;
	}
	FRPGMeleeHitInfo Hit;
	Hit.AttackName = TEXT("Energy Blast");
	Hit.Attacker = Impact.Attacker;
	Hit.HitActor = this;
	Hit.HitBone = Impact.Hit.BoneName;
	Hit.ImpactLocation = Impact.Hit.ImpactPoint;
	Hit.StrikeDirection = Impact.Direction;
	React(Hit, Input, Result, &Impact);
}

void ARPGNpcCharacter::React(const FRPGMeleeHitInfo& Hit, const FRPGPowerInput& Input, FRPGPowerResult Result, const FRPGEnergyImpact* Energy)
{
	const URPGPowerSettings* S = URPGPowerSettings::Get();
	FVector Dir = Hit.StrikeDirection.IsNearlyZero() && Hit.Attacker ? GetActorLocation() - Hit.Attacker->GetActorLocation() : FVector(Hit.StrikeDirection);
	Dir = Dir.GetSafeNormal2D().IsNearlyZero() ? -GetActorForwardVector() : Dir.GetSafeNormal2D();
	const bool bOutCold = bKnockedOut || (HitOutcome && HitOutcome->IsKnockedOut());
	const bool bWasDown = bDown || (PhysicsReaction && PhysicsReaction->GetReactionState() == ERPGPhysicsReactionState::Limp);
	// Getting up: anything that would move it a few steps puts it straight back down.
	const bool bGettingUp = !bWasDown && ActiveReaction != nullptr;
	if (bOutCold && Result.Reaction < ERPGPowerReaction::Knockdown) Result.Reaction = ERPGPowerReaction::Knockdown;
	if (bGettingUp && Result.Reaction >= ERPGPowerReaction::Knockback && Result.Reaction < ERPGPowerReaction::Knockdown) Result.Reaction = ERPGPowerReaction::Knockdown;
	if (HitOutcome) HitOutcome->NoteHit(Result);
	LastPowerResult = Result;
	StopForReaction(HitPause + (Energy ? Energy->Charge : 0.f));
	const URPGAttributeSet* A = Stats ? Stats->GetAttributes() : nullptr;
	UE_LOG(LogRPGInput, Log, TEXT("%s\n  %s Health %.0f / %.0f%s%s"), *URPGPowerSystem::DescribeResult(Input, Result),
		*(DisplayLabel.IsEmpty() ? GetActorNameOrLabel() : DisplayLabel), A ? A->GetHealth() : 0.f, A ? A->GetMaxHealth() : 0.f,
		bOutCold ? TEXT("  KNOCKED OUT") : TEXT(""), bWasDown ? TEXT("  (hit while down)") : TEXT(""));
	if (!PhysicsReaction) return;

	// On the ground it can still be hit: the blow goes into the lying body (a gentler slide than standing) and keeps it
	// down a little longer; knocked out by it, the last of its muscle goes.
	if (bWasDown)
	{
		Result.DisplacementVelocity *= DownedPushScale;
		PhysicsReaction->HandlePowerHit(Hit, Result);
		if (Energy) PhysicsReaction->HandleEnergyHit(*Energy);
		if (bOutCold && HitOutcome && PhysicsReaction->IsRagdollBraced()) HitOutcome->BeginFall(Dir, true);
		++DownedHitCount;
		OnRagdoll();
		return;
	}

	const bool bFalls = Result.Reaction == ERPGPowerReaction::Knockdown || Result.Reaction == ERPGPowerReaction::Launch;
	// Knocked off balance and not quite saved: it tries to catch itself and falls while trying.
	const bool bFallsCatching = bFalls && Result.bCatch && Result.Reaction == ERPGPowerReaction::Knockdown
		&& Result.ReactionLevel < S->KnockdownCertainLevel && !bOutCold && !bGettingUp;
	if (bFalls && !bFallsCatching)
	{
		// Down: a whole-body ragdoll that fights the fall (arms toward the ground, head held) unless it is out cold.
		if (HitOutcome) HitOutcome->BeginFall(Result.DisplacementVelocity.IsNearlyZero() ? Dir : FVector(Result.DisplacementVelocity), bOutCold);
		PhysicsReaction->HandlePowerHit(Hit, Result);
		if (PhysicsReaction->GetReactionState() != ERPGPhysicsReactionState::Limp) PhysicsReaction->EnterFullRagdoll(2.f);
		if (Energy) PhysicsReaction->HandleEnergyHit(*Energy);
		OnRagdoll();
		return;
	}

	// On its feet: the struck part gives (the head snaps back harder), a body shot doubles it over.
	float Speed = Result.Reaction == ERPGPowerReaction::Flinch ? HitSpeedFlinch : HitSpeedStagger;
	if (Result.HitZone == ERPGHitZone::Head) Speed *= HeadSnapScale;
	float MaxImpulse = -1.f;
	if (Energy)
	{
		const FBodyInstance* Body = GetMesh() ? GetMesh()->GetBodyInstance(Energy->Hit.BoneName) : nullptr;
		const float Mass = Body ? FMath::Max(0.5f, Body->GetBodyMass()) : 5.f;
		Speed = FMath::Lerp(180.f, 450.f, FMath::Clamp(Energy->Charge, 0.f, 1.f)) * (Result.HitZone == ERPGHitZone::Head ? HeadSnapScale : 1.f);
		if (Energy->Impulse > 0.f) MaxImpulse = FMath::Min(Energy->Impulse, Mass * 450.f);
	}
	ReactToHit(Hit.HitBone, Hit.ImpactLocation, Dir, Speed, Energy ? nullptr : Hit.Attacker.Get(), MaxImpulse);
	const FString Bone = Hit.HitBone.ToString().ToLower();
	if (HitOutcome && Result.Reaction != ERPGPowerReaction::Flinch && (Bone == TEXT("pelvis") || Bone == TEXT("spine_01") || Bone == TEXT("spine_02") || Bone == TEXT("spine_03")))
		HitOutcome->StartBend(Result.Reaction >= ERPGPowerReaction::Knockback ? 28.f : 18.f, 0.25f, 0.6f);
	// Knocked off balance: stumbling steps with the arms thrown out toward the fall.
	const bool bCatching = Result.Reaction == ERPGPowerReaction::Catch || bFallsCatching;
	// Falling straight forward or back it may go down onto a knee (the hand-keyed clips; root motion carries it, and a
	// failed one hands the body to the braced ragdoll as the clip ends).
	if (UAnimMontage* Knee = bCatching && HitOutcome ? HitOutcome->TryKneeCatch(Dir, bFallsCatching, Result) : nullptr)
	{
		bShoving = false;
		ShoveVelocity = FVector::ZeroVector;
		ActiveReaction = Knee;
		SetAnimRootMotionTranslationScale(1.f);
		StopForReaction(Knee->GetPlayLength() + 0.3f);
		return;
	}
	if (HitOutcome && bCatching) HitOutcome->StartBrace(Dir, 0.85f, 0.7f, 0.5f);

	// The push: the Power System's displacement velocity, plus the attacker's momentum over this body's weight.
	FVector Push = FVector(Result.DisplacementVelocity) * ShoveScale;
	if (Energy && Push.IsNearlyZero()) Push = Dir * Energy->Impulse * EnergyShovePerImpulse;
	if (Hit.Attacker) Push += Hit.Attacker->GetVelocity() * MomentumTransfer * RPGBodyMass::TotalKgFor(0.f) / RPGBodyMass::TotalKgFor(BodyWeightLbs);
	if (Push.IsNearlyZero()) Push = Dir;
	Shove(Push, Result.Reaction == ERPGPowerReaction::Knockback || bCatching);

	if (bFallsCatching)
		GetWorldTimerManager().SetTimer(CatchFailTimer, FTimerDelegate::CreateWeakLambda(this, [this, Dir, Point = Hit.ImpactLocation]()
		{
			if (bDown || !PhysicsReaction) return;
			// The catch fails: down it goes, still fighting it.
			++CatchFallCount;
			if (HitOutcome) HitOutcome->BeginFall(Dir, false);
			PhysicsReaction->Launch(FVector(GetVelocity().X, GetVelocity().Y, 0.f) + Dir * 150.f, Point, PhysicsReaction->LimpTime);
		}), CatchFailDelay, false);
}

void ARPGNpcCharacter::ReceiveBump(const FVector& Velocity, const FVector& Point, ERPGPowerReaction Reaction)
{
	if (bDown) return;
	if (HitOutcome) HitOutcome->NoteBump(Reaction);
	// Run into: the torso gives (a nudge barely, a stagger more) and the body takes its share of the momentum on its
	// feet - a step for a nudge, a stumble for a stagger, several steps back for a knockback. Never a fall.
	const bool bHard = Reaction != ERPGPowerReaction::Flinch;
	StopForReaction(bHard ? HitPause : 0.4f);
	ReactToHit(TEXT("spine_03"), Point, Velocity, bHard ? HitSpeedStagger : HitSpeedFlinch * 0.5f, nullptr);
	Shove(Velocity, Reaction == ERPGPowerReaction::Knockback);
	UE_LOG(LogRPGInput, Log, TEXT("%s bumped: %s at %.0f cm/s"), *GetActorNameOrLabel(), *UEnum::GetDisplayValueAsText(Reaction).ToString(), Velocity.Size2D());
}

void ARPGNpcCharacter::UpdatePushObstacles()
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const UWorld* World = GetWorld();
	if (!bShoving || !Move || !World || World->GetTimeSeconds() - ShoveStartTime < 0.08f) return;
	const FVector Dir = ShoveVelocity.GetSafeNormal2D();
	const float Speed = float(ShoveVelocity.Size2D());
	// Driven off a ledge: it goes over, a braced fall.
	if (Move->IsFalling())
	{
		UE_LOG(LogRPGInput, Log, TEXT("%s pushed off an edge at %.0f cm/s"), *GetActorNameOrLabel(), Speed);
		bShoving = false;
		++LedgeFallCount;
		if (HitOutcome) HitOutcome->BeginFall(Dir, bKnockedOut);
		if (PhysicsReaction) PhysicsReaction->Launch(FVector(ShoveVelocity.X, ShoveVelocity.Y, Move->Velocity.Z), GetActorLocation(), PhysicsReaction->LimpTime, true);
		ShoveVelocity = FVector::ZeroVector;
		return;
	}
	// Driven into a wall (the push is going nowhere): it slams into it, the torso gives off the wall, a hand braces on it.
	if (Speed < WallSlamSpeed || float(FVector::DotProduct(GetVelocity(), Dir)) > Speed * 0.35f) return;
	FHitResult Wall;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RPGPushWall), false, this);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	const float Reach = GetCapsuleComponent()->GetScaledCapsuleRadius() + 35.f;
	const FVector Chest = GetActorLocation() + FVector(0.f, 0.f, 40.f);
	if (!World->LineTraceSingleByObjectType(Wall, Chest, Chest + Dir * Reach, Objects, Params) || Cast<APawn>(Wall.GetActor())) return;
	UE_LOG(LogRPGInput, Log, TEXT("%s slammed into %s at %.0f cm/s"), *GetActorNameOrLabel(), *GetNameSafe(Wall.GetActor()), Speed);
	bShoving = false;
	ShoveVelocity = FVector::ZeroVector;
	Move->Velocity = FVector(0.f, 0.f, Move->Velocity.Z);
	++WallSlamCount;
	ReactToHit(TEXT("spine_05"), Wall.ImpactPoint, Wall.ImpactNormal, HitSpeedStagger * FMath::Clamp(Speed / 400.f, 0.7f, 1.5f), nullptr);
	if (HitOutcome)
	{
		HitOutcome->StartBrace(-Wall.ImpactNormal, 0.75f, 0.5f, 0.6f);
		HitOutcome->NoteBump(ERPGPowerReaction::Stagger);
	}
	URPGHitOutcomeComponent::PlayImpactSound(this, Wall.ImpactPoint, 0.6f);
	StopForReaction(HitPause);
}

void ARPGNpcCharacter::Shove(FVector Velocity, bool bKnockback)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const UWorld* World = GetWorld();
	Velocity.Z = 0.f;
	if (bDown || !Move || !World || Velocity.Size() < (bKnockback ? KINDA_SMALL_NUMBER : MinShove)) return;
	// A knockback always drives it back several steps; anything else is capped at an ordinary stagger's push.
	const float Max = bKnockback ? KnockbackMaxShove : MaxShove;
	Velocity = bKnockback ? Velocity.GetSafeNormal() * FMath::Clamp(float(Velocity.Size()), KnockbackMinShove, Max) : Velocity.GetClampedToMaxSize(Max);
	const float Time = bKnockback ? KnockbackShoveTime : ShoveTime;
	// The body moves; with no walking input it brakes to a stop, and motion matching picks the steps for that motion
	// (the size, angle and speed of every push differ, so do the steps). It keeps facing the way it faced (strafing).
	// (GASP rewrites braking and friction every frame, so the push is driven here, before movement, each frame - see Tick.)
	if (!bShoving) ShoveStartTime = World->GetTimeSeconds();
	ShoveVelocity = (ShoveVelocity + Velocity).GetClampedToMaxSize(Max);
	ShoveUntil = FMath::Max(bShoving ? ShoveUntil : 0.f, World->GetTimeSeconds() + Time);
	bShoving = true;
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		AI->ClearFocus(EAIFocusPriority::Gameplay);
		AI->SetControlRotation(GetActorRotation());
	}
	StopForReaction(Time + 0.3f);
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
	if (ReactivePhysics && ReactivePhysics->ApplyHit(Bone,Point,Impulse))
	{
		HitReaction->ResetReaction(); // no additive torque stacked onto the physical-animation target
		return;
	}
	HitReaction->AddHit(Bone, Point, Impulse);
}

void ARPGNpcCharacter::PlayImpactFeel(AActor* Attacker, ERPGPowerReaction Reaction)
{
	const URPGPowerSettings* S = URPGPowerSettings::Get();
	float StopTime = 0.f, Shake = 0.f;
	switch (Reaction)
	{
		case ERPGPowerReaction::Stagger:
		case ERPGPowerReaction::Knockback:	StopTime = S->HitStopStagger;	Shake = S->ShakeStagger;	break;
		case ERPGPowerReaction::Catch:	StopTime = S->HitStopKnockdown;	Shake = S->ShakeKnockdown;	break;
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
