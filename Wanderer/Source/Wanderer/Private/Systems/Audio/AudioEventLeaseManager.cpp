#include "Systems/Audio/AudioEventLeaseManager.h"

#include "Containers/Ticker.h"
#include "HAL/PlatformTime.h"
#include "UObject/StrongObjectPtr.h"

#include "AkAudioEvent.h"
#include "Wwise/WwiseResourceLoader.h"

#if WITH_EDITORONLY_DATA
#include "Wwise/WwiseProjectDatabase.h"
#include "Wwise/WwiseResourceCooker.h"
#endif

#include "Systems/Audio/WwiseAudioBackend.h"

namespace
{
    // Weak keys identify assets. EventUsers and this in-flight record provide GC ownership.
    struct FSharedEventLease
    {
        FWwiseLoadedEventFuture Load;
        FWwiseLoadedEventPtr Loaded = nullptr;
        TStrongObjectPtr<UAkAudioEvent> Event;
        int32 Users = 0;
        float Priority = -FLT_MAX;
        double QueuedAtSec = 0;
        uint64 RequestOrder = 0;
        bool bRequested = false;
        EAudioEventResourceStatus Status = EAudioEventResourceStatus::Pending;
    };

    TMap<TWeakObjectPtr<UAkAudioEvent>, FSharedEventLease> SharedEventLeases;
    bool bMediaPumpRunning = false;
    uint64 MediaRequestOrder = 0;

    void ReleaseSharedEvent(const TWeakObjectPtr<UAkAudioEvent>& InEvent)
    {
        FSharedEventLease* Lease = SharedEventLeases.Find(InEvent);
        if (!Lease || --Lease->Users != 0)
        {
            return;
        }
        // Keep an issued request counted until completion, even after its final owner leaves.
        if (Lease->Load.IsValid() && !Lease->Load.IsReady())
        {
            return;
        }
        FSharedEventLease Released = MoveTemp(*Lease);
        SharedEventLeases.Remove(InEvent);
        if (FWwiseResourceLoaderPtr Loader = FWwiseResourceLoader::Get())
        {
            if (Released.Load.IsValid())
            {
                Loader->UnloadEventAsync(MoveTemp(Released.Load));
            }
            else if (Released.Loaded)
            {
                FWwiseLoadedEventPromise Media;
                Media.EmplaceValue(Released.Loaded);
                Loader->UnloadEventAsync(Media.GetFuture());
            }
        }
    }

    FWwiseLoadedEventFuture RequestEventMedia(UAkAudioEvent& InEvent)
    {
        FWwiseResourceLoaderPtr Loader = FWwiseResourceLoader::Get();
        if (!Loader || !Loader->IsEnabled())
        {
            return {};
        }
#if WITH_EDITORONLY_DATA
        IWwiseResourceCooker* Cooker = IWwiseResourceCooker::GetDefault();
        FWwiseProjectDatabase* Database = FWwiseProjectDatabase::Get();
        if (!Cooker || !Database || !Database->IsProjectDatabaseParsed()
            || !Cooker->PrepareCookedData(InEvent.EventCookedData, &InEvent,
                InEvent.GetValidatedInfo(InEvent.EventInfo)))
        {
            return {};
        }
        InEvent.FillMetadata(Database);
#endif
        return Loader->LoadEventAsync(InEvent.EventCookedData);
    }

    // 跨世界限流，避免冷加载请求同时涌入
    bool PumpEventMedia(float)
    {
        check(IsInGameThread());
        int32 InFlight = 0;
        TArray<TWeakObjectPtr<UAkAudioEvent>> Unused;
        TArray<FSharedEventLease*> Queued;
        for (auto& Pair : SharedEventLeases)
        {
            FSharedEventLease& Lease = Pair.Value;
            if (!IsValid(Lease.Event.Get()))
            {
                Lease.Status = EAudioEventResourceStatus::Failed;
            }
            if (Lease.Load.IsValid() && Lease.Load.IsReady())
            {
                Lease.Loaded = Lease.Load.Get();
                Lease.Load.Reset();
                Lease.Status = Lease.Loaded && Lease.Loaded->GetValue().LoadedData.IsLoaded()
                    ? EAudioEventResourceStatus::Ready : EAudioEventResourceStatus::Failed;
            }
            if (Lease.Users == 0)
            {
                if (!Lease.Load.IsValid())
                {
                    Unused.Add(Pair.Key);
                }
            }
            else if (Lease.bRequested && Lease.Status == EAudioEventResourceStatus::Pending
                && !Lease.Load.IsValid())
            {
                Queued.Add(&Lease);
            }
            InFlight += Lease.Load.IsValid();
        }
        for (const auto& EventWeakPtr : Unused)
        {
            // ReleaseSharedEvent consumes one user; the abandoned load has no remaining user.
            SharedEventLeases.FindChecked(EventWeakPtr).Users = 1;
            // Preserve the identity even if explicit destruction made weak Get() return null.
            ReleaseSharedEvent(EventWeakPtr);
        }
        const double Now = FPlatformTime::Seconds();
        Queued.Sort([Now](const FSharedEventLease& A, const FSharedEventLease& B)
        {
            const double ScoreA = A.Priority + FMath::Min(20.0, Now - A.QueuedAtSec);
            const double ScoreB = B.Priority + FMath::Min(20.0, Now - B.QueuedAtSec);
            return ScoreA != ScoreB ? ScoreA > ScoreB
                : A.Event->GetPathName() < B.Event->GetPathName();
        });
        for (FSharedEventLease* Lease : Queued)
        {
            if (InFlight >= AudioSchedulingDefaults::ConcurrentMediaLoads)
            {
                break;
            }
            Lease->Load = RequestEventMedia(*Lease->Event);
            Lease->RequestOrder = ++MediaRequestOrder;
            Lease->Status = Lease->Load.IsValid()
                ? EAudioEventResourceStatus::Pending : EAudioEventResourceStatus::Failed;
            InFlight += Lease->Load.IsValid();
        }
        bMediaPumpRunning = InFlight > 0 || Queued.ContainsByPredicate(
            [](const FSharedEventLease* Lease)
            {
                return Lease->Status == EAudioEventResourceStatus::Pending;
            });
        return bMediaPumpRunning;
    }

} // namespace

