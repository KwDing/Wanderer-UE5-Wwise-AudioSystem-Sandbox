#include "Systems/Audio/WandererAudioSubsystem.h"

#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"

#include "AkAudioEvent.h"

#include "Interfaces/Audio/AudioSourceInterface.h"
#include "Systems/Audio/AudioEmitterPool.h"
#include "Systems/Audio/AudioSchedulingStateTypes.h"

namespace
{
    constexpr double EventRetryDelaySec = 2.0;
    constexpr double GroupLoadTimeoutSec = 15.0;
    constexpr double GroupRetryDelaySec = 5.0;
} // namespace

void UWandererAudioSubsystem::UpdateGroup(const FWandererAudioResourceGroup& InGroup,
                                          bool bInWanted)
{
    using EPhase = FWandererAudioResourceState::EPhase;
    FWandererAudioResourceState& State = ResourceState->Groups.FindChecked(InGroup.Name);
    if (!bInWanted)
    {
        if (State.Phase == EPhase::Dormant)
        {
            return;
        }
        if (State.Phase == EPhase::Loading)
        {
            ReleaseGroup(State);
            return;
        }
        State.Phase = EPhase::Draining;
        if (IsResourceGroupPlaying(InGroup.Name))
        {
            return;
        }
        if (EmitterPool)
        {
            EmitterPool->SuspendIdleGroup(InGroup.Name);
        }
        SetSpatialResources(InGroup, false);
        ReleaseGroup(State);
        return;
    }

    if (State.Phase == EPhase::Active)
    {
        return;
    }

    if (State.Phase == EPhase::Draining)
    {
        State.Phase = EPhase::Active;
        return;
    }

    AdvanceGroupLoad(InGroup, State);
}

void UWandererAudioSubsystem::AdvanceGroupLoad(const FWandererAudioResourceGroup& InGroup,
                                               FWandererAudioResourceState& InOutState)
{
    using EPhase = FWandererAudioResourceState::EPhase;
    const double Now = GetWorld()->GetTimeSeconds();
    if (InOutState.Phase == EPhase::Dormant)
    {
        if (Now < InOutState.RetryAtSec)
        {
            return;
        }
        BeginGroupLoad(InGroup, InOutState);
    }

    if (Now - InOutState.LoadStartedAtSec > GroupLoadTimeoutSec)
    {
        InOutState.Error = TEXT("Resource load timeout; retrying");
        ReleaseGroup(InOutState);
        InOutState.RetryAtSec = Now + GroupRetryDelaySec;
        return;
    }

    if (InOutState.LoadHandle && !InOutState.LoadHandle->HasLoadCompleted())
    {
        return;
    }

    if (!AcquirePreloadEvents(InGroup, InOutState))
    {
        return;
    }

    for (FName Dependency : InGroup.Dependencies)
    {
        if (!IsResourceGroupActive(Dependency))
        {
            InOutState.Error = TEXT("Waiting for resource dependency");
            return;
        }
    }

    SetSpatialResources(InGroup, true);
    InOutState.Phase = EPhase::Active;
    InOutState.Error.Reset();
}

bool UWandererAudioSubsystem::AcquirePreloadEvents(const FWandererAudioResourceGroup& InGroup,
                                                   FWandererAudioResourceState& InOutState)
{
    bool bAllReady = true;
    bool bAnyFailed = false;
    const double Now = GetWorld()->GetTimeSeconds();
    for (const TSoftObjectPtr<UAkAudioEvent>& Reference : InGroup.PreloadEvents)
    {
        UAkAudioEvent* Event = Reference.Get();
        if (!Event || Event->bAutoLoad)
        {
            InOutState.Error = TEXT("Missing Event / Auto Load must be disabled");
            return false;
        }

        if (!InOutState.Leases.Contains(Event))
        {
            if (Now < InOutState.RetryAtSec || !AcquireEventResource(Event))
            {
                bAllReady = false;
                bAnyFailed = true;
                continue;
            }
            InOutState.Leases.Add(Event);
        }
        const EAudioEventResourceStatus Status = AdvanceEventResource(Event,
            AudioSchedulingDefaults::PreloadPriority - InOutState.DistanceM);
        bAllReady &= Status == EAudioEventResourceStatus::Ready;
        if (Status == EAudioEventResourceStatus::Failed)
        {
            InOutState.Leases.Remove(Event);
            ReleaseEventResource(Event);
            bAnyFailed = true;
        }
    }
    if (bAnyFailed && Now >= InOutState.RetryAtSec)
    {
        InOutState.RetryAtSec = Now + EventRetryDelaySec;
    }
    InOutState.Error = bAnyFailed ? TEXT("Wwise data unavailable")
                                  : bAllReady ? FString() : TEXT("Loading Wwise media");
    return bAllReady;
}

bool UWandererAudioSubsystem::IsResourceGroupPlaying(FName InGroup) const
{
    if (IsObjectGroupPlaying(InGroup))
    {
        return true;
    }
    if (EmitterPool && EmitterPool->HasPendingGroup(InGroup))
    {
        return true;
    }
    for (const TWeakObjectPtr<UObject>& SourceWeakPtr : SourcesWeakPtrs)
    {
        const IAudioSourceInterface* Source = Cast<IAudioSourceInterface>(SourceWeakPtr.Get());
        if (Source && Source->GetAudioResourceGroup() == InGroup && Source->GetPlayingID())
        {
            return true;
        }
    }

    return false;
}

void UWandererAudioSubsystem::BeginGroupLoad(const FWandererAudioResourceGroup& InGroup,
                                             FWandererAudioResourceState& InOutState)
{
    TArray<FSoftObjectPath> Paths;
    for (const TSoftObjectPtr<UAkAudioEvent>& Event : InGroup.PreloadEvents)
    {
        if (!Event.IsNull())
        {
            Paths.AddUnique(Event.ToSoftObjectPath());
        }
    }

    InOutState.LoadHandle =
        Paths.IsEmpty() ? nullptr : UAssetManager::GetStreamableManager().RequestAsyncLoad(
            Paths, FStreamableDelegate(), -10);
    InOutState.LoadStartedAtSec = GetWorld()->GetTimeSeconds();
    InOutState.Phase = FWandererAudioResourceState::EPhase::Loading;
    InOutState.Error.Reset();
}
