#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "UObject/UObjectIterator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMotionMatchingIndexTest, "RPG.Animation.MotionMatchingIndexes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRPGMotionMatchingIndexTest::RunTest(const FString&)
{
	using namespace UE::PoseSearch;
	int32 Checked = 0, EmptyTemplates = 0;
	const FString MoverChooser = TEXT("/Game/Characters/UEFN_Mannequin/Animations/Traversal/CHT_TraversalMontages_Mover.CHT_TraversalMontages_Mover:");
	const TSet<FString> EmptyMoverTemplates = { MoverChooser + TEXT("PSD_Hurdles_0"), MoverChooser + TEXT("PSD_Mantles_0"), MoverChooser + TEXT("PSD_Vaults_0") };
	for (TObjectIterator<UPoseSearchDatabase> It; It; ++It)
	{
		if (It->HasAnyFlags(RF_ClassDefaultObject) || !It->GetPathName().StartsWith(TEXT("/Game/"))) continue;
		if (It->GetNumAnimationAssets() == 0 && EmptyMoverTemplates.Contains(It->GetPathName()))
		{
			// These empty embedded Mover chooser templates are loaded alongside the populated CMC chooser.
			// They are not searchable databases and must not be forced through GetSearchIndex (which ensures).
			++EmptyTemplates;
			continue;
		}
		++Checked;
		TestTrue(FString::Printf(TEXT("Search database contains animation assets: %s"), *It->GetPathName()), It->GetNumAnimationAssets() > 0);
		// Distinguish the startup warning's two possible causes: a pending cache task versus a failed index.
		const EAsyncBuildIndexResult Result = FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(*It,
			ERequestAsyncBuildFlag::ContinueRequest | ERequestAsyncBuildFlag::WaitForCompletion);
		TestTrue(FString::Printf(TEXT("Motion-matching index ready: %s"), *It->GetPathName()), Result == EAsyncBuildIndexResult::Success);
	}
	TestTrue(TEXT("Checked the loaded project motion-matching databases"), Checked > 0);
	AddInfo(FString::Printf(TEXT("Motion-matching index audit: %d populated project databases ready; %d known empty embedded Mover templates excluded."), Checked, EmptyTemplates));
	return true;
}
#endif
