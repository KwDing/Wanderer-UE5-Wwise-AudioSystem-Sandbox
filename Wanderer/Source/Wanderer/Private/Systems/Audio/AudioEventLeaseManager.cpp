#include "Systems/Audio/AudioEventLeaseManager.h"

#include "AkAudioEvent.h"

namespace
{
    // Weak keys observe shared assets; each world keeps its own leased Events alive.
    TMap<TWeakObjectPtr<UAkAudioEvent>, int32> GlobalEventUsers;
} // namespace

bool UAudioEventLeaseManager::Acquire(UAkAudioEvent* InEvent)
{
    check(IsInGameThread());
    if (bStopped || !IsValid(InEvent) || InEvent->bAutoLoad)
    {
        return false;
    }
    if (GlobalEventUsers.FindRef(InEvent) == 0)
    {
        InEvent->LoadData();
    }
    if (bStopped || !InEvent->IsLoaded() || !InEvent->IsDataFullyLoaded())
    {
        if (GlobalEventUsers.FindRef(InEvent) == 0)
        {
            GlobalEventUsers.Remove(InEvent);
            InEvent->UnloadData(true);
        }
        return false;
    }
    ++GlobalEventUsers.FindOrAdd(InEvent);
    ++EventUsers.FindOrAdd(InEvent);
    return true;
}

void UAudioEventLeaseManager::Release(UAkAudioEvent* InEvent)
{
    check(IsInGameThread());
    int32* LocalUsers = EventUsers.Find(InEvent);
    if (!LocalUsers)
    {
        // Source EndPlay may arrive after subsystem teardown already drained its lease.
        return;
    }
    if (--*LocalUsers == 0)
    {
        EventUsers.Remove(InEvent);
    }
    int32* GlobalUsers = GlobalEventUsers.Find(InEvent);
    if (!GlobalUsers || --*GlobalUsers != 0)
    {
        return;
    }
    GlobalEventUsers.Remove(InEvent);
    // Commit accounting before handing control back to the Integration.
    if (IsValid(InEvent))
    {
        InEvent->UnloadData(true);
    }
}

void UAudioEventLeaseManager::Stop()
{
    check(IsInGameThread());
    bStopped = true;
    while (!EventUsers.IsEmpty())
    {
        Release(EventUsers.CreateConstIterator().Key());
    }
}

int32 UAudioEventLeaseManager::GetEventCount() const
{
    return EventUsers.Num();
}
