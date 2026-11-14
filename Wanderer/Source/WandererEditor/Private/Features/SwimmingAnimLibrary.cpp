#include "Features/SwimmingAnimLibrary.h"

#include "AnimGraphNode_BlendListByBool.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_Root.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Components/WandererSwimmingAnimInstance.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace
{
    template <typename T> T* AddSwimmingNode(UEdGraph& Graph, int32 X, int32 Y)
    {
        T* Node = NewObject<T>(&Graph);
        Graph.AddNode(Node, false, false);
        Node->CreateNewGuid();
        Node->NodePosX = X;
        Node->NodePosY = Y;
        Node->PostPlacedNewNode();
        return Node;
    }

    UK2Node_VariableGet* AddSwimmingVariable(UEdGraph& Graph, FName Name, int32 Y)
    {
        UK2Node_VariableGet* Node = AddSwimmingNode<UK2Node_VariableGet>(Graph, -600, Y);
        Node->VariableReference.SetSelfMember(Name);
        Node->AllocateDefaultPins();
        return Node;
    }

    bool ConnectSwimmingGraph(UEdGraph& Graph, UEdGraphPin& Result, UBlendSpace& BlendSpace)
    {
        UEdGraphPin* Land = Result.LinkedTo[0];
        UAnimGraphNode_BlendListByBool* Blend =
            AddSwimmingNode<UAnimGraphNode_BlendListByBool>(Graph, -200, 0);
        Blend->NodeComment = TEXT("Wanderer swimming: preserve original land graph");
        Blend->AllocateDefaultPins();
        UAnimGraphNode_BlendSpacePlayer* Player =
            AddSwimmingNode<UAnimGraphNode_BlendSpacePlayer>(Graph, -400, -250);
        Player->SetAnimationAsset(&BlendSpace);
        Player->Node.SetLoop(true);
        Player->AllocateDefaultPins();
        UK2Node_VariableGet* Swimming = AddSwimmingVariable(Graph, TEXT("bSwimmingPose"), -400);
        UK2Node_VariableGet* Speed = AddSwimmingVariable(Graph, TEXT("SwimmingPoseSpeed"), -600);
        const UEdGraphSchema* Schema = Graph.GetSchema();
        auto Connect = [Schema](UEdGraphPin* A, UEdGraphPin* B)
        {
            return A && B && Schema->TryCreateConnection(A, B);
        };
        if (!Connect(Swimming->GetValuePin(), Blend->FindPin(TEXT("bActiveValue")))
            || !Connect(Speed->GetValuePin(), Player->FindPin(TEXT("X")))
            || !Connect(Player->FindPin(TEXT("Pose")), Blend->FindPin(TEXT("BlendPose_0")))
            || !Connect(Land, Blend->FindPin(TEXT("BlendPose_1"))))
        {
            return false;
        }
        Result.BreakAllPinLinks();
        if (!Connect(Blend->FindPin(TEXT("Pose")), &Result))
        {
            Connect(Land, &Result);
            return false;
        }
        return true;
    }
} // namespace

bool USwimmingAnimLibrary::ConfigureSwimmingAnimations(
    UBlueprint* Blueprint, UBlendSpace* BlendSpace, UAnimSequence* Idle, UAnimSequence* Stroke)
{
    UAnimBlueprint* AnimationBlueprint = Cast<UAnimBlueprint>(Blueprint);
    if (!IsInGameThread() || !AnimationBlueprint || !BlendSpace || !Idle || !Stroke
        || !AnimationBlueprint->TargetSkeleton
        || !AnimationBlueprint->ParentClass->IsChildOf(
            UWandererSwimmingAnimInstance::StaticClass()))
    {
        return false;
    }
    UEdGraph* Graph = nullptr;
    UAnimGraphNode_Root* Root = nullptr;
    for (UEdGraph* Candidate : AnimationBlueprint->FunctionGraphs)
    {
        if (Candidate->GetFName() != TEXT("AnimGraph"))
        {
            continue;
        }
        Graph = Candidate;
        for (UEdGraphNode* Node : Candidate->Nodes)
        {
            if (Node->NodeComment.StartsWith(TEXT("Wanderer swimming:")))
            {
                return false;
            }
            if (UAnimGraphNode_Root* CandidateRoot = Cast<UAnimGraphNode_Root>(Node))
            {
                Root = CandidateRoot;
            }
        }
    }
    UEdGraphPin* Result = Root ? Root->FindPin(TEXT("Result")) : nullptr;
    if (!Graph || !Result || Result->LinkedTo.Num() != 1)
    {
        return false;
    }
    for (UAnimSequence* Sequence : {Idle, Stroke})
    {
        Sequence->WaitOnExistingCompression(false);
        // UE 5.5 Sequencer-backed animation models must bind the target rig before track remapping.
        Sequence->GetController().UpdateWithSkeleton(AnimationBlueprint->TargetSkeleton, true);
        if (!Sequence->ReplaceSkeleton(AnimationBlueprint->TargetSkeleton, true))
        {
            return false;
        }
        Sequence->bEnableRootMotion = false;
        Sequence->bForceRootLock = true;
        Sequence->RootMotionRootLock = ERootMotionRootLock::RefPose;
        Sequence->MarkPackageDirty();
    }
    BlendSpace->SetSkeleton(AnimationBlueprint->TargetSkeleton);
    if (BlendSpace->AddSample(Idle, FVector::ZeroVector) == INDEX_NONE
        || BlendSpace->AddSample(Stroke, FVector(100.0f, 0.0f, 0.0f)) == INDEX_NONE)
    {
        return false;
    }
    BlendSpace->ValidateSampleData();
    BlendSpace->ResampleData();
    BlendSpace->MarkPackageDirty();
    if (!ConnectSwimmingGraph(*Graph, *Result, *BlendSpace))
    {
        return false;
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimationBlueprint);
    FKismetEditorUtilities::CompileBlueprint(AnimationBlueprint);
    return AnimationBlueprint->Status != BS_Error;
}