bool UAudioEventLeaseManager::Acquire(UAkAudioEvent* InEvent)
{
    check(IsInGameThread());
    if (bStopped || !IsValid(InEvent))
    {
        return false;
    }
    FSharedEventLease& Lease = SharedEventLeases.FindOrAdd(InEvent);
    // 强持事件，避免异步加载期间被回收
    Lease.Event.Reset(InEvent);
    ++Lease.Users;
    ++EventUsers.FindOrAdd(InEvent);
    return true;
}

// 统一排队，让多个世界共享加载额度
EAudioEventResourceStatus UAudioEventLeaseManager::Advance(UAkAudioEvent* InEvent, float InPriority)
{
    check(IsInGameThread());
    if (bStopped || !IsValid(InEvent) || !EventUsers.Contains(InEvent))
    {
        return EAudioEventResourceStatus::Failed;
    }
    FSharedEventLease& Lease = SharedEventLeases.FindChecked(InEvent);
    if (InEvent->IsLoaded() && InEvent->IsDataFullyLoaded())
    {
        return EAudioEventResourceStatus::Ready;
    }
    if (Lease.Status != EAudioEventResourceStatus::Pending)
    {
        return Lease.Status;
    }
    if (!Lease.bRequested)
    {
        Lease.bRequested = true;
        Lease.QueuedAtSec = FPlatformTime::Seconds();
    }
    Lease.Priority = FMath::Max(Lease.Priority, FMath::IsFinite(InPriority) ? InPriority : 0.0f);
    if (!bMediaPumpRunning)
    {
        // Process-owned pump captures no World. Abandoned futures remain counted until completion.
        // SDK shutdown with an unresolved future requires separate engine-exit verification.
        bMediaPumpRunning = true;
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&PumpEventMedia));
    }
    return Lease.Status;
}

uint64 UAudioEventLeaseManager::GetRequestOrder(UAkAudioEvent* InEvent)
{
    const FSharedEventLease* Lease = SharedEventLeases.Find(InEvent);
    return Lease ? Lease->RequestOrder : 0;
}

FString UAudioEventLeaseManager::GetLoadSummary()
{
    int32 Queued = 0;
    int32 Loading = 0;
    for (const auto& Pair : SharedEventLeases)
    {
        Loading += Pair.Value.Load.IsValid();
        Queued += Pair.Value.bRequested && !Pair.Value.Load.IsValid()
            && Pair.Value.Status == EAudioEventResourceStatus::Pending;
    }
    return FString::Printf(TEXT("Media queued=%d in-flight=%d/%d"),
        Queued, Loading, AudioSchedulingDefaults::ConcurrentMediaLoads);
}

bool UAudioEventLeaseManager::IsReady(UAkAudioEvent* InEvent)
{
    check(IsInGameThread());
    if (!IsValid(InEvent))
    {
        return false;
    }
    const FSharedEventLease* Lease = SharedEventLeases.Find(InEvent);
    return (InEvent->IsLoaded() && InEvent->IsDataFullyLoaded())
        || (Lease && Lease->Status == EAudioEventResourceStatus::Ready);
}

uint32 UAudioEventLeaseManager::Post(UAkAudioEvent& InEvent, UAkGameObject* InObject)
{
    check(IsInGameThread());
    if (!IsValid(&InEvent))
    {
        return 0;
    }
    if (InEvent.IsLoaded() && InEvent.IsDataFullyLoaded())
    {
        return WandererAudioBackend::PostIntegratedEvent(InEvent, InObject);
    }
    const FSharedEventLease* Lease = SharedEventLeases.Find(&InEvent);
    return Lease && Lease->Status == EAudioEventResourceStatus::Ready && Lease->Loaded
        ? WandererAudioBackend::PostManagedEvent(Lease->Loaded->GetValue(), InObject)
        : 0;
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
    ReleaseSharedEvent(InEvent);
}

// 切图时由进程持有租约，防止尾音被提前卸载
void UAudioEventLeaseManager::Retire(UAkAudioEvent* InEvent, uint32 InPlayingID,
    double InTailDeadlineSec, float InTailSec)
{
    check(IsInGameThread());
    int32* LocalUsers = EventUsers.Find(InEvent);
    if (!LocalUsers)
    {
        return;
    }
    if (--*LocalUsers == 0)
    {
        EventUsers.Remove(InEvent);
    }
    if (!InPlayingID || !WandererAudioBackend::IsInitialized())
    {
        ReleaseSharedEvent(InEvent);
        return;
    }

    // World teardown transfers one shared reference without retaining the World or its manager.
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
        [Event = TStrongObjectPtr<UAkAudioEvent>(InEvent),
         InPlayingID, DeadlineSec = InTailDeadlineSec, InTailSec](float) mutable
        {
            if (WandererAudioBackend::IsInitialized())
            {
                if (WandererAudioBackend::QueryPlaybackActive(Event.Get(), InPlayingID).Get(true))
                {
                    return true;
                }
                const double Now = FPlatformTime::Seconds();
                if (DeadlineSec < 0)
                {
                    DeadlineSec = Now + InTailSec;
                }
                if (Now < DeadlineSec)
                {
                    return true;
                }
            }
            ReleaseSharedEvent(Event.Get());
            return false;
        }));
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
