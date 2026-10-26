#include "RPGNpcCharacter.h"

#include "RPGAttributeSet.h"
#include "RPGBodyMass.h"
#include "RPGHitReactionComponent.h"
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

	GetUpFaceDown = FSoftObjectPath(TEXT("/Game/Characters/UEFN_Mannequin/Animations/Ragdoll/M_ragdoll_getup_stand_F.M_ragdoll_getup_stand_F"));
	GetUpFaceUp = FSoftObjectPath(TEXT("/Game/Characters/UEFN_Mannequin/Animations/Ragdoll/M_ragdoll_getup_stand_B.M_ragdoll_getup_stand_B"));
	GetUpLeftSide = FSoftObjectPath(TEXT("/Game/Characters/UEFN_Mannequin/Animations/Ragdoll/M_ragdoll_getup_stand_L.M_ragdoll_getup_stand_L"));
	GetUpRightSide = FSoftObjectPath(TEXT("/Game/Characters/UEFN_Mannequin/Animations/Ragdoll/M_ragdoll_getup_stand_R.M_ragdoll_getup_stand_R"));
}

void ARPGNpcCharacter::BeginPlay()
{
	Super::BeginPlay();
	Home = GetActorLocation();
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		// GASP's motion matching reads acceleration: path following must accelerate like a player, not set velocity.
		Move->GetNavMovementProperties()->bUseAccelerationForPaths = true;
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
}

void ARPGNpcCharacter::ApplyWalkIntent()
{
	// GASP's character keeps its input intent in a Blueprint struct (fields with generated suffixes).
	FStructProperty* InputState = FindFProperty<FStructProperty>(GetClass(), TEXT("CharacterInputState"));
	if (!InputState) return;
	for (TFieldIterator<FBoolProperty> It(InputState->Struct); It; ++It)
		if (It->GetName().StartsWith(TEXT("WantsToWalk_")))
			It->SetPropertyValue_InContainer(InputState->ContainerPtrToValuePtr<void>(this), bWalk);
}

void ARPGNpcCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
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
	if (!bDown && Result.Displacement >= StepDisplacement)
	{
		// Pushed hard enough to move: a balance step the way the blow pushes, on the struck leg if a leg was hit.
		const FName Limb = PhysicsReaction ? PhysicsReaction->GetLimbForBone(Hit.HitBone) : NAME_None;
		const float FootSide = Limb == TEXT("LegLeft") ? 1.f : Limb == TEXT("LegRight") ? -1.f : 0.f;
		const FVector Push = Hit.StrikeDirection.IsNearlyZero() && Hit.Attacker ? GetActorLocation() - Hit.Attacker->GetActorLocation() : Hit.StrikeDirection;
		PlayBalanceStep(Push, Result.Displacement >= StaggerHeavyDisplacement, FootSide,
			FMath::Clamp(float(Result.Displacement) * StepPerDisplacement, StepMinDistance, StepMaxDistance));
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
	// A leg hit hard enough steps that leg away with the force.
	const FName Limb = PhysicsReaction->GetLimbForBone(Impact.Hit.BoneName);
	const bool bLeg = Limb == TEXT("LegLeft") || Limb == TEXT("LegRight");
	if (Impact.Charge >= EnergyStepCharge || (bLeg && Impact.Charge >= EnergyStepCharge * 0.5f))
	{
		const FVector Push = FVector(Impact.Direction.X, Impact.Direction.Y, 0.f);
		PlayBalanceStep(Push, Impact.Charge >= 0.75f, Limb == TEXT("LegLeft") ? 1.f : Limb == TEXT("LegRight") ? -1.f : 0.f,
			FMath::Lerp(StepMinDistance, StepMaxDistance * 0.6f, FMath::Clamp(Impact.Charge, 0.f, 1.f)));
	}
}

UAnimSequence* ARPGNpcCharacter::PickStepClip(const FVector& WorldPush, bool bHeavy, float FootSide) const
{
	// Push in the NPC's own frame: 0 = straight ahead, +90 = to its right, 180 = straight back.
	const FVector Local = GetActorTransform().InverseTransformVectorNoScale(FVector(WorldPush.X, WorldPush.Y, 0.f));
	if (Local.IsNearlyZero()) return nullptr;
	const float Angle = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));
	static const TCHAR* Sectors[] = { TEXT("F"), TEXT("FR"), TEXT("R"), TEXT("BR"), TEXT("B"), TEXT("BL"), TEXT("L"), TEXT("FL") };
	const int32 Sector = FMath::RoundToInt(FMath::Fmod(Angle + 360.f, 360.f) / 45.f) % 8;
	// The struck leg is the one that steps; GASP names the foot a stop lands on.
	const bool bLeftFoot = FootSide > 0.f ? true : FootSide < 0.f ? false : FMath::RandBool();
	FString Dir = Sectors[Sector];
	if (Dir == TEXT("L") || Dir == TEXT("R")) Dir += bLeftFoot ? TEXT("L") : TEXT("R");
	const TCHAR* Gait = bHeavy ? TEXT("Run") : TEXT("Walk");
	const FString Name = FString::Printf(TEXT("M_Neutral_%s_Stop_%s_%s"), Gait, *Dir, bLeftFoot ? TEXT("Lfoot") : TEXT("Rfoot"));
	return LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/Characters/UEFN_Mannequin/Animations/%s/%s.%s"), Gait, *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
}

