// RPG.Npc.ReactionStress: replays the sequence that froze the editor on 2026-10-29 (Docs/HIT_OUTCOMES.md), on the real
// Steady Veteran where it stands in the dummy field: the player flies into it (both knocked down, both get up), walks
// into it twice, then a run of energy hits from the front of rising charge pushes it backward over and over, near the
// level's own slopes and edges, followed by quick punches. It passes if the game keeps running to the end and every
// reaction finishes (nothing stuck). Logs STRESS lines per step, so a hang shows the last step reached.
//   UnrealEditor RPG_CODEX.uproject /Game/RPG/Maps/L_DummyField -ExecCmds="Automation RunTests RPG.Npc.ReactionStress"
//   -TestExit="Automation Test Queue Empty" -unattended -ini:Engine:[Core.System]:HangDuration=15

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "RPG_CODEX.h"
#include "RPGBodyImpactComponent.h"
#include "RPGCharacterStatsComponent.h"
#include "RPGHitOutcomeComponent.h"
#include "RPGMovementAbilityComponent.h"
#include "RPGNpcCharacter.h"
#include "RPGPhysicsReactionComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

namespace
{
	class FRPGReactionStressCommand : public IAutomationLatentCommand
	{
	public:
		explicit FRPGReactionStressCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World) return ++Frames > 600 ? Fail(TEXT("no play world")) : false;
			ACharacter* Player = UGameplayStatics::GetPlayerCharacter(World, 0);
			const float Now = World->GetTimeSeconds();
			if (!Npc.IsValid())
			{
				for (TActorIterator<ARPGNpcCharacter> It(World); It; ++It)
					if (It->GetActorNameOrLabel().Contains(TEXT("Steady")) || It->DisplayLabel.Contains(TEXT("Steady"))) { Npc = *It; break; }
				if (!Npc.IsValid()) for (TActorIterator<ARPGNpcCharacter> It(World); It; ++It) { Npc = *It; break; }
				if (!Npc.IsValid() || !Player) return ++Frames > 600 ? Fail(TEXT("no NPC or player")) : false;
				PhaseStart = Now;
				Step(TEXT("start"), Now);
				return false;
			}
			ARPGNpcCharacter* N = Npc.Get();
			URPGBodyImpactComponent* PlayerImpact = Player->FindComponentByClass<URPGBodyImpactComponent>();
			const bool bNpcDown = N->BodyImpact->IsDown() || (N->PhysicsReaction && N->PhysicsReaction->GetReactionState() == ERPGPhysicsReactionState::Limp);
			const bool bSettled = !bNpcDown && !N->IsBusy() && (!PlayerImpact || !PlayerImpact->IsDown());

