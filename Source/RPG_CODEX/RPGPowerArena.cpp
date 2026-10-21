#include "RPGPowerArena.h"

#include "RPG_CODEX.h"
#include "RPGCharacterStatsComponent.h"
#include "RPGCombatTestDummy.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	struct FArenaTarget
	{
		const TCHAR* Label;
		float Mass, Stability, StaggerResistance, Durability, Health;
	};

	const FArenaTarget ArenaTargets[] = {
		{ TEXT("Standard Bandit"),	82.f,	50.f, 50.f, 40.f, 100.f },
		{ TEXT("Light Scout"),		55.f,	40.f, 40.f, 35.f, 70.f },
		{ TEXT("Heavy Brute"),		164.f,	70.f, 60.f, 70.f, 250.f },
		{ TEXT("Steady Veteran"),	90.f,	85.f, 80.f, 55.f, 150.f },
	};

	/** The arena built in this play session (the world is new every time Play starts). */
	TWeakObjectPtr<AStaticMeshActor> GArenaFloor;
	FVector GArenaTop = FVector::ZeroVector;

	void ShowArenaMessage(const FString& Message)
	{
		UE_LOG(LogRPGInput, Log, TEXT("%s"), *Message);
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Cyan, Message);
	}

	void ArenaCommand(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || !World->IsGameWorld())
		{
			ShowArenaMessage(TEXT("rpg.Arena only works while playing (press Play first)."));
			return;
		}
		ACharacter* Player = UGameplayStatics::GetPlayerCharacter(World, 0);
		if (!Player) return;

		const bool bAlreadyBuilt = GArenaFloor.IsValid() && GArenaFloor->GetWorld() == World;
		if (!bAlreadyBuilt)
		{
			// Player at the arena's start (X = 0), the targets in a row 6 m ahead, 6 m apart. Hits throw them along +X.
			GArenaTop = Player->GetActorLocation() + FVector(0.f, 0.f, 10000.f);
			GArenaFloor = RPGPowerArena::BuildFlatFloor(World, GArenaTop, 300.f);

			const int32 Count = UE_ARRAY_COUNT(ArenaTargets);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const FArenaTarget& Target = ArenaTargets[Index];
				const FVector Spot = GArenaTop + FVector(600.f, (Index - (Count - 1) * 0.5f) * 600.f, 0.f);
				const FTransform Transform(FRotator(0.f, 180.f, 0.f), Spot + FVector(0.f, 0.f, 91.f));
				ARPGCombatTestDummy* Dummy = World->SpawnActorDeferred<ARPGCombatTestDummy>(ARPGCombatTestDummy::StaticClass(), Transform);
				if (!Dummy) continue;
				Dummy->bWander = false;
				Dummy->DisplayLabel = Target.Label;
				Dummy->Stats->StartingStats.Mass = Target.Mass;
				Dummy->Stats->StartingStats.Stability = Target.Stability;
				Dummy->Stats->StartingStats.StaggerResistance = Target.StaggerResistance;
				Dummy->Stats->StartingStats.Durability = Target.Durability;
				Dummy->Stats->StartingStats.MaxHealth = Target.Health;
#if WITH_EDITOR
				Dummy->SetActorLabel(Target.Label);
#endif
				Dummy->FinishSpawning(Transform);
			}
		}

		Player->SetActorLocation(GArenaTop + FVector(0.f, 0.f, 120.f), false, nullptr, ETeleportType::ResetPhysics);
		if (APlayerController* Controller = Cast<APlayerController>(Player->GetController()))
			Controller->SetControlRotation(FRotator(-10.f, 0.f, 0.f));

		ShowArenaMessage(bAlreadyBuilt
			? TEXT("Back at the arena start.")
			: TEXT("Calibration arena: Standard Bandit, Light Scout, Heavy Brute, Steady Veteran (left to right). ")
			  TEXT("Change your Strength with rpg.SetStat Player Strength 12. rpg.Arena again returns here."));
	}

	FAutoConsoleCommandWithWorldAndArgs GArenaCommand(
		TEXT("rpg.Arena"),
		TEXT("Build (or return to) the Power System calibration arena: a flat floor with four different dummies."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ArenaCommand));
}

AStaticMeshActor* RPGPowerArena::BuildFlatFloor(UWorld* World, const FVector& Top, float SizeMeters)
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	AStaticMeshActor* Floor = World && Cube ? World->SpawnActor<AStaticMeshActor>(Top - FVector(0.f, 0.f, 50.f), FRotator::ZeroRotator) : nullptr;
	if (!Floor) return nullptr;

	// The cube is 1 m on a side, centred: scaled to SizeMeters x SizeMeters x 1 m, its top is at Top.
	Floor->SetMobility(EComponentMobility::Movable);
	Floor->GetStaticMeshComponent()->SetStaticMesh(Cube);
	Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
	Floor->SetActorScale3D(FVector(SizeMeters, SizeMeters, 1.f));
	return Floor;
}