void ARPGNpcCharacter::PlayBalanceStep(const FVector& Push, bool bHeavy, float FootSide, float Distance)
{
	if (bDown || ActiveReaction) return;   // already stepping: the springs take the rest
	UAnimSequence* Step = PickStepClip(Push, bHeavy, FootSide);
	UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!Step || !Anim) return;
	// The step's own root motion carries the capsule (feet planted), scaled to how far this push should move it.
	const float Travel = float(Step->ExtractRootMotionFromRange(0., Step->GetPlayLength(), FAnimExtractContext(0., false)).GetTranslation().Size2D());
	SetAnimRootMotionTranslationScale(Travel > 1.f ? FMath::Clamp(Distance / Travel, 0.1f, 1.5f) : 1.f);
	ActiveReaction = Anim->PlaySlotAnimationAsDynamicMontage(Step, ReactionSlot, 0.12f, 0.3f);
	UE_LOG(LogRPGInput, Log, TEXT("%s balance step %s, %.0f cm (clip %.0f cm)"), *GetActorNameOrLabel(), *Step->GetName(), Distance, Travel);
	if (!ActiveReaction) SetAnimRootMotionTranslationScale(1.f);
	StopForReaction(Step->GetPlayLength() + 0.4f);
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

	// Stand up where the body lies: how it lies picks the get-up, and the capsule is turned and placed so that clip's
	// first frame lies on the ragdoll (the fade back to animation then blends into it).
	const FVector Pelvis = Body->GetBoneLocation(TEXT("pelvis"));
	const FVector Left = (Body->GetBoneLocation(TEXT("thigh_l")) - Body->GetBoneLocation(TEXT("thigh_r"))).GetSafeNormal();
	const FVector Front = FVector::CrossProduct(Body->GetBoneLocation(TEXT("spine_05")) - Pelvis, Left).GetSafeNormal();
	const bool bOnSide = FMath::Abs(Left.Z) > FMath::Abs(Front.Z);
	const bool bFaceDown = !bOnSide && Front.Z < 0.f;
	const bool bLeftDown = bOnSide && Left.Z < 0.f;
	UAnimSequence* GetUp = (bOnSide ? (bLeftDown ? GetUpLeftSide : GetUpRightSide) : (bFaceDown ? GetUpFaceDown : GetUpFaceUp)).LoadSynchronous();
	const FVector Start = bOnSide ? (bLeftDown ? GetUpLeftSideStart : GetUpRightSideStart) : (bFaceDown ? GetUpFaceDownStart : GetUpFaceUpStart);

	FHitResult Ground;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RPGNpcGetUp), false, this);
	const bool bGround = GetWorld()->LineTraceSingleByChannel(Ground, Pelvis + FVector(0.f, 0.f, 80.f), Pelvis - FVector(0.f, 0.f, 300.f), ECC_Visibility, Params);
	const FVector Head = Body->GetBoneLocation(TEXT("head")) - Pelvis;
	const float MeshYaw = FMath::RadiansToDegrees(FMath::Atan2(Head.Y, Head.X)) - Start.Z;
	const FVector Offset = FRotator(0.f, MeshYaw, 0.f).RotateVector(FVector(Start.X, Start.Y, 0.f));
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Target(Pelvis.X - Offset.X, Pelvis.Y - Offset.Y, (bGround ? Ground.ImpactPoint.Z : Home.Z - HalfHeight) + HalfHeight + 1.f);
	SetActorLocationAndRotation(bGround ? Target : Home, FRotator(0.f, MeshYaw - Body->GetRelativeRotation().Yaw, 0.f), false, nullptr, ETeleportType::None);

	if (UCharacterMovementComponent* Move = GetCharacterMovement()) Move->SetMovementMode(MOVE_Walking);
	float Duration = 1.f;
	if (UAnimInstance* Anim = Body->GetAnimInstance(); Anim && GetUp)
	{
		// Hurt bodies get up slower.
		const URPGAttributeSet* A = Stats ? Stats->GetAttributes() : nullptr;
		const float HealthLeft = bKnockedOut || !A || A->GetMaxHealth() <= 0.f ? 0.f : FMath::Clamp(A->GetHealth() / A->GetMaxHealth(), 0.f, 1.f);
		const float Rate = FMath::Lerp(GetUpHurtRate, GetUpHealthyRate, HealthLeft);
		ActiveReaction = Anim->PlaySlotAnimationAsDynamicMontage(GetUp, ReactionSlot, 0.f, 0.35f, Rate);
		Duration = GetUp->GetPlayLength() / Rate;
		UE_LOG(LogRPGInput, Log, TEXT("%s gets up %s (%s) at %.2fx"), *GetActorNameOrLabel(),
			bOnSide ? (bLeftDown ? TEXT("from its left side") : TEXT("from its right side")) : (bFaceDown ? TEXT("from face down") : TEXT("from its back")),
			*GetUp->GetName(), Rate);
	}
	bDown = false;
	StopForReaction(Duration + 0.5f);
	if (bKnockedOut)
	{
		if (UAbilitySystemComponent* AbilitySystem = Stats ? Stats->GetAbilitySystem() : nullptr)
			if (const URPGAttributeSet* A = Stats->GetAttributes())
				AbilitySystem->SetNumericAttributeBase(URPGAttributeSet::GetHealthAttribute(), A->GetMaxHealth());
		bKnockedOut = false;
	}
}
