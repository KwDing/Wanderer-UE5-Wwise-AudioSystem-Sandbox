#include "Features/AudioManager/InteriorFitAudioSyncService.h"

#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "UObject/UObjectGlobals.h"

#include "InteriorFitArea.h"
#include "InteriorFitSplineGuide.h"

#include "Actors/Audio/WandererAudioAreaEmitter.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Features/AudioManager/InteriorFitGeometryImportOperation.h"

namespace
{
    constexpr float InteriorFitPollIntervalSec = 1.0f;
    constexpr double InteriorFitFallbackScanIntervalSec = 10.0;
}

FInteriorFitAudioSyncService::~FInteriorFitAudioSyncService()
{
    Stop();
}

void FInteriorFitAudioSyncService::Start()
{
    if (Handle.IsValid())
    {
        return;
    }
    bStopped = false;
    ++Generation;
    FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &FInteriorFitAudioSyncService::OnPropertyChanged);
    FEditorDelegates::PostUndoRedo.AddSP(this, &FInteriorFitAudioSyncService::MarkDirty);
    if (GEngine)
    {
        GEngine->OnLevelActorAdded().AddSP(this, &FInteriorFitAudioSyncService::OnActorChanged);
        GEngine->OnLevelActorDeleted().AddSP(this, &FInteriorFitAudioSyncService::OnActorChanged);
        GEngine->OnActorMoved().AddSP(this, &FInteriorFitAudioSyncService::OnActorChanged);
    }
    Handle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateSP(AsShared(), &FInteriorFitAudioSyncService::Tick),
        InteriorFitPollIntervalSec);
}

void FInteriorFitAudioSyncService::Stop()
{
    if (bStopped)
    {
        return;
    }
    bStopped = true;
    ++Generation;
    FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
    FEditorDelegates::PostUndoRedo.RemoveAll(this);
    if (GEngine)
    {
        GEngine->OnLevelActorAdded().RemoveAll(this);
        GEngine->OnLevelActorDeleted().RemoveAll(this);
        GEngine->OnActorMoved().RemoveAll(this);
    }
    FTSTicker::GetCoreTicker().RemoveTicker(Handle);
    Handle.Reset();
    ResetWorldState();
}

bool FInteriorFitAudioSyncService::Tick(float)
{
    if (!bStopped && GEditor && !GEditor->PlayWorld && !IsRunningCommandlet()
        && !IsGarbageCollecting() && !GIsTransacting && !GEditor->IsTransactionActive()
        && !GIsAutomationTesting)
    {
        UWorld* World = GEditor->GetEditorWorldContext().World();
        if (World && World->WorldType == EWorldType::Editor && !World->bIsTearingDown)
        {
            PollWorld(*World, FPlatformTime::Seconds());
        }
    }
    return !bStopped;
}

void FInteriorFitAudioSyncService::PollWorld(UWorld& InWorld, double InNowSec)
{
    if (bStopped || bApplying)
    {
        return;
    }
    if (LastWorld.Get() != &InWorld)
    {
        ResetWorldState();
        LastWorld = &InWorld;
    }
    if (!bDirty && !bNeedsConfirmation && InNowSec < NextScanAtSec
        && (RetryAtSec == 0 || InNowSec < RetryAtSec))
    {
        return;
    }
    bDirty = false;
    NextScanAtSec = InNowSec + InteriorFitFallbackScanIntervalSec;
    Sample(InWorld, InNowSec);
}

void FInteriorFitAudioSyncService::ResetWorldState()
{
    LastWorld.Reset();
    LastSample.Reset();
    AppliedGroups.Reset();
    bDirty = true;
    bNeedsConfirmation = true;
    bHasSample = false;
    NextScanAtSec = 0;
    RetryAtSec = 0;
}

void FInteriorFitAudioSyncService::MarkDirty()
{
    if (IsInGameThread() && !bStopped)
    {
        bDirty = true;
    }
}

void FInteriorFitAudioSyncService::OnActorChanged(AActor* InActor)
{
    if (!IsInGameThread() || !InActor || InActor->GetWorld() != LastWorld.Get())
    {
        return;
    }
    // Deleted actors may already fail IsSource's validity check, and audio Areas inherit the plugin class.
    const bool bSourceClass = InActor->IsA<AInteriorFitSplineGuide>()
        || (InActor->IsA<AInteriorFitArea>() && !InActor->IsA<AWandererAudioAreaEmitter>());
    if (bSourceClass
        || (!bApplying && InActor->FindComponentByClass<UWandererAudioSourceComponent>()))
    {
        MarkDirty();
    }
}

void FInteriorFitAudioSyncService::OnPropertyChanged(UObject* InObject, FPropertyChangedEvent&)
{
    if (!IsInGameThread() || !InObject || InObject->GetWorld() != LastWorld.Get())
    {
        return;
    }
    if (InObject->IsA<ULevel>())
    {
        if (!bApplying)
        {
            MarkDirty();
        }
        return;
    }
    AActor* Actor = Cast<AActor>(InObject);
    Actor = Actor ? Actor : InObject->GetTypedOuter<AActor>();
    if (FInteriorFitGeometryImportOperation::IsSource(Actor)
        || (!bApplying && (InObject->IsA<UWandererAudioSourceComponent>()
            || (Actor && Actor->FindComponentByClass<UWandererAudioSourceComponent>()))))
    {
        MarkDirty();
    }
}
