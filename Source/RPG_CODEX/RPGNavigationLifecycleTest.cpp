#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "RPGCrowdManager.h"
#include "RPGNpcCharacter.h"
#include "Editor.h"
#include "AIController.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "NavMesh/RecastNavMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

namespace
{
	class FNavigationLog : public FOutputDevice
	{
	public:
		FNavigationLog() { GLog->AddOutputDevice(this); }
		~FNavigationLog() { GLog->RemoveOutputDevice(this); }
		void Serialize(const TCHAR* Message, ELogVerbosity::Type, const FName&) override
		{
			if (FCString::Strstr(Message, TEXT("Unable to find RecastNavMesh instance"))) ++MissingNavWarnings;
		}
		TAtomic<int32> MissingNavWarnings{0};
	};

	class FWalkNavigation : public IAutomationLatentCommand
	{
	public:
		explicit FWalkNavigation(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World) return ++Frames > 600 ? Fail(TEXT("PIE world missing")) : false;
			const float Now = World->GetTimeSeconds();
			if (Started < 0.f) Started = Now;
			if (Now - Started > 12.f) return Fail(TEXT("navigation did not produce a walkable path and actual NPC movement"));
			if (!Npc.IsValid())
				for (TActorIterator<ARPGNpcCharacter> It(World); It; ++It) { Npc = *It; break; }
			if (!Npc.IsValid()) return false;
			ARPGNpcCharacter* N = Npc.Get();
			if (!bWalking)
			{
				UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
				if (!Nav || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World)) return false;
				const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance());
				UCrowdManager* Crowd = UCrowdManager::GetCurrent(World);
				if (!Recast || !Crowd || Crowd->GetNavData() != Recast) return false;
				Test->TestNotNull(TEXT("Project crowd-manager shutdown fix is configured"), Cast<URPGCrowdManager>(Crowd));
				N->bWander = false;
				AAIController* AI = Cast<AAIController>(N->GetController());
				if (!AI) return Fail(TEXT("NPC has no AI controller"));
				AI->StopMovement();
				StartPosition = N->GetActorLocation();
				FNavLocation Goal;
				for (int32 Try = 0; Try < 20; ++Try)
				{
					if (!Nav->GetRandomReachablePointInRadius(StartPosition, 700.f, Goal) || FVector::Dist2D(StartPosition, Goal.Location) < 200.f) continue;
					UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, StartPosition, Goal.Location, N);
					if (!Path || !Path->IsValid() || Path->IsPartial()) continue;
					bWalking = AI->MoveToLocation(Goal.Location, 40.f) != EPathFollowingRequestResult::Failed;
					if (bWalking) break;
				}
				return false;
			}
			if (FVector::Dist2D(StartPosition, N->GetActorLocation()) < 100.f) return false;
			Test->AddInfo(TEXT("Navigation lifecycle: Recast and crowd data valid; NPC followed a complete path for at least 100 cm."));
			return true;
		}
	private:
		bool Fail(const TCHAR* Reason) { Test->AddError(Reason); return true; }
		FAutomationTestBase* Test;
		TWeakObjectPtr<ARPGNpcCharacter> Npc;
		FVector StartPosition;
		float Started = -1.f;
		int32 Frames = 0;
		bool bWalking = false;
	};

	class FCheckNavigationLog : public IAutomationLatentCommand
	{
	public:
		FCheckNavigationLog(FAutomationTestBase* InTest, TSharedRef<FNavigationLog> InLog) : Test(InTest), Log(InLog) {}
		bool Update() override
		{
			Test->TestEqual(TEXT("No crowd navmesh-recreation warnings across two PIE sessions including teardown"), Log->MissingNavWarnings.Load(), 0);
			return true;
		}
	private:
		FAutomationTestBase* Test;
		TSharedRef<FNavigationLog> Log;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNavigationLifecycleTest, "RPG.Npc.NavigationLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRPGNavigationLifecycleTest::RunTest(const FString&)
{
	TSharedRef<FNavigationLog> Log = MakeShared<FNavigationLog>();
	for (int32 Session = 0; Session < 2; ++Session)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
		ADD_LATENT_AUTOMATION_COMMAND(FWalkNavigation(this));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	}
	ADD_LATENT_AUTOMATION_COMMAND(FCheckNavigationLog(this, Log));
	return true;
}

#endif
