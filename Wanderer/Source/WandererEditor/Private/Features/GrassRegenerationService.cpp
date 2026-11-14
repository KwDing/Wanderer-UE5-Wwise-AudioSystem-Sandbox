#include "Features/GrassRegenerationService.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"

#include "PCGComponent.h"

#include "Foundation/EditorFeatureRegistration.h"

namespace
{
    constexpr int32 MaxGenerationPasses = 2;
    constexpr float GenerationIntervalSeconds = 2.0f;
    const FName AuthoredGrassTag(TEXT("TrialAutoGrassPCG"));

    FGrassRegenerationService GrassRegeneration;
    FEditorFeatureRegistration Registration(
        TEXT("GrassRegeneration"),
        []()
        {
            GrassRegeneration.Start();
        },
        []()
        {
            GrassRegeneration.Stop();
        });
} // namespace

FGrassRegenerationService::~FGrassRegenerationService()
{
    Stop();
}

void FGrassRegenerationService::Start()
{
    check(IsInGameThread());
    if (bStarted || IsRunningCommandlet())
    {
        return;
    }
    bStarted = true;
    MapOpenedHandle =
        FEditorDelegates::OnMapOpened.AddRaw(this, &FGrassRegenerationService::OnMapOpened);
}

void FGrassRegenerationService::Stop()
{
    check(IsInGameThread());
    bStarted = false;
    FEditorDelegates::OnMapOpened.Remove(MapOpenedHandle);
    MapOpenedHandle.Reset();
    CancelGeneration();
}

void FGrassRegenerationService::OnMapOpened(const FString& InFilename, bool bInTemplate)
{
    CancelGeneration();
    if (!bStarted)
    {
        return;
    }
    RemainingPasses = MaxGenerationPasses;
    // Resolve the world after map transition; the second pass allows collision registration.
    GenerationHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateRaw(this, &FGrassRegenerationService::OnGenerationTick),
        GenerationIntervalSeconds);
}

bool FGrassRegenerationService::OnGenerationTick(float InDeltaTime)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!bStarted || RemainingPasses <= 0 || !IsValid(World) || World->bIsTearingDown
        || World->WorldType != EWorldType::Editor)
    {
        return false;
    }
    const uint32 CurrentGeneration = Generation;
    const TWeakObjectPtr<UWorld> WorldWeakPtr = World;
    bool bRequestedGeneration = false;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        bRequestedGeneration |= RegenerateMissingGrass(**It);
        // Generation callbacks may stop the service or begin another map load.
        if (!bStarted || CurrentGeneration != Generation || !WorldWeakPtr.IsValid()
            || WorldWeakPtr->bIsTearingDown)
        {
            return false;
        }
    }
    --RemainingPasses;
    return bRequestedGeneration && RemainingPasses > 0;
}

void FGrassRegenerationService::CancelGeneration()
{
    ++Generation;
    RemainingPasses = 0;
    FTSTicker::GetCoreTicker().RemoveTicker(GenerationHandle);
    GenerationHandle.Reset();
}

bool FGrassRegenerationService::RegenerateMissingGrass(AActor& InActor)
{
    if (!IsValid(&InActor) || InActor.IsActorBeingDestroyed()
        || !InActor.ActorHasTag(AuthoredGrassTag))
    {
        return false;
    }
    UPCGComponent* Grass = InActor.FindComponentByClass<UPCGComponent>();
    if (!IsValid(Grass))
    {
        return false;
    }
    TInlineComponentArray<UHierarchicalInstancedStaticMeshComponent*> Instances(&InActor);
    const bool bHasSavedGrass = Instances.ContainsByPredicate(
        [](const UHierarchicalInstancedStaticMeshComponent* InComponent)
        {
            return IsValid(InComponent) && InComponent->GetInstanceCount() > 0;
        });
    // Generate(true) dirties the map, so reuse existing authored instances unchanged.
    if (bHasSavedGrass)
    {
        return false;
    }
    Grass->Generate(true);
    return true;
}
