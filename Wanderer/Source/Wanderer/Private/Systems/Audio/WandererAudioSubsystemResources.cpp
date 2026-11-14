#include "Systems/Audio/WandererAudioSubsystem.h"

#include "Engine/StreamableManager.h"

#include "AkAudioEvent.h"

#include "Systems/Audio/AudioEventLeaseManager.h"
#include "Systems/Audio/AudioSchedulingStateTypes.h"

namespace
{

    const TCHAR* GetPhaseName(FWandererAudioResourceState::EPhase InPhase)
    {
        static constexpr const TCHAR* Names[] = {
            TEXT("Dormant"), TEXT("Loading"), TEXT("Active"), TEXT("Draining")};
        return Names[static_cast<int32>(InPhase)];
    }
} // namespace

FString UWandererAudioSubsystem::GetResourceSummary() const
{
    FString Result = FString::Printf(TEXT("\nManaged resource Events: %d | %s"),
                                     EventLeaseManager ? EventLeaseManager->GetEventCount() : 0,
                                     *ResourceConfigError);
    for (const TPair<FName, FWandererAudioResourceState>& Pair : ResourceState->Groups)
    {
        const FWandererAudioResourceState& State = Pair.Value;
        Result += FString::Printf(TEXT("\n%s: %s | %.1fm | Preloaded=%d | %s"),
                                  *Pair.Key.ToString(),
                                  GetPhaseName(State.Phase),
                                  State.DistanceM,
                                  State.Leases.Num(),
                                  *State.Error);
    }

    return Result;
}

bool UWandererAudioSubsystem::IsResourceGroupActive(FName Group) const
{
    if (Group.IsNone())
    {
        return true;
    }

    const FWandererAudioResourceState* State = ResourceState->Groups.Find(Group);
    return State && State->Phase == FWandererAudioResourceState::EPhase::Active;
}

bool UWandererAudioSubsystem::IsEventDataResident(UAkAudioEvent* Event) const
{
    return IsValid(Event) && Event->IsLoaded() && Event->IsDataFullyLoaded();
}

FString UWandererAudioSubsystem::GetResourceGroupStatus(FName Group) const
{
    if (Group.IsNone())
    {
        return TEXT("Independent (None)");
    }
    if (!ResourceConfigError.IsEmpty())
    {
        return ResourceConfigError;
    }
    if (!bResourcesInitialized)
    {
        return TEXT("Resource groups not initialized");
    }
    const FWandererAudioResourceState* State = ResourceState->Groups.Find(Group);
    if (!State)
    {
        return FString::Printf(TEXT("Unknown resource group '%s'"), *Group.ToString());
    }

    return State->Error.IsEmpty() ? FString(GetPhaseName(State->Phase)) : State->Error;
}

int32 UWandererAudioSubsystem::GetResourceGroupLimit(FName Group) const
{
    for (const FWandererAudioResourceGroup& Config : ResourceConfig)
    {
        if (Config.Name == Group)
        {
            return Config.MaxEvents;
        }
    }

    return 0;
}

bool UWandererAudioSubsystem::AcquireEventResource(UAkAudioEvent* InEvent)
{
    check(IsInGameThread());
    if (bDeinitializing)
    {
        return false;
    }
    if (!EventLeaseManager)
    {
        EventLeaseManager = NewObject<UAudioEventLeaseManager>(this);
    }
    return EventLeaseManager->Acquire(InEvent);
}

void UWandererAudioSubsystem::ReleaseEventResource(UAkAudioEvent* InEvent)
{
    check(IsInGameThread());
    if (EventLeaseManager)
    {
        EventLeaseManager->Release(InEvent);
    }
}

void UWandererAudioSubsystem::ReleaseGroup(FWandererAudioResourceState& InOutState)
{
    for (const TWeakObjectPtr<UAkAudioEvent>& EventWeakPtr : InOutState.Leases)
    {
        if (EventWeakPtr.IsValid())
        {
            ReleaseEventResource(EventWeakPtr.Get());
        }
    }

    InOutState.Leases.Reset();
    if (InOutState.LoadHandle)
    {
        InOutState.LoadHandle->CancelHandle();
    }

    InOutState.LoadHandle.Reset();
    InOutState.Phase = FWandererAudioResourceState::EPhase::Dormant;
}

void UWandererAudioSubsystem::ShutdownResources()
{
    for (TPair<FName, FWandererAudioResourceState>& Pair : ResourceState->Groups)
    {
        ReleaseGroup(Pair.Value);
    }

    if (EventLeaseManager)
    {
        EventLeaseManager->Stop();
        EventLeaseManager = nullptr;
    }

    ResourceState->Groups.Reset();
    ResourceConfig.Reset();
    SuspendedComponents.Reset();
    bResourcesInitialized = false;
}
