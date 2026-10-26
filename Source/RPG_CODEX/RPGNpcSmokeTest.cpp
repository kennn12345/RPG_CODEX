// RPG.Npc.Smoke: plays the dummy field, checks every GASP NPC is AI-controlled and walks (navigation mesh), then gives
// one a normal punch (a give + maybe a step, stays up) and another a Strength 14 punch (ragdoll, then a get-up where it
// lies, back on its feet and walking again). Logs NPCSMOKE lines.
//   UnrealEditor-Cmd RPG_CODEX.uproject /Game/RPG/Maps/L_DummyField -ExecCmds="Automation RunTests RPG.Npc.Smoke"
//   -TestExit="Automation Test Queue Empty" -unattended -nullrhi

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "RPG_CODEX.h"
#include "RPGCharacterStatsComponent.h"
#include "RPGNpcCharacter.h"
#include "RPGNpcAIController.h"
#include "RPGPhysicsReactionComponent.h"
#include "Editor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "UnrealClient.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

namespace
{
	class FRPGNpcSmokeCommand : public IAutomationLatentCommand
	{
	public:
		explicit FRPGNpcSmokeCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World) return ++Frames > 600 ? Fail(TEXT("no play world")) : false;
			const float Now = World->GetTimeSeconds();
			if (Start < 0.f)
			{
				for (TActorIterator<ARPGNpcCharacter> It(World); It; ++It) { Npcs.Add(*It); StartSpots.Add(It->GetActorLocation()); }
				if (Npcs.Num() == 0) return ++Frames > 600 ? Fail(TEXT("no NPCs in the level")) : false;
				Start = Now;
				return false;
			}
			if (Phase == 0 && Now - Start >= 10.f)
			{
				// Walking: AI-controlled and moved from where it was placed.
				int32 Walked = 0;
				for (int32 I = 0; I < Npcs.Num(); ++I)
				{
					ARPGNpcCharacter* Npc = Npcs[I].Get();
					const bool bAI = Npc && Cast<ARPGNpcAIController>(Npc->GetController());
					const float Moved = Npc ? float(FVector::Dist2D(Npc->GetActorLocation(), StartSpots[I])) : 0.f;
					if (bAI && Moved > 60.f) ++Walked;
					UE_LOG(LogRPGInput, Display, TEXT("NPCSMOKE walk %s: AI %d, moved %.0f cm"), Npc ? *Npc->GetActorNameOrLabel() : TEXT("?"), bAI, Moved);
				}
				bWalkOk = Walked >= FMath::Max(1, Npcs.Num() - 1);
				// Hits: a normal punch on the first, a strong one on the second.
				ACharacter* Player = UGameplayStatics::GetPlayerCharacter(World, 0);
				if (URPGCharacterStatsComponent* PlayerStats = URPGCharacterStatsComponent::FindStats(Player)) PlayerStats->SetStatBaseByName(TEXT("Strength"), 5.f);
				Hit(Player, Npcs[0].Get(), TEXT("spine_05"));
				if (Npcs.Num() > 1)
				{
					if (URPGCharacterStatsComponent* PlayerStats = URPGCharacterStatsComponent::FindStats(Player)) PlayerStats->SetStatBaseByName(TEXT("Strength"), 14.f);
					Hit(Player, Npcs[1].Get(), TEXT("spine_05"));
				}
				Phase = 1; HitAt = Now;
				return false;
			}
			if (Phase == 1 && FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_SHOT_NPC")) == TEXT("1") && Npcs[0].IsValid() && Now - HitAt <= 2.f && Now - LastShot >= 0.1f)
			{
				// RPG_SHOT_NPC=1 (rendered editor): film the normal punch's reaction from the side, every 0.1 s for 2 s.
				ARPGNpcCharacter* Target = Npcs[0].Get();
				if (!Camera)
				{
					Camera = World->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator);
					Camera->GetCameraComponent()->SetFieldOfView(40.f);
					Camera->GetCameraComponent()->bConstrainAspectRatio = false;
					if (APlayerController* PC = World->GetFirstPlayerController()) PC->SetViewTarget(Camera);
				}
				const FVector Look = Target->GetActorLocation() + FVector(0.f, 0.f, 10.f);
				const FVector Eye = Look + Target->GetActorRightVector() * 380.f + FVector(0.f, 0.f, 40.f);
				Camera->SetActorLocationAndRotation(Eye, (Look - Eye).Rotation());
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / FString::Printf(TEXT("Screenshots/NpcHit/hit_%02d_%.1fs.png"), Shot++, Now - HitAt), false, false);
				LastShot = Now;
			}
			if (Phase == 1)
			{
				ARPGNpcCharacter* Light = Npcs[0].Get();
				ARPGNpcCharacter* Heavy = Npcs.Num() > 1 ? Npcs[1].Get() : nullptr;
				if (Light && Light->PhysicsReaction->GetReactionState() == ERPGPhysicsReactionState::Limp) bLightWentLimp = true;
				if (Heavy && Heavy->PhysicsReaction->GetReactionState() == ERPGPhysicsReactionState::Limp) bHeavyWentLimp = true;
				if (Heavy && bHeavyWentLimp && !Heavy->IsBusy() && Heavy->PhysicsReaction->GetReactionState() == ERPGPhysicsReactionState::Animated) bHeavyRecovered = true;
				if (!bHeavyRecovered && Now - HitAt < 16.f) return false;
				const float HeavyZ = Heavy ? float(Heavy->GetActorLocation().Z) : 0.f;
				const bool bPass = bWalkOk && !bLightWentLimp && (Npcs.Num() < 2 || (bHeavyWentLimp && bHeavyRecovered));
				const FString Line = FString::Printf(TEXT("NPCSMOKE %s  %d NPCs, walking ok %d, light punch stayed standing %d (%s), strong punch ragdolled %d (%s) and got back up %d at Z %.0f"),
					bPass ? TEXT("PASS") : TEXT("FAIL"), Npcs.Num(), bWalkOk, !bLightWentLimp,
					Light ? *UEnum::GetDisplayValueAsText(Light->LastPowerResult.Reaction).ToString() : TEXT("-"), bHeavyWentLimp,
					Heavy ? *UEnum::GetDisplayValueAsText(Heavy->LastPowerResult.Reaction).ToString() : TEXT("-"), bHeavyRecovered, HeavyZ);
				UE_LOG(LogRPGInput, Display, TEXT("%s"), *Line);
				if (bPass) Test->AddInfo(Line); else Test->AddError(Line);
				return true;
			}
			return false;
		}

	private:
		void Hit(AActor* Attacker, ARPGNpcCharacter* Npc, const TCHAR* Bone)
		{
			if (!Npc) return;
			FRPGMeleeHitInfo Info;
			Info.AttackName = TEXT("Smoke Punch");
			Info.Attacker = Attacker;
			Info.HitActor = Npc;
			Info.HitBone = Bone;
			Info.ImpactLocation = Npc->GetMesh()->GetBoneLocation(Info.HitBone);
			Info.StrikeDirection = -Npc->GetActorForwardVector();
			IRPGMeleeTarget::Execute_ReceiveMeleeHit(Npc, Info);
		}
		bool Fail(const TCHAR* Why) { Test->AddError(FString::Printf(TEXT("NPCSMOKE could not run: %s"), Why)); return true; }

		FAutomationTestBase* Test;
		TArray<TWeakObjectPtr<ARPGNpcCharacter>> Npcs;
		TArray<FVector> StartSpots;
		int32 Frames = 0, Phase = 0;
		float Start = -1.f, HitAt = 0.f, LastShot = -1.f;
		int32 Shot = 0;
		ACameraActor* Camera = nullptr;
		bool bWalkOk = false, bLightWentLimp = false, bHeavyWentLimp = false, bHeavyRecovered = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNpcSmokeTest, "RPG.Npc.Smoke", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRPGNpcSmokeTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FRPGNpcSmokeCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
