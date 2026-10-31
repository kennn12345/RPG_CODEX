// Hit outcomes: which reaction each kind and size of force gives.
// RPG.Power.Outcomes: the calculator alone (no world): punches, kicks, charged punches, energy taps / charges and body
//   bumps against the standard bandit, plus heavier targets.
// RPG.Npc.HitOutcomes: the same on a real GASP NPC in the dummy field, plus a combo escalating, a body shot doubling it
//   over, a hit from behind, a catch that recovers and one that fails (a braced fall), a wall slam, a ledge fall, a braced
//   knockdown, a hit while it lies on the ground and a knockout (fully limp). RPG.Player.HitOutcomes: the player through
//   URPGHitOutcomeComponent. A normal punch staggers, a charged punch knocks
//   back, an energy tap staggers, half a charge knocks back, walking / running / sprinting / very fast running into it
//   nudges / staggers / knocks back / knocks back (never down), and a Strength 14 punch knocks it down. Logs HITOUTCOME lines.
//   RPG_SHOT_OUTCOMES=1 (rendered editor) films every case from the side into Saved/Screenshots/HitOutcomes/.
//   UnrealEditor RPG_CODEX.uproject /Game/RPG/Maps/L_DummyField -ExecCmds="Automation RunTests RPG.Npc.HitOutcomes"
//   -TestExit="Automation Test Queue Empty" -unattended

