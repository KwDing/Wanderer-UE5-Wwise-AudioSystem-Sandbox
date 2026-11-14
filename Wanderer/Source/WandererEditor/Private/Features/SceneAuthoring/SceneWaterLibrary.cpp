#include "Features/SceneAuthoring/SceneWaterLibrary.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace
{
    bool DisconnectLegacyWaterCall(UEdGraphNode* InNode, bool bInUpdater)
    {
        UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(InNode);
        if (!Call)
        {
            return false;
        }
        const FName FunctionName = Call->FunctionReference.GetMemberName();
        const bool bDisable = bInUpdater ? FunctionName == TEXT("K2_SetTimerDelegate")
                                         : FunctionName == TEXT("PlaySoundAtLocation")
                                               || FunctionName == TEXT("Splash")
                                               || FunctionName == TEXT("Create Simulated Splash");
        if (!bDisable)
        {
            return false;
        }
        UEdGraphPin* Execute = Call->FindPin(TEXT("execute"));
        if (!Execute || Execute->LinkedTo.IsEmpty())
        {
            return false;
        }
        Call->Modify();
        Execute->BreakAllPinLinks();
        return true;
    }
} // namespace

bool USceneWaterLibrary::ConfigureIWSWaterBlueprint(UBlueprint* Blueprint)
{
    const FString LocalRoot = TEXT("/Game/Blueprints/Systems/Water/");
    if (!IsInGameThread() || !IsValid(Blueprint) || !Blueprint->GetPathName().StartsWith(LocalRoot))
    {
        return false;
    }
    const bool bIsUpdater = Blueprint->GetName() == TEXT("BP_GlobalUpdater");
    const bool bIsSurface = Blueprint->GetName() == TEXT("BP_WaterSurface");
    if (!bIsUpdater && !bIsSurface)
    {
        return false;
    }
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    int32 DisconnectedNodes = 0;
    for (UEdGraph* Graph : Graphs)
    {
        if (!Graph)
        {
            continue;
        }
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            DisconnectedNodes += DisconnectLegacyWaterCall(Node, bIsUpdater) ? 1 : 0;
        }
    }
    if (DisconnectedNodes == 0)
    {
        return false;
    }
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    return Blueprint->Status != BS_Error;
}
