#include "RPGGASPAssetLibrary.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/Skeleton.h"
#include "RPGAnimInstance.h"
#include "RPGPrototypeAnimationComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/World.h"

#if WITH_EDITOR
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraph/EdGraphNodeUtils.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#endif

bool URPGGASPAssetLibrary::RebuildBlendSpace(UBlendSpace* BlendSpace)
{
#if WITH_EDITOR
    if (!IsValid(BlendSpace) || !BlendSpace->GetSkeleton()) return false;
    BlendSpace->Modify();
    BlendSpace->ValidateSampleData();
    BlendSpace->ResampleData();
    BlendSpace->MarkPackageDirty();
    return true;
#else
    return false;
#endif
}

FString URPGGASPAssetLibrary::ConfigurePlayerAnimBlueprint(UAnimBlueprint* Blueprint)
{
#if WITH_EDITOR
    if (!Blueprint || !Blueprint->TargetSkeleton || !(Blueprint->GetPathName().StartsWith(TEXT("/Game/RPG/")) || Blueprint->GetPathName() == TEXT("/Game/Blueprints/SandboxCharacter_CMC_ABP.SandboxCharacter_CMC_ABP")))
        return TEXT("ERROR: pass the imported CMC AnimBP or a project-owned RPG copy.");
    if (!Blueprint->ParentClass || !URPGAnimInstance::StaticClass()->IsChildOf(Blueprint->ParentClass))
        return FString::Printf(TEXT("ERROR: incompatible parent %s; preserve the sample and document this blocker."), *GetNameSafe(Blueprint->ParentClass));

    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    UAnimGraphNode_Slot* Default = nullptr;
    for (UEdGraph* Graph : Graphs)
        for (UEdGraphNode* Node : Graph->Nodes)
            if (UAnimGraphNode_Slot* Slot = Cast<UAnimGraphNode_Slot>(Node))
            {
                if (Slot->Node.SlotName == TEXT("UpperBody"))
                    return TEXT("ERROR: UpperBody already exists; inspect the existing setup before changing it.");
                if (Slot->Node.SlotName == TEXT("DefaultSlot"))
                {
                    if (Default) return TEXT("ERROR: multiple DefaultSlot nodes; inspect before changing the graph.");
                    Default = Slot;
                }
            }

    Blueprint->Modify();
    Blueprint->ParentClass = URPGAnimInstance::StaticClass();
    FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (Blueprint->Status == BS_Error) return TEXT("ERROR: RPG parent failed to compile; nothing saved.");

    // Reacquire nodes after reparenting/compilation, which can reconstruct graph nodes and pins.
    Default = nullptr;
    Graphs.Reset();
    Blueprint->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
        for (UEdGraphNode* Node : Graph->Nodes)
            if (UAnimGraphNode_Slot* Slot = Cast<UAnimGraphNode_Slot>(Node))
                if (Slot->Node.SlotName == TEXT("DefaultSlot")) Default = Slot;

    if (!Default)
    {
        UEdGraph* const* Found = Graphs.FindByPredicate([](const UEdGraph* Graph) { return Graph->GetFName() == TEXT("AnimGraph"); });
        if (!Found) return TEXT("ERROR: AnimGraph missing.");
        UAnimGraphNode_Root* Root = nullptr;
        for (UEdGraphNode* Node : (*Found)->Nodes)
            if (UAnimGraphNode_Root* Candidate = Cast<UAnimGraphNode_Root>(Node)) Root = Candidate;
        UEdGraphPin* Input = Root ? Root->FindPin(TEXT("Result"), EGPD_Input) : nullptr;
        if (!Input || Input->LinkedTo.Num() != 1) return TEXT("ERROR: expected one final pose input.");
        UEdGraphPin* Source = Input->LinkedTo[0];
        FGraphNodeCreator<UAnimGraphNode_Slot> Creator(**Found);
        Default = Creator.CreateNode(false);
        Default->Node.SlotName = TEXT("DefaultSlot");
        Creator.Finalize();
        Source->BreakLinkTo(Input);
        const UEdGraphSchema* Schema = (*Found)->GetSchema();
        if (!Schema->TryCreateConnection(Source, Default->FindPin(TEXT("Source"), EGPD_Input)) ||
            !Schema->TryCreateConnection(Default->FindPin(TEXT("Pose"), EGPD_Output), Input))
            return TEXT("ERROR: could not connect DefaultSlot; nothing saved.");
    }

    UEdGraph* Graph = Default->GetGraph();
    UEdGraphPin* SlotOut = Default->FindPin(TEXT("Pose"), EGPD_Output);
    if (!SlotOut || SlotOut->LinkedTo.Num() != 1) return TEXT("ERROR: DefaultSlot must have one downstream pose.");
    UEdGraphPin* Downstream = SlotOut->LinkedTo[0];
    Graph->Modify();
    const int32 X = Default->NodePosX, Y = Default->NodePosY;

    FGraphNodeCreator<UAnimGraphNode_SaveCachedPose> CacheCreator(*Graph);
    UAnimGraphNode_SaveCachedPose* Cache = CacheCreator.CreateNode(false);
    CacheCreator.Finalize();
    Cache->CacheName = TEXT("RPG_GASP_BeforeUpperBody");
    Cache->NodePosX = X + 300; Cache->NodePosY = Y + 250;
    auto MakeUse = [&](int32 PosY)
    {
        FGraphNodeCreator<UAnimGraphNode_UseCachedPose> Creator(*Graph);
        UAnimGraphNode_UseCachedPose* Use = Creator.CreateNode(false);
        Use->SaveCachedPoseNode = Cache;
        Creator.Finalize();
        Use->NodePosX = X + 600; Use->NodePosY = PosY;
        return Use;
    };
    UAnimGraphNode_UseCachedPose* Base = MakeUse(Y);
    UAnimGraphNode_UseCachedPose* UpperSource = MakeUse(Y + 200);
    FGraphNodeCreator<UAnimGraphNode_Slot> UpperCreator(*Graph);
    UAnimGraphNode_Slot* Upper = UpperCreator.CreateNode(false);
    Upper->Node.SlotName = TEXT("UpperBody");
    UpperCreator.Finalize();
    Upper->NodePosX = X + 850; Upper->NodePosY = Y + 200;
    FGraphNodeCreator<UAnimGraphNode_LayeredBoneBlend> BlendCreator(*Graph);
    UAnimGraphNode_LayeredBoneBlend* Blend = BlendCreator.CreateNode(false);
    BlendCreator.Finalize();
    FBranchFilter Filter;
    Filter.BoneName = TEXT("spine_01"); Filter.BlendDepth = 1;
    Blend->Node.LayerSetup[0].BranchFilters = {Filter};
    Blend->Node.bMeshSpaceRotationBlend = true;
    Blend->Node.bBlendRootMotionBasedOnRootBone = true;
    Blend->NodePosX = X + 1150; Blend->NodePosY = Y;

    const UEdGraphSchema* Schema = Graph->GetSchema();
    SlotOut->BreakLinkTo(Downstream);
    const bool bLinked =
        Schema->TryCreateConnection(SlotOut, Cache->FindPin(TEXT("Pose"), EGPD_Input)) &&
        Schema->TryCreateConnection(Base->FindPin(TEXT("Pose"), EGPD_Output), Blend->FindPin(TEXT("BasePose"), EGPD_Input)) &&
        Schema->TryCreateConnection(UpperSource->FindPin(TEXT("Pose"), EGPD_Output), Upper->FindPin(TEXT("Source"), EGPD_Input)) &&
        Schema->TryCreateConnection(Upper->FindPin(TEXT("Pose"), EGPD_Output), Blend->FindPin(TEXT("BlendPoses_0"), EGPD_Input)) &&
        Schema->TryCreateConnection(Blend->FindPin(TEXT("Pose"), EGPD_Output), Downstream);
    if (!bLinked) return TEXT("ERROR: could not connect UpperBody layer; nothing saved.");
    Blueprint->TargetSkeleton->Modify();
    Blueprint->TargetSkeleton->RegisterSlotNode(TEXT("DefaultSlot"));
    Blueprint->TargetSkeleton->RegisterSlotNode(TEXT("UpperBody"));
    Blueprint->TargetSkeleton->SetSlotGroupName(TEXT("UpperBody"), TEXT("RPGUpperBodyGroup"));
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (Blueprint->Status == BS_Error) return TEXT("ERROR: animation hooks failed compilation; nothing saved.");
    Blueprint->MarkPackageDirty();
    return TEXT("SUCCESS: RPG parent, DefaultSlot and cached UpperBody layer from spine_01 compile. Caller must save and test.");
#else
    return TEXT("ERROR: animation setup requires the editor.");
#endif
}


UAnimSequence* URPGGASPAssetLibrary::CreateGuardTestFixture(URPGPrototypeAnimationComponent* Component, UAnimSequence* Sequence)
{
#if WITH_EDITOR
    if (!Component || !Sequence || !Component->GetWorld() || Component->GetWorld()->WorldType != EWorldType::PIE) return nullptr;
    UAnimSequence* Fixture = DuplicateObject<UAnimSequence>(Sequence, Component);
    Fixture->bEnableRootMotion = false;
    Fixture->SetFlags(RF_Transient);
    Component->RightPunch = Fixture;
    Component->RightPunchCharged = Fixture;
    Component->FirstPersonRightPunch = Fixture;
    Component->FirstPersonRightPunchCharged = Fixture;
    Component->UnarmedGuard = Fixture;
    Component->FirstPersonUnarmedGuard = Fixture;
    return Fixture;
#else
    return nullptr;
#endif
}