#include "CoreMinimal.h"
#include "RPGPowerSystem.h"
#include "RPGPowerSettings.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FRPGPowerInput OutcomeInput(ERPGPowerSource Source, float Strength, int32 Seed)
	{
		FRPGPowerInput Input;
		Input.Source = Source;
		Input.Attacker.Strength = Strength;
		Input.Target = FRPGPowerStats();
		Input.Target.Strength = 3.f;
		Input.HitBone = TEXT("spine_05");
		Input.RandomSeed = Seed;
		return Input;
	}

	/** Every reaction this input gives over many rolls. */
	TSet<ERPGPowerReaction> Reactions(FRPGPowerInput Input, int32 Rolls = 300)
	{
		TSet<ERPGPowerReaction> Seen;
		for (int32 Seed = 1; Seed <= Rolls; ++Seed) { Input.RandomSeed = Seed; Seen.Add(URPGPowerSystem::Calculate(Input).Reaction); }
		return Seen;
	}

	FString Names(const TSet<ERPGPowerReaction>& Seen)
	{
		FString Out;
		for (ERPGPowerReaction R : Seen) Out += UEnum::GetDisplayValueAsText(R).ToString() + TEXT(" ");
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPowerOutcomesTest, "RPG.Power.Outcomes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRPGPowerOutcomesTest::RunTest(const FString& Parameters)
{
	auto Only = [this](const TCHAR* What, const FRPGPowerInput& Input, std::initializer_list<ERPGPowerReaction> Allowed)
	{
		const TSet<ERPGPowerReaction> Seen = Reactions(Input);
		bool bOk = Seen.Num() > 0;
		for (ERPGPowerReaction R : Seen) bOk &= TSet<ERPGPowerReaction>(Allowed).Contains(R);
		TestTrue(FString::Printf(TEXT("%s: got %s"), What, *Names(Seen)), bOk);
		return Seen;
	};
	using R = ERPGPowerReaction;

	// Punches and kicks at the player's Strength 5: never a knockdown from an ordinary blow.
	Only(TEXT("Normal punch, Strength 5"), OutcomeInput(ERPGPowerSource::Punch, 5.f, 1), { R::Stagger });
	Only(TEXT("Kick, Strength 5"), OutcomeInput(ERPGPowerSource::Kick, 5.f, 1), { R::Stagger });
	{
		FRPGPowerInput Heavy = OutcomeInput(ERPGPowerSource::Punch, 5.f, 1);
		Heavy.Tier = ERPGPowerAttackTier::Heavy;
		Heavy.ChargePercent = 0.5f;
		Only(TEXT("Charged punch, Strength 5"), Heavy, { R::Knockback });
		Heavy.Tier = ERPGPowerAttackTier::Power;
		Heavy.ChargePercent = 1.f;
		const TSet<R> Seen = Only(TEXT("Full-power punch, Strength 5"), Heavy, { R::Catch, R::Knockdown });
		TestTrue(TEXT("Full-power punch mostly catches itself"), Seen.Contains(R::Catch));
	}
	// Running into the punch adds momentum, still not a knockdown.
	{
		FRPGPowerInput Running = OutcomeInput(ERPGPowerSource::Punch, 5.f, 1);
		Running.ClosingSpeed = 500.f;
		Only(TEXT("Normal punch while running, Strength 5"), Running, { R::Stagger, R::Knockback });
	}
	Only(TEXT("Normal punch, Strength 14"), OutcomeInput(ERPGPowerSource::Punch, 14.f, 1), { R::Knockdown, R::Launch });

	// Energy: the charge decides, not the caster's Strength.
	{
		FRPGPowerInput Tap = OutcomeInput(ERPGPowerSource::Energy, 5.f, 1);
		Only(TEXT("Energy tap"), Tap, { R::Stagger });
		Tap.Attacker.Strength = 20.f;
		Only(TEXT("Energy tap from Strength 20"), Tap, { R::Stagger });
		FRPGPowerInput Half = OutcomeInput(ERPGPowerSource::Energy, 5.f, 1);
		Half.ChargePercent = 0.5f;
		Only(TEXT("Energy half charge"), Half, { R::Knockback });
		FRPGPowerInput Full = OutcomeInput(ERPGPowerSource::Energy, 5.f, 1);
		Full.ChargePercent = 1.f;
		const TSet<R> Seen = Only(TEXT("Energy full charge"), Full, { R::Catch, R::Knockdown });
		TestTrue(TEXT("Energy full charge can knock down"), Seen.Contains(R::Knockdown));
		Full.Target.Mass = 164.f;
		Full.Target.Stability = 70.f;
		Only(TEXT("Energy full charge on the heavy brute"), Full, { R::Flinch, R::Stagger });
	}

	// Body bumps: by closing speed, never a knockdown on foot; no damage.
	{
		const URPGPowerSettings* S = URPGPowerSettings::Get();
		auto Bump = [](float Speed) { FRPGPowerInput I = OutcomeInput(ERPGPowerSource::Body, 5.f, 1); I.ClosingSpeed = Speed; return I; };
		Only(TEXT("Walking into the bandit"), Bump(180.f), { R::Flinch });
		Only(TEXT("Running into the bandit"), Bump(450.f), { R::Stagger });
		Only(TEXT("Sprinting into the bandit"), Bump(750.f), { R::Knockback });
		Only(TEXT("Just under a flying crash"), Bump(S->BumpKnockdownSpeed - 1.f), { R::Knockback });
		Only(TEXT("Flying crash"), Bump(1500.f), { R::Knockdown });
		TestEqual(TEXT("A bump deals no damage"), URPGPowerSystem::Calculate(Bump(750.f)).Damage, 0.f);
		FRPGPowerInput Brute = Bump(450.f);
		Brute.Target.Mass = 164.f;
		Only(TEXT("Running into the heavy brute"), Brute, { R::Flinch });
	}

	// Balance: the same punch on a body rocked by recent hits escalates stagger -> knockback -> catch.
	{
		FRPGPowerInput Punch = OutcomeInput(ERPGPowerSource::Punch, 5.f, 1);
		Only(TEXT("Punch at full balance"), Punch, { R::Stagger });
		Punch.Balance = 0.4f;
		Only(TEXT("Punch at 40% balance"), Punch, { R::Knockback });
		Punch.Balance = 0.f;
		Only(TEXT("Punch at empty balance"), Punch, { R::Catch, R::Knockdown });
		Punch.Balance = 0.f;
		TestEqual(TEXT("Balance changes the reaction, not the damage"), URPGPowerSystem::Calculate(Punch).Damage, URPGPowerSystem::Calculate(OutcomeInput(ERPGPowerSource::Punch, 5.f, 1)).Damage, 0.001f);
	}
	// Hit location: head staggers more easily, legs lean toward a catch, arms take some of it.
	{
		FRPGPowerInput Body = OutcomeInput(ERPGPowerSource::Punch, 4.f, 1);
		FRPGPowerInput Head = Body; Head.HitBone = TEXT("head");
		FRPGPowerInput Leg = Body; Leg.HitBone = TEXT("thigh_l");
		FRPGPowerInput Arm = Body; Arm.HitBone = TEXT("lowerarm_r");
		const FRPGPowerResult B = URPGPowerSystem::Calculate(Body), H = URPGPowerSystem::Calculate(Head), L = URPGPowerSystem::Calculate(Leg), A = URPGPowerSystem::Calculate(Arm);
		TestTrue(TEXT("Zones read from the bone"), B.HitZone == ERPGHitZone::Body && H.HitZone == ERPGHitZone::Head && L.HitZone == ERPGHitZone::Leg && A.HitZone == ERPGHitZone::Arm);
		TestTrue(TEXT("Head hit staggers more easily"), H.StaggerChance > B.StaggerChance);
		TestTrue(TEXT("Leg hit rocks the footing more"), L.ReactionLevel > B.ReactionLevel);
		TestTrue(TEXT("Arm hit rocks it less"), A.ReactionLevel < B.ReactionLevel);
		FRPGPowerInput LegCharged = OutcomeInput(ERPGPowerSource::Punch, 7.f, 1); LegCharged.HitBone = TEXT("calf_r");
		Only(TEXT("Strength 7 punch to the leg"), LegCharged, { R::Catch, R::Knockdown });
	}
	// Situation: planted in the stance it holds; caught mid-attack it goes further.
	{
		FRPGPowerInput Punch = OutcomeInput(ERPGPowerSource::Punch, 5.f, 1);
		Punch.Tier = ERPGPowerAttackTier::Heavy; Punch.ChargePercent = 0.f;   // 5.75: a stagger as it stands
		Only(TEXT("Charged punch, relaxed"), Punch, { R::Stagger });
		Punch.Situation = URPGPowerSettings::Get()->AttackingSituation;
		Only(TEXT("Charged punch, caught mid-attack"), Punch, { R::Knockback });
		FRPGPowerInput Strong = OutcomeInput(ERPGPowerSource::Punch, 6.5f, 1);
		Only(TEXT("Strength 6.5 punch, relaxed"), Strong, { R::Knockback });
		Strong.Situation = URPGPowerSettings::Get()->StanceSituation;
		Only(TEXT("Strength 6.5 punch, planted in the stance"), Strong, { R::Stagger });
	}
	return true;
}

#endif

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "RPG_CODEX.h"
#include "RPGBodyImpactComponent.h"
#include "RPGCharacterStatsComponent.h"
#include "RPGHitOutcomeComponent.h"
#include "RPGNpcCharacter.h"
#include "RPGPhysicsReactionComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealClient.h"

namespace
{
	enum class EOutcomeKind : uint8 { Punch, Energy, Bump, Combo, CatchRecover, CatchFail, KnockoutLimp, Downed, Wall, Ledge, FromBehind, KneeFwd, KneeBack, KneeFwdFail, KneeAngled };
	struct FOutcomeCase
	{
		const TCHAR* Name;
		EOutcomeKind Kind;
		float Amount;		// punch: Strength; energy: charge; bump: speed (cm/s)
		float Charge;		// punch charge (0 = normal, 1 = full power)
		const TCHAR* Bone;
		ERPGPowerReaction Expected;
		bool bDown;
	};
	const FOutcomeCase OutcomeCases[] = {
		{ TEXT("punch_normal"), EOutcomeKind::Punch, 5.f, 0.f, TEXT("spine_05"), ERPGPowerReaction::Stagger, false },
		{ TEXT("punch_charged"), EOutcomeKind::Punch, 5.f, 0.5f, TEXT("spine_05"), ERPGPowerReaction::Knockback, false },
		{ TEXT("energy_tap"), EOutcomeKind::Energy, 0.f, 0.f, TEXT("spine_05"), ERPGPowerReaction::Stagger, false },
		{ TEXT("energy_half"), EOutcomeKind::Energy, 0.5f, 0.f, TEXT("spine_05"), ERPGPowerReaction::Knockback, false },
		{ TEXT("bump_walk"), EOutcomeKind::Bump, 180.f, 0.f, TEXT(""), ERPGPowerReaction::Flinch, false },
		{ TEXT("bump_run"), EOutcomeKind::Bump, 450.f, 0.f, TEXT(""), ERPGPowerReaction::Stagger, false },
		{ TEXT("bump_sprint"), EOutcomeKind::Bump, 800.f, 0.f, TEXT(""), ERPGPowerReaction::Knockback, false },
		{ TEXT("bump_very_fast"), EOutcomeKind::Bump, 900.f, 0.f, TEXT(""), ERPGPowerReaction::Knockback, false },
		{ TEXT("body_shot"), EOutcomeKind::Punch, 5.f, 0.f, TEXT("spine_02"), ERPGPowerReaction::Stagger, false },
		{ TEXT("head_shot"), EOutcomeKind::Punch, 5.f, 0.f, TEXT("head"), ERPGPowerReaction::Stagger, false },
		{ TEXT("from_behind"), EOutcomeKind::FromBehind, 5.f, 0.01f, TEXT("spine_05"), ERPGPowerReaction::Knockback, false },
		{ TEXT("combo_escalates"), EOutcomeKind::Combo, 5.f, 0.f, TEXT("spine_05"), ERPGPowerReaction::Knockback, false },
		{ TEXT("catch_recovers"), EOutcomeKind::CatchRecover, 5.f, 1.f, TEXT("spine_05"), ERPGPowerReaction::Catch, false },
		{ TEXT("catch_fails"), EOutcomeKind::CatchFail, 5.f, 1.f, TEXT("spine_05"), ERPGPowerReaction::Knockdown, true },
		{ TEXT("knee_fwd_recovers"), EOutcomeKind::KneeFwd, 5.f, 1.f, TEXT("spine_05"), ERPGPowerReaction::Catch, false },
		{ TEXT("knee_back_recovers"), EOutcomeKind::KneeBack, 5.f, 1.f, TEXT("spine_05"), ERPGPowerReaction::Catch, false },
		{ TEXT("knee_fwd_fails"), EOutcomeKind::KneeFwdFail, 5.f, 1.f, TEXT("spine_05"), ERPGPowerReaction::Knockdown, true },
		{ TEXT("knee_angled_turns"), EOutcomeKind::KneeAngled, 5.f, 1.f, TEXT("spine_05"), ERPGPowerReaction::Catch, false },
		{ TEXT("wall_slam"), EOutcomeKind::Wall, 5.f, 0.5f, TEXT("spine_05"), ERPGPowerReaction::Knockback, false },
		{ TEXT("ledge_fall"), EOutcomeKind::Ledge, 5.f, 0.5f, TEXT("spine_05"), ERPGPowerReaction::Knockback, true },
		{ TEXT("knockdown_braced"), EOutcomeKind::Punch, 14.f, 0.f, TEXT("spine_05"), ERPGPowerReaction::Knockdown, true },
		{ TEXT("hit_while_down"), EOutcomeKind::Downed, 14.f, 0.f, TEXT("pelvis"), ERPGPowerReaction::Knockdown, true },
		{ TEXT("knockout_limp"), EOutcomeKind::KnockoutLimp, 5.f, 0.f, TEXT("spine_05"), ERPGPowerReaction::Knockdown, true },
	};

	FRPGMeleeHitInfo MakePunch(AActor* Attacker, ACharacter* Target, const TCHAR* Bone, const FVector& Direction, float Charge)
	{
		FRPGMeleeHitInfo Info;
		Info.AttackName = TEXT("Outcome Punch");
		Info.Attacker = Attacker;
		Info.HitActor = Target;
		Info.HitBone = Bone;
		Info.ImpactLocation = Target->GetMesh()->GetBoneLocation(Info.HitBone);
		Info.StrikeDirection = Direction;
		Info.bCharged = Charge > 0.f;
		Info.ChargePercent = Charge;
		return Info;
	}

	/** A seed whose roll gives Wanted for this hit on Target as it stands now (0: none found). */
	int32 FindSeed(const FRPGMeleeHitInfo& Info, URPGHitOutcomeComponent* Outcome, ERPGPowerReaction Wanted)
	{
		FRPGPowerInput Input = URPGPowerSystem::MakeMeleeInput(Info);
		Outcome->TestSeed = 0;
		Outcome->PrepareInput(Input);
		for (int32 Seed = 1; Seed < 2000; ++Seed)
		{
			Input.RandomSeed = Seed;
			if (URPGPowerSystem::Calculate(Input).Reaction == Wanted) return Seed;
		}
		return 0;
	}

	AStaticMeshActor* SpawnBox(UWorld* World, const FVector& Center, const FVector& Size, const FRotator& Rotation)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		AStaticMeshActor* Box = World->SpawnActor<AStaticMeshActor>(Center, Rotation);
		if (!Box || !Cube) return Box;
		Box->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Box->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Box->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Box->SetActorScale3D(Size / 100.f);
		return Box;
	}

	/** Side-view screenshots of each case (RPG_SHOT_OUTCOMES=1, rendered editor). */
	struct FOutcomeFilm
	{
		ACameraActor* Camera = nullptr;
		float LastShot = -1.f;
		FVector Look = FVector::ZeroVector;
		/** Follows Body (its pelvis, smoothed) from the side; every 0.1 s for the first 1.5 s, then every 0.25 s. */
		void Shoot(UWorld* World, const FVector& Body, const FVector& Forward, const FString& Folder, int32 Case, const TCHAR* Name, float Now, float Since)
		{
			if (FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_SHOT_OUTCOMES")) != TEXT("1")) return;
			Look = Since < 0.05f || Look.IsZero() ? Body : FMath::Lerp(Look, Body, 0.35f);
			if (Now - LastShot < (Since < 1.5f ? 0.1f : 0.25f)) return;
			if (!Camera)
			{
				Camera = World->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator);
				Camera->GetCameraComponent()->SetFieldOfView(60.f);
				Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			}
			if (APlayerController* PC = World->GetFirstPlayerController()) PC->SetViewTarget(Camera);
			const FVector Side = FVector::CrossProduct(Forward, FVector::UpVector);
			const FVector Eye = Look - Side * 520.f + FVector(0.f, 0.f, 70.f);
			Camera->SetActorLocationAndRotation(Eye, (Look - Eye).Rotation());
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / FString::Printf(TEXT("Screenshots/%s/%02d_%s_%.2fs.png"), *Folder, Case, Name, Since), false, false);
			LastShot = Now;
		}
	};

	class FRPGHitOutcomeCommand : public IAutomationLatentCommand
	{
	public:
		explicit FRPGHitOutcomeCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World) return ++Frames > 600 ? Fail(TEXT("no play world")) : false;
			ACharacter* Player = UGameplayStatics::GetPlayerCharacter(World, 0);
			const float Now = World->GetTimeSeconds();
			if (!Npc.IsValid())
			{
				for (TActorIterator<ARPGNpcCharacter> It(World); It; ++It) { Npc = *It; break; }
				if (!Npc.IsValid() || !Player) return ++Frames > 600 ? Fail(TEXT("no NPC or player")) : false;
				for (TActorIterator<ARPGNpcCharacter> It(World); It; ++It) It->bWander = false;
				PhaseStart = Now;
				return false;
			}
			ARPGNpcCharacter* N = Npc.Get();
			if (Case >= int32(UE_ARRAY_COUNT(OutcomeCases))) return Finish();
			const FOutcomeCase& C = OutcomeCases[Case];
			URPGPhysicsReactionComponent* Reaction = N->PhysicsReaction;
			const bool bLimp = Reaction && Reaction->GetReactionState() == ERPGPhysicsReactionState::Limp;

			switch (Phase)
			{
			case 0:   // wait until it stands still on its feet with its balance back, then set up
			{
				const bool bReady = !N->IsBusy() && !N->BodyImpact->IsDown() && Reaction && Reaction->GetReactionState() == ERPGPhysicsReactionState::Animated
					&& N->HitOutcome->GetBalance() >= 0.999f;
				if ((!bReady || Now - PhaseStart < 1.5f) && Now - PhaseStart < 25.f) return false;
				if (AController* Controller = N->GetController()) Controller->StopMovement();
				N->Stats->SetStatBaseByName(TEXT("Health"), C.Kind == EOutcomeKind::KnockoutLimp ? 1.f : 100.f);
				N->HitOutcome->TestSeed = 0;
				N->HitOutcome->ResetPeaks();
				// Knee-down catches only in their own cases (elsewhere the arms-out stumble, so each case is one outcome).
				N->HitOutcome->TestKneeCatch = C.Kind == EOutcomeKind::KneeFwd || C.Kind == EOutcomeKind::KneeBack || C.Kind == EOutcomeKind::KneeFwdFail
					|| C.Kind == EOutcomeKind::KneeAngled ? 1 : -1;
				StartYaw = float(N->GetActorRotation().Yaw);
				KneesBefore = N->HitOutcome->KneeCatchCount; KneeFallsBefore = N->HitOutcome->KneeFallCount;
				Forward = N->GetActorForwardVector().GetSafeNormal2D();
				if (C.Kind == EOutcomeKind::Ledge)
				{
					// A 1 m high platform; it stands 70 cm from the back edge.
					const float Half = N->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
					const FVector Ground = N->GetActorLocation() - FVector(0.f, 0.f, Half);
					Props.Add(SpawnBox(World, Ground + Forward * 130.f + FVector(0.f, 0.f, 50.f), FVector(400.f, 400.f, 100.f), Forward.Rotation()));
					N->SetActorLocation(N->GetActorLocation() + FVector(0.f, 0.f, 102.f), false, nullptr, ETeleportType::TeleportPhysics);
				}
				if (C.Kind == EOutcomeKind::Wall)
					Props.Add(SpawnBox(World, N->GetActorLocation() - Forward * 140.f, FVector(30.f, 400.f, 300.f), Forward.Rotation()));
				StartSpot = N->GetActorLocation();
				// The player waits off to the side for hits; for a bump it stands in front, facing the NPC.
				const FVector PlayerSpot = C.Kind == EOutcomeKind::Bump ? StartSpot + Forward * 320.f : StartSpot + N->GetActorRightVector() * 600.f;
				Player->SetActorLocationAndRotation(PlayerSpot, (StartSpot - PlayerSpot).Rotation(), false, nullptr, ETeleportType::TeleportPhysics);
				Player->GetCharacterMovement()->StopMovementImmediately();
				if (URPGCharacterStatsComponent* PlayerStats = URPGCharacterStatsComponent::FindStats(Player)) PlayerStats->SetStatBaseByName(TEXT("Strength"), C.Amount);
				BumpsBefore = N->BodyImpact->GetBumpCount();
				CatchFallsBefore = N->CatchFallCount; WallsBefore = N->WallSlamCount; LedgesBefore = N->LedgeFallCount; DownedBefore = N->DownedHitCount;
				LimpFallsBefore = N->HitOutcome->LimpFallCount;
				MaxMoved = 0.f; bSawLimp = false; bSawDown = false; bApplied = false; bSawBraced = false; bSawUnbracedLimp = false; FirstLimpAt = -1.f;
				bSawGrip = false; AfterGripMoved = 0.f;
				Levels.Reset(); Shots = 0; DownedPelvisBefore = DownedChestBefore = DownedHeadBefore = FVector::ZeroVector;
				Phase = 1; PhaseStart = Now;
				return false;
			}
			case 1:   // apply the force
			{
				if (Now - PhaseStart < 0.3f) return false;
				// Pushed from behind (forward falls) for the hit from behind and the forward knee-downs.
				// (angled: pushed from behind but 40 degrees off to the side)
				const FVector Dir = C.Kind == EOutcomeKind::KneeAngled ? Forward.RotateAngleAxis(40.f, FVector::UpVector)
					: C.Kind == EOutcomeKind::FromBehind || C.Kind == EOutcomeKind::KneeFwd || C.Kind == EOutcomeKind::KneeFwdFail ? Forward : -Forward;
				switch (C.Kind)
				{
				case EOutcomeKind::Energy:
				{
					FRPGEnergyImpact Impact;
					Impact.Attacker = Player;
					Impact.Direction = Dir;
					Impact.Charge = C.Amount;
					Impact.Damage = 1.f;
					Impact.Impulse = FMath::Lerp(1200.f, 5000.f, C.Amount);
					Impact.Hit.BoneName = C.Bone;
					Impact.Hit.ImpactPoint = N->GetMesh()->GetBoneLocation(Impact.Hit.BoneName);
					IRPGEnergySpellTarget::Execute_ReceiveEnergyHit(N, Impact);
					bApplied = true;
					break;
				}
				case EOutcomeKind::Bump:
				{
					// Run at the NPC until the capsules meet.
					const FVector Toward = (StartSpot - Player->GetActorLocation()).GetSafeNormal2D();
					UCharacterMovementComponent* Move = Player->GetCharacterMovement();
					if (N->BodyImpact->GetBumpCount() > BumpsBefore || N->BodyImpact->IsDown()) { Move->StopMovementImmediately(); bApplied = true; }
					else if (Now - PhaseStart < 3.f) { Move->Velocity = Toward * C.Amount + FVector(0.f, 0.f, Move->Velocity.Z); return false; }
					break;
				}
				case EOutcomeKind::Combo:
				{
					// Three ordinary punches 0.35 s apart: its balance can't come back in between.
					if (Shots < 3 && Now - LastShot >= 0.35f)
					{
						IRPGMeleeTarget::Execute_ReceiveMeleeHit(N, MakePunch(Player, N, C.Bone, Dir, 0.f));
						Levels.Add(N->LastPowerResult.ReactionLevel);
						++Shots; LastShot = Now;
					}
					if (Shots < 3) return false;
					bApplied = true;
					break;
				}
				case EOutcomeKind::CatchRecover:
				case EOutcomeKind::CatchFail:
				case EOutcomeKind::KneeFwd:
				case EOutcomeKind::KneeBack:
				case EOutcomeKind::KneeFwdFail:
				case EOutcomeKind::KneeAngled:
				{
					const FRPGMeleeHitInfo Info = MakePunch(Player, N, C.Bone, Dir, C.Charge);
					N->HitOutcome->TestSeed = FindSeed(Info, N->HitOutcome, C.Expected);
					if (N->HitOutcome->TestSeed == 0) { Test->AddError(FString::Printf(TEXT("HITOUTCOME %s: no seed gives %s"), C.Name, *UEnum::GetDisplayValueAsText(C.Expected).ToString())); break; }
					IRPGMeleeTarget::Execute_ReceiveMeleeHit(N, Info);
					bApplied = true;
					break;
				}
				default:   // punches: Punch, FromBehind, Wall, Ledge, Downed (its first, knocking it down), KnockoutLimp
					IRPGMeleeTarget::Execute_ReceiveMeleeHit(N, MakePunch(Player, N, C.Bone, Dir, C.Charge));
					bApplied = true;
					break;
				}
				HitAt = Now;
				Phase = 2; PhaseStart = Now;
				return false;
			}
			case 2:   // watch it react
			{
				Film.Shoot(World, N->GetMesh()->GetBoneLocation(TEXT("pelvis")), Forward, TEXT("HitOutcomes"), Case, C.Name, Now, Now - HitAt);
				MaxMoved = FMath::Max(MaxMoved, float(FVector::Dist2D(N->GetActorLocation(), StartSpot)));
				if (bLimp && Reaction->IsGroundGripping())
				{
					const FVector Pelvis = N->GetMesh()->GetBoneLocation(TEXT("pelvis"));
					if (!bSawGrip) { GripSpot = Pelvis; bSawGrip = true; }
					AfterGripMoved = FMath::Max(AfterGripMoved, float(FVector::Dist2D(Pelvis, GripSpot)));
				}
				if (bLimp && FirstLimpAt < 0.f) FirstLimpAt = Now - HitAt;
				bSawLimp |= bLimp;
				bSawDown |= N->BodyImpact->IsDown();
				if (bLimp && Reaction->GetBraceStrength() > 0.5f && N->HitOutcome->GetBraceWeight() > 0.9f) bSawBraced = true;
				if (bLimp && !Reaction->IsRagdollBraced()) bSawUnbracedLimp = true;
				if (C.Kind == EOutcomeKind::Bump && Player->GetCharacterMovement()) Player->GetCharacterMovement()->StopMovementImmediately();
				// Lying there: hit it again (into the lying body).
				if (C.Kind == EOutcomeKind::Downed && bLimp && DownedPelvisBefore.IsZero() && Now - HitAt > 1.6f && !Reaction->IsReturning())
				{
					DownedPelvisBefore = N->GetMesh()->GetBoneLocation(TEXT("pelvis"));
					DownedChestBefore = N->GetMesh()->GetBoneLocation(TEXT("spine_03"));
					DownedHeadBefore = N->GetMesh()->GetBoneLocation(TEXT("head"));
					if (URPGCharacterStatsComponent* PlayerStats = URPGCharacterStatsComponent::FindStats(Player)) PlayerStats->SetStatBaseByName(TEXT("Strength"), 6.f);
					IRPGMeleeTarget::Execute_ReceiveMeleeHit(N, MakePunch(Player, N, C.Bone, FVector(-Forward.X, -Forward.Y, -0.3f).GetSafeNormal(), 0.f));
					DownedHitAt = Now;
				}
				// How far the blow moves the lying body: the most the hips, chest or head travel in the 0.6 s after it. (One
				// point at one moment was a coin toss: the slide depends on how the body happens to lie on the ground.)
				if (C.Kind == EOutcomeKind::Downed && !DownedPelvisBefore.IsZero() && Now - DownedHitAt <= 0.6f)
					DownedMoved = FMath::Max(DownedMoved, float(FMath::Max3(FVector::Dist(DownedPelvisBefore, N->GetMesh()->GetBoneLocation(TEXT("pelvis"))),
						FVector::Dist(DownedChestBefore, N->GetMesh()->GetBoneLocation(TEXT("spine_03"))), FVector::Dist(DownedHeadBefore, N->GetMesh()->GetBoneLocation(TEXT("head"))))));
				if (Now - PhaseStart < (C.bDown ? 4.5f : 3.2f)) return false;

				const ERPGPowerReaction Got = C.Kind == EOutcomeKind::Bump ? N->BodyImpact->GetLastBumpReaction() : N->LastPowerResult.Reaction;
				const bool bWentDown = bSawLimp || bSawDown;
				bool bReactionOk = C.bDown ? bWentDown : Got == C.Expected;
				FString Extra;
				switch (C.Kind)
				{
				case EOutcomeKind::Combo:
					// (the third punch lands in the catch band, where a fall is a roll: any of knockback / catch / knockdown escalates)
					bReactionOk = Levels.Num() == 3 && Levels[2] > Levels[0] * 1.2f && Got >= ERPGPowerReaction::Knockback;
					Extra = FString::Printf(TEXT(" levels %.1f %.1f %.1f"), Levels.IsValidIndex(0) ? Levels[0] : 0.f, Levels.IsValidIndex(1) ? Levels[1] : 0.f, Levels.IsValidIndex(2) ? Levels[2] : 0.f);
					break;
				case EOutcomeKind::CatchRecover:
					bReactionOk &= N->HitOutcome->PeakBrace > 0.6f && N->CatchFallCount == CatchFallsBefore;
					// A few steps (2-4 ft nominal), with room for pose/capsule offsets. The old knockback push travelled 7-15 ft.
					bReactionOk &= MaxMoved >= 30.48f && MaxMoved <= 5.f * 30.48f;
					Extra = FString::Printf(TEXT(" arms out %.2f"), N->HitOutcome->PeakBrace);
					break;
				case EOutcomeKind::CatchFail:
					bReactionOk &= N->CatchFallCount > CatchFallsBefore && FirstLimpAt >= 0.25f && bSawBraced;
					bReactionOk &= MaxMoved <= 6.f * 30.48f && bSawGrip && AfterGripMoved <= 2.f * 30.48f;
					Extra = FString::Printf(TEXT(" stumbled %.2f s first, braced fall %d, grip %d, after grip %.0f cm"), FirstLimpAt, bSawBraced, bSawGrip, AfterGripMoved);
					break;
				case EOutcomeKind::Wall:
					bReactionOk &= N->WallSlamCount > WallsBefore;
					Extra = FString::Printf(TEXT(" wall slams %d"), N->WallSlamCount - WallsBefore);
					break;
				case EOutcomeKind::Ledge:
					bReactionOk &= N->LedgeFallCount > LedgesBefore && bSawBraced;
					Extra = FString::Printf(TEXT(" went over the edge %d, braced %d"), N->LedgeFallCount - LedgesBefore, bSawBraced);
					break;
				case EOutcomeKind::Downed:
					bReactionOk &= N->DownedHitCount > DownedBefore && DownedMoved > 3.f;
					Extra = FString::Printf(TEXT(" hit while down %d, body moved %.0f cm"), N->DownedHitCount - DownedBefore, DownedMoved);
					break;
				case EOutcomeKind::KnockoutLimp:
					bReactionOk &= bSawUnbracedLimp && !bSawBraced && N->HitOutcome->LimpFallCount > LimpFallsBefore;
					Extra = FString::Printf(TEXT(" fully limp %d, braced %d"), bSawUnbracedLimp, bSawBraced);
					break;
				case EOutcomeKind::KneeFwd:
				case EOutcomeKind::KneeBack:
				{
					const FString Clip = N->HitOutcome->LastKneeClip.ToString();
					const bool bRightClip = Clip.Contains(C.Kind == EOutcomeKind::KneeFwd ? TEXT("KneeFwd_Recover") : TEXT("KneeBack_Recover"));
					bReactionOk &= N->HitOutcome->KneeCatchCount > KneesBefore && N->HitOutcome->KneeFallCount == KneeFallsBefore && !bWentDown && bRightClip;
					Extra = FString::Printf(TEXT(" %s at %.2fx (intensity %.2f), recovered"), *Clip, N->HitOutcome->LastKneeRate, N->HitOutcome->LastKneeIntensity);
					break;
				}
				case EOutcomeKind::KneeFwdFail:
				{
					// it falls with its own momentum: forward, at a real speed
					const FVector V = N->HitOutcome->LastKneeFallVelocity;
					const bool bMomentum = V.Size2D() > 50.f && FVector::DotProduct(V.GetSafeNormal2D(), Forward) > 0.3f;
					bReactionOk &= N->HitOutcome->KneeCatchCount > KneesBefore && N->HitOutcome->KneeFallCount > KneeFallsBefore && FirstLimpAt >= 0.85f && bSawBraced
						&& N->HitOutcome->LastKneeClip.ToString().Contains(TEXT("KneeFwd_Fall")) && bMomentum;
					Extra = FString::Printf(TEXT(" %s, went down after %.2f s, braced %d, fell at %.0f cm/s"), *N->HitOutcome->LastKneeClip.ToString(), FirstLimpAt, bSawBraced, V.Size());
					break;
				}
				case EOutcomeKind::KneeAngled:
				{
					// squared up into the fall: turned ~40 degrees toward it
					const float Turned = FMath::FindDeltaAngleDegrees(StartYaw, float(N->GetActorRotation().Yaw));
					bReactionOk &= N->HitOutcome->KneeCatchCount > KneesBefore && FMath::Abs(Turned - 40.f) < 12.f && !bWentDown;
					Extra = FString::Printf(TEXT(" %s, turned %.0f deg into a fall 40 deg off"), *N->HitOutcome->LastKneeClip.ToString(), Turned);
					break;
				}
				case EOutcomeKind::FromBehind:
					bReactionOk &= N->LastPowerResult.ReactionLevel > N->LastPowerResult.RelativePower * 1.1f;
					Extra = FString::Printf(TEXT(" level %.1f from relative %.1f"), N->LastPowerResult.ReactionLevel, N->LastPowerResult.RelativePower);
					break;
				default:
					if (FCString::Strcmp(C.Name, TEXT("knockdown_braced")) == 0)
					{
						bReactionOk &= bSawBraced && !bSawUnbracedLimp;
						if (Got == ERPGPowerReaction::Knockdown) bReactionOk &= bSawGrip && AfterGripMoved <= 2.f * 30.48f;
						Extra = FString::Printf(TEXT(" braced %d, grip %d, after grip %.0f cm"), bSawBraced, bSawGrip, AfterGripMoved);
					}
					if (FCString::Strcmp(C.Name, TEXT("body_shot")) == 0) { bReactionOk &= N->HitOutcome->PeakBend > 10.f; Extra = FString::Printf(TEXT(" doubled over %.0f deg"), N->HitOutcome->PeakBend); }
					if (FCString::Strcmp(C.Name, TEXT("head_shot")) == 0) { bReactionOk &= N->LastPowerResult.HitZone == ERPGHitZone::Head; }
					break;
				}
				const bool bPass = bApplied && bReactionOk && (bWentDown == C.bDown || C.Kind == EOutcomeKind::Combo);
				Moved.Add(C.Name, MaxMoved);
				bAllOk &= bPass;
				const FString Line = FString::Printf(TEXT("HITOUTCOME %s  %-16s expected %-9s got %-9s down %d moved %.0f cm%s"), bPass ? TEXT("PASS") : TEXT("FAIL"), C.Name,
					*UEnum::GetDisplayValueAsText(C.Expected).ToString(), *UEnum::GetDisplayValueAsText(Got).ToString(), bWentDown, MaxMoved, *Extra);
				UE_LOG(LogRPGInput, Display, TEXT("%s"), *Line);
				if (bPass) Test->AddInfo(Line); else Test->AddError(Line);
				N->HitOutcome->TestSeed = 0;
				for (AActor* Prop : Props) if (Prop) Prop->Destroy();
				Props.Reset();
				DownedMoved = -1.f;
				++Case; Phase = 0; PhaseStart = Now;
				return false;
			}
			}
			return false;
		}

	private:
		bool Finish()
		{
			// Bigger outcomes move it further.
			auto M = [this](const TCHAR* Name) { const float* V = Moved.Find(Name); return V ? *V : 0.f; };
			const bool bOrder = M(TEXT("punch_charged")) > M(TEXT("punch_normal")) && M(TEXT("energy_half")) > M(TEXT("energy_tap"))
				&& M(TEXT("bump_sprint")) > M(TEXT("bump_run")) && M(TEXT("bump_run")) > M(TEXT("bump_walk"));
			const FString Line = FString::Printf(TEXT("HITOUTCOME %s  all cases %d, knockbacks move further than staggers, staggers than nudges %d"),
				bAllOk && bOrder ? TEXT("PASS") : TEXT("FAIL"), bAllOk, bOrder);
			UE_LOG(LogRPGInput, Display, TEXT("%s"), *Line);
			if (bAllOk && bOrder) Test->AddInfo(Line); else Test->AddError(Line);
			return true;
		}

		bool Fail(const TCHAR* Why) { Test->AddError(FString::Printf(TEXT("HITOUTCOME could not run: %s"), Why)); return true; }

		FAutomationTestBase* Test;
		TWeakObjectPtr<ARPGNpcCharacter> Npc;
		FOutcomeFilm Film;
		TArray<AActor*> Props;
		TMap<FString, float> Moved;
		TArray<float> Levels;
		FVector StartSpot = FVector::ZeroVector, Forward = FVector::ForwardVector, DownedPelvisBefore = FVector::ZeroVector, DownedChestBefore = FVector::ZeroVector, DownedHeadBefore = FVector::ZeroVector;
		int32 KneesBefore = 0, KneeFallsBefore = 0;
		float StartYaw = 0.f;
		int32 Frames = 0, Case = 0, Phase = 0, BumpsBefore = 0, Shots = 0, CatchFallsBefore = 0, WallsBefore = 0, LedgesBefore = 0, DownedBefore = 0, LimpFallsBefore = 0;
		float PhaseStart = 0.f, HitAt = 0.f, MaxMoved = 0.f, LastShot = -10.f, FirstLimpAt = -1.f, DownedHitAt = 0.f, DownedMoved = -1.f;
		bool bSawLimp = false, bSawDown = false, bApplied = false, bAllOk = true, bSawBraced = false, bSawUnbracedLimp = false;
		bool bSawGrip = false;
		float AfterGripMoved = 0.f;
		FVector GripSpot = FVector::ZeroVector;
	};

	// ---------------- The player: the same outcomes through URPGHitOutcomeComponent ----------------

	enum class EPlayerCase : uint8 { Stagger, Knockback, Catch, Knockdown, EnergyTap, BumpKnockback, KneeCatch };
	struct FPlayerCase { const TCHAR* Name; EPlayerCase Kind; float Strength; float Charge; ERPGPowerReaction Expected; bool bDown; };
	const FPlayerCase PlayerCases[] = {
		{ TEXT("player_stagger"), EPlayerCase::Stagger, 5.f, 0.f, ERPGPowerReaction::Stagger, false },
		{ TEXT("player_knockback"), EPlayerCase::Knockback, 5.f, 0.5f, ERPGPowerReaction::Knockback, false },
		{ TEXT("player_catch"), EPlayerCase::Catch, 5.f, 1.f, ERPGPowerReaction::Catch, false },
		{ TEXT("player_knee_catch"), EPlayerCase::KneeCatch, 5.f, 1.f, ERPGPowerReaction::Catch, false },
		{ TEXT("player_energy_tap"), EPlayerCase::EnergyTap, 5.f, 0.f, ERPGPowerReaction::Stagger, false },
		{ TEXT("player_bumped"), EPlayerCase::BumpKnockback, 5.f, 0.f, ERPGPowerReaction::Knockback, false },
		{ TEXT("player_knockdown"), EPlayerCase::Knockdown, 14.f, 0.f, ERPGPowerReaction::Knockdown, true },
	};

	class FRPGPlayerOutcomeCommand : public IAutomationLatentCommand
	{
	public:
		explicit FRPGPlayerOutcomeCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World) return ++Frames > 600 ? Fail(TEXT("no play world")) : false;
			ACharacter* Player = UGameplayStatics::GetPlayerCharacter(World, 0);
			const float Now = World->GetTimeSeconds();
			if (!Attacker.IsValid())
			{
				for (TActorIterator<ARPGNpcCharacter> It(World); It; ++It) { Attacker = *It; break; }
				if (!Attacker.IsValid() || !Player || !Player->FindComponentByClass<URPGHitOutcomeComponent>()) return ++Frames > 600 ? Fail(TEXT("no NPC, player or player hit outcome component")) : false;
				for (TActorIterator<ARPGNpcCharacter> It(World); It; ++It) It->bWander = false;
				PhaseStart = Now;
				return false;
			}
			URPGHitOutcomeComponent* Outcome = Player->FindComponentByClass<URPGHitOutcomeComponent>();
			URPGBodyImpactComponent* Impact = Player->FindComponentByClass<URPGBodyImpactComponent>();
			URPGPhysicsReactionComponent* Reaction = Player->FindComponentByClass<URPGPhysicsReactionComponent>();
			ARPGNpcCharacter* N = Attacker.Get();
			if (Case >= int32(UE_ARRAY_COUNT(PlayerCases)))
			{
				const FString Line = FString::Printf(TEXT("PLAYEROUTCOME %s  all player cases"), bAllOk ? TEXT("PASS") : TEXT("FAIL"));
				UE_LOG(LogRPGInput, Display, TEXT("%s"), *Line);
				if (bAllOk) Test->AddInfo(Line); else Test->AddError(Line);
				return true;
			}
			const FPlayerCase& C = PlayerCases[Case];
			const bool bLimp = Reaction && Reaction->GetReactionState() == ERPGPhysicsReactionState::Limp;
			switch (Phase)
			{
			case 0:
			{
				const bool bReady = (!Impact || !Impact->IsDown()) && !bLimp && Outcome->GetBalance() >= 0.999f;
				if ((!bReady || Now - PhaseStart < 1.5f) && Now - PhaseStart < 25.f) return false;
				Player->GetCharacterMovement()->StopMovementImmediately();
				Outcome->TestSeed = 0;
				Outcome->ResetPeaks();
				Outcome->TestKneeCatch = C.Kind == EPlayerCase::KneeCatch ? 1 : -1;
				KneesBefore = Outcome->KneeCatchCount;
				Forward = Player->GetActorForwardVector().GetSafeNormal2D();
				StartSpot = Player->GetActorLocation();
				// The NPC stands in front of the player, out of reach.
				N->SetActorLocation(StartSpot + Forward * 400.f, false, nullptr, ETeleportType::TeleportPhysics);
				N->Stats->SetStatBaseByName(TEXT("Strength"), C.Strength);
				if (URPGCharacterStatsComponent* Stats = URPGCharacterStatsComponent::FindStats(Player)) Stats->SetStatBaseByName(TEXT("Health"), 100.f);
				FallsBefore = Outcome->FallCount;
				MaxMoved = 0.f; bSawLimp = false; bSawBraced = false;
				Phase = 1; PhaseStart = Now;
				return false;
			}
			case 1:
			{
				if (Now - PhaseStart < 0.3f) return false;
				const FVector Dir = -Forward;
				if (C.Kind == EPlayerCase::EnergyTap)
				{
					FRPGEnergyImpact Hit;
					Hit.Attacker = N; Hit.Direction = Dir; Hit.Charge = 0.f; Hit.Damage = 1.f; Hit.Impulse = 1200.f;
					Hit.Hit.BoneName = TEXT("spine_05"); Hit.Hit.ImpactPoint = Player->GetMesh()->GetBoneLocation(Hit.Hit.BoneName);
					Outcome->ReceiveEnergyHit(Hit);
				}
				else if (C.Kind == EPlayerCase::BumpKnockback) Impact->Bump(Dir * 450.f, Player->GetActorLocation(), ERPGPowerReaction::Knockback);
				else
				{
					const FRPGMeleeHitInfo Info = MakePunch(N, Player, TEXT("spine_05"), Dir, C.Charge);
					if (C.Kind == EPlayerCase::Catch || C.Kind == EPlayerCase::KneeCatch) Outcome->TestSeed = FindSeed(Info, Outcome, ERPGPowerReaction::Catch);
					Outcome->ReceiveMeleeHit(Info);
				}
				HitAt = Now;
				Phase = 2; PhaseStart = Now;
				return false;
			}
			case 2:
			{
				Film.Shoot(World, Player->GetMesh()->GetBoneLocation(TEXT("pelvis")), Forward, TEXT("PlayerOutcomes"), Case, C.Name, Now, Now - HitAt);
				MaxMoved = FMath::Max(MaxMoved, float(FVector::Dist2D(Player->GetActorLocation(), StartSpot)));
				bSawLimp |= bLimp || (Impact && Impact->IsDown());
				if (bLimp && Reaction->GetBraceStrength() > 0.5f && Outcome->GetBraceWeight() > 0.9f) bSawBraced = true;
				if (Now - PhaseStart < (C.bDown ? 5.f : C.Kind == EPlayerCase::KneeCatch ? 3.2f : 2.f)) return false;
				const ERPGPowerReaction Got = Outcome->LastResult.Reaction;
				bool bOk = bSawLimp == C.bDown && (C.bDown ? (Got >= ERPGPowerReaction::Knockdown && bSawBraced && Outcome->FallCount > FallsBefore) : Got == C.Expected) && MaxMoved > 3.f;
				if (C.Kind == EPlayerCase::Catch) bOk &= Outcome->PeakBrace > 0.6f;
				if (C.Kind == EPlayerCase::KneeCatch) bOk &= Outcome->KneeCatchCount > KneesBefore;
				bAllOk &= bOk;
				const FString Line = FString::Printf(TEXT("PLAYEROUTCOME %s  %-18s expected %-9s got %-9s down %d braced %d moved %.0f cm arms out %.2f"), bOk ? TEXT("PASS") : TEXT("FAIL"), C.Name,
					*UEnum::GetDisplayValueAsText(C.Expected).ToString(), *UEnum::GetDisplayValueAsText(Got).ToString(), bSawLimp, bSawBraced, MaxMoved, Outcome->PeakBrace);
				UE_LOG(LogRPGInput, Display, TEXT("%s"), *Line);
				if (bOk) Test->AddInfo(Line); else Test->AddError(Line);
				Outcome->TestSeed = 0;
				++Case; Phase = 0; PhaseStart = Now;
				return false;
			}
			}
			return false;
		}

	private:
		bool Fail(const TCHAR* Why) { Test->AddError(FString::Printf(TEXT("PLAYEROUTCOME could not run: %s"), Why)); return true; }

		FAutomationTestBase* Test;
		TWeakObjectPtr<ARPGNpcCharacter> Attacker;
		FOutcomeFilm Film;
		FVector StartSpot = FVector::ZeroVector, Forward = FVector::ForwardVector;
		int32 Frames = 0, Case = 0, Phase = 0, FallsBefore = 0, KneesBefore = 0;
		float PhaseStart = 0.f, HitAt = 0.f, MaxMoved = 0.f;
		bool bSawLimp = false, bSawBraced = false, bAllOk = true;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNpcHitOutcomeTest, "RPG.Npc.HitOutcomes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRPGNpcHitOutcomeTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FRPGHitOutcomeCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPlayerHitOutcomeTest, "RPG.Player.HitOutcomes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRPGPlayerHitOutcomeTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FRPGPlayerOutcomeCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