			switch (Phase)
			{
			case 0:   // fly into it at 18 m/s, as in the frozen session
				if (Now - PhaseStart < 1.f) return false;
				N->bWander = false;
				{
					const FVector From = N->GetActorLocation() + N->GetActorForwardVector() * 700.f + FVector(0.f, 0.f, 5.f);
					Player->SetActorLocation(From, false, nullptr, ETeleportType::TeleportPhysics);
					if (URPGMovementAbilityComponent* Abilities = Player->FindComponentByClass<URPGMovementAbilityComponent>()) Abilities->StartFlight();
					Dir = (N->GetActorLocation() - From).GetSafeNormal2D();
				}
				Step(TEXT("flying crash"), Now);
				Phase = 1; PhaseStart = Now;
				return false;
			case 1:
				if (!bSawDown && !N->BodyImpact->IsDown() && (!PlayerImpact || !PlayerImpact->IsDown()) && Now - PhaseStart < 3.f)
				{
					Player->GetCharacterMovement()->Velocity = Dir * 1813.f;
					return false;
				}
				bSawDown = true;
				if (!bSettled && Now - PhaseStart < 16.f) return false;
				Step(TEXT("both back up"), Now);
				Phase = 2; PhaseStart = Now; Shots = 0;
				return false;
			case 2:   // walk into it twice
				if (Shots < 2)
				{
					if (Now - PhaseStart < 1.2f * Shots + 0.5f) return false;
					const FVector Front = N->GetActorLocation() + N->GetActorForwardVector() * 150.f;
					Player->SetActorLocation(Front, false, nullptr, ETeleportType::TeleportPhysics);
					Player->GetCharacterMovement()->StopMovementImmediately();
					Player->GetCharacterMovement()->Velocity = (N->GetActorLocation() - Front).GetSafeNormal2D() * 420.f;
					++Shots;
					Step(TEXT("bump"), Now);
					return false;
				}
				if (Now - PhaseStart < 3.f) return false;
				Phase = 3; PhaseStart = Now; Shots = 0;
				return false;
			case 3:   // energy from the front, rising charge, every 1.6 s whether or not it has settled (as in play)
			{
				static const float Charges[] = { 0.3f, 0.3f, 0.6f, 1.f, 0.6f, 0.6f, 1.f, 1.f, 0.3f, 1.f, 0.6f, 1.f };
				if (Shots < int32(UE_ARRAY_COUNT(Charges)))
				{
					if (Now - PhaseStart < 1.6f * Shots) return false;
					if (URPGCharacterStatsComponent* Stats = N->Stats) if (Shots % 4 == 0) Stats->SetStatBaseByName(TEXT("Health"), 150.f);
					const float Charge = Charges[Shots];
					FRPGEnergyImpact Impact;
					Impact.Attacker = Player;
					Impact.Direction = (-N->GetActorForwardVector()).RotateAngleAxis(Shots % 2 ? 30.f : -30.f, FVector::UpVector);
					Impact.Charge = Charge;
					Impact.Damage = FMath::Lerp(6.f, 28.f, Charge);
					Impact.Impulse = FMath::Lerp(1200.f, 5000.f, Charge);
					Impact.Hit.BoneName = Shots % 3 == 0 ? TEXT("upperarm_r") : Shots % 3 == 1 ? TEXT("thigh_l") : TEXT("spine_05");
					Impact.Hit.ImpactPoint = N->GetMesh()->GetBoneLocation(Impact.Hit.BoneName);
					IRPGEnergySpellTarget::Execute_ReceiveEnergyHit(N, Impact);
					Step(*FString::Printf(TEXT("energy %.1f on %s -> %s"), Charge, *Impact.Hit.BoneName.ToString(),
						*UEnum::GetDisplayValueAsText(N->LastPowerResult.Reaction).ToString()), Now);
					++Shots;
					return false;
				}
				if (Now - PhaseStart < 1.6f * Shots + 2.f) return false;
				Phase = 4; PhaseStart = Now; Shots = 0;
				return false;
			}
			case 4:   // quick punches from the front
				if (Shots < 6)
				{
					if (Now - PhaseStart < 0.45f * Shots) return false;
					FRPGMeleeHitInfo Info;
					Info.AttackName = TEXT("Stress Punch");
					Info.Attacker = Player;
					Info.HitActor = N;
					Info.HitBone = Shots % 2 ? TEXT("head") : TEXT("spine_03");
					Info.ImpactLocation = N->GetMesh()->GetBoneLocation(Info.HitBone);
					Info.StrikeDirection = -N->GetActorForwardVector();
					Info.bCharged = Shots == 5;
					Info.ChargePercent = Shots == 5 ? 1.f : 0.f;
					IRPGMeleeTarget::Execute_ReceiveMeleeHit(N, Info);
					Step(*FString::Printf(TEXT("punch on %s -> %s"), *Info.HitBone.ToString(), *UEnum::GetDisplayValueAsText(N->LastPowerResult.Reaction).ToString()), Now);
					++Shots;
					return false;
				}
				Phase = 5; PhaseStart = Now;
				return false;
			case 5:   // everything must settle: nothing stuck down, busy or mid-reaction
			{
				if (!bSettled && Now - PhaseStart < 20.f) return false;
				const bool bPass = bSettled;
				const FString Line = FString::Printf(TEXT("STRESS %s  ran %d steps to the end, NPC settled %d (down %d busy %d)"),
					bPass ? TEXT("PASS") : TEXT("FAIL"), Steps, bSettled, bNpcDown, N->IsBusy());
				UE_LOG(LogRPGInput, Display, TEXT("%s"), *Line);
				if (bPass) Test->AddInfo(Line); else Test->AddError(Line);
				return true;
			}
			}
			return false;
		}

	private:
		void Step(const TCHAR* What, float Now)
		{
			++Steps;
			ARPGNpcCharacter* N = Npc.Get();
			UE_LOG(LogRPGInput, Display, TEXT("STRESS step %d t=%.2f %s  (NPC at %s)"), Steps, Now, What, N ? *N->GetActorLocation().ToCompactString() : TEXT("?"));
			GLog->Flush();
		}
		bool Fail(const TCHAR* Why) { Test->AddError(FString::Printf(TEXT("STRESS could not run: %s"), Why)); return true; }

		FAutomationTestBase* Test;
		TWeakObjectPtr<ARPGNpcCharacter> Npc;
		FVector Dir = FVector::ForwardVector;
		int32 Frames = 0, Phase = 0, Shots = 0, Steps = 0;
		float PhaseStart = 0.f;
		bool bSawDown = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGReactionStressTest, "RPG.Npc.ReactionStress", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRPGReactionStressTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FRPGReactionStressCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
