#include "Editing/AssetMaintenanceLibrary.h"

#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphUtilities.h"
#include "Engine/Blueprint.h"
#include "IAssetTools.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "UObject/ObjectRedirector.h"


bool UAssetMaintenanceLibrary::ExportBlueprintGraphs(UBlueprint* Blueprint, const FString& Filename)
{
    if (!IsInGameThread() || !IsValid(Blueprint) || Filename.IsEmpty())
    {
        return false;
    }
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    FString Output;
    for (UEdGraph* Graph : Graphs)
    {
        if (!IsValid(Graph))
        {
            continue;
        }
        TSet<UObject*> Nodes;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (IsValid(Node))
            {
                Nodes.Add(Node);
            }
        }
        FString GraphText;
        FEdGraphUtilities::ExportNodesToText(Nodes, GraphText);
        Output += Graph->GetName() + TEXT("\n") + GraphText;
    }
    return FFileHelper::SaveStringToFile(Output, *Filename);
}

bool UAssetMaintenanceLibrary::FixRedirectors(const FString& Directory)
{
    if (!IsInGameThread() || !Directory.StartsWith(TEXT("/Game/")))
    {
        return false;
    }
    FAssetRegistryModule& Registry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FAssetData> Assets;
    Registry.Get().GetAssetsByPath(FName(*Directory), Assets, true);
    TArray<UObjectRedirector*> Redirectors;
    for (const FAssetData& Asset : Assets)
    {
        if (Asset.IsRedirector())
        {
            if (UObjectRedirector* Redirector = Cast<UObjectRedirector>(Asset.GetAsset()))
            {
                Redirectors.Add(Redirector);
            }
        }
    }
    if (!Redirectors.IsEmpty())
    {
        FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"))
            .Get()
            .FixupReferencers(Redirectors, false);
    }
    return true;
}

void UAssetMaintenanceLibrary::WaitForAssetCompilation()
{
    if (IsInGameThread())
    {
        FAssetCompilingManager::Get().FinishAllCompilation();
    }
}
