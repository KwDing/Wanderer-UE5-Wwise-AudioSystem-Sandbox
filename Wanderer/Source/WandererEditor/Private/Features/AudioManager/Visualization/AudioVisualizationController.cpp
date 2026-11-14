#include "Features/AudioManager/Visualization/AudioVisualizationController.h"

#include "Components/LineBatchComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"

#include "Components/WorldAudio/WandererAudioSourceComponent.h"

namespace
{
    uint32 NextDrawBatchID = AudioVisualizationSettings::FirstOwnedDrawBatchID;
}

FAudioVisualizationController::FAudioVisualizationController() : DrawBatchID(NextDrawBatchID++)
{
    check(IsInGameThread());
}

FAudioVisualizationController::~FAudioVisualizationController()
{
    Stop();
}

void FAudioVisualizationController::Start()
{
    if (bStopped || TickerHandle.IsValid())
    {
        return;
    }
    RefreshSources();
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateSP(this, &FAudioVisualizationController::OnTick),
        AudioVisualizationSettings::TickIntervalSec);
}

void FAudioVisualizationController::Stop()
{
    bStopped = true;
    bEnabled = false;
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    TickerHandle.Reset();
    ClearDrawing();
    SourcesWeakPtr.Reset();
    ObservedWorldWeakPtr.Reset();
}

void FAudioVisualizationController::RefreshSources()
{
    SourcesWeakPtr.Reset();
    UWorld* World = bStopped ? nullptr : ResolveWorld();
    ObservedWorldWeakPtr = World;
    if (!World)
    {
        return;
    }
    // Discovery follows the world, independently of table filters and visible pages.
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        TArray<UWandererAudioSourceComponent*> Sources;
        It->GetComponents(Sources);
        for (UWandererAudioSourceComponent* Source : Sources)
        {
            SourcesWeakPtr.Add(Source);
        }
    }
}

void FAudioVisualizationController::SetEnabled(bool bInEnabled)
{
    if (bStopped)
    {
        return;
    }
    bEnabled = bInEnabled;
    if (!bEnabled)
    {
        ClearDrawing();
    }
}

void FAudioVisualizationController::SetRangeM(float InRangeM)
{
    if (!bStopped && FMath::IsFinite(InRangeM))
    {
        RangeM = FMath::Clamp(
            InRangeM, AudioVisualizationSettings::RangeMinM, AudioVisualizationSettings::RangeMaxM);
    }
}

bool FAudioVisualizationController::IsEnabled() const
{
    return bEnabled;
}

float FAudioVisualizationController::GetRangeM() const
{
    return RangeM;
}

const FString& FAudioVisualizationController::GetSummary() const
{
    return Summary;
}

UWorld* FAudioVisualizationController::ResolveWorld()
{
    if (!GEditor)
    {
        return nullptr;
    }
    return GEditor->PlayWorld ? GEditor->PlayWorld.Get() : GEditor->GetEditorWorldContext().World();
}

bool FAudioVisualizationController::OnTick(float InDeltaSeconds)
{
    if (bStopped)
    {
        return false;
    }
    if (IsGarbageCollecting())
    {
        return true;
    }
    const double Time = FPlatformTime::Seconds();
    if (ObservedWorldWeakPtr.Get() != ResolveWorld() || Time >= NextDiscovery)
    {
        NextDiscovery = Time + AudioVisualizationSettings::DiscoveryIntervalSec;
        RefreshSources();
    }
    if (bEnabled)
    {
        DrawNow();
    }
    return true;
}

void FAudioVisualizationController::ClearDrawing()
{
    UWorld* World = VisualizationWorldWeakPtr.Get();
    if (World && World->PersistentLineBatcher)
    {
        World->PersistentLineBatcher->ClearBatch(DrawBatchID);
    }
    VisualizationWorldWeakPtr.Reset();
    Summary.Reset();
}
