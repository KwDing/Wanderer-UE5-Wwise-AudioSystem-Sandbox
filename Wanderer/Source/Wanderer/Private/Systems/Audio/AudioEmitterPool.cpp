#include "Systems/Audio/AudioEmitterPool.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"

#include "AkAudioEvent.h"
#include "AkComponent.h"

#include "Interfaces/Audio/AudioPlaybackService.h"
#include "Systems/Audio/AudioPooledEmitterComponent.h"
#include "Systems/Audio/WwiseAudioBackend.h"

namespace
{
    constexpr int32 EmitterWarmupBatch = 4;

    bool ResetPooledEmitter(UAudioPooledEmitterComponent& InEmitter, bool bInRadio)
    {
        InEmitter.SetComponentTickEnabled(false);
        InEmitter.SetComponentTickInterval(0);
        InEmitter.DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
        InEmitter.SetWorldTransform(FTransform::Identity);
        InEmitter.ClearReverbCache();
        return WandererAudioBackend::ResetPooledEmitterParameters(InEmitter, bInRadio);
    }
}

void UAudioEmitterPool::ConfigureBudget(const FAudioBudgetLimits& InLimits)
{
    check(IsInGameThread());
    bBudgetConfigured = true;
    RegularLimit = FMath::Clamp(InLimits.RegularSlots, 1, 64);
    WorldCapacity = RegularLimit + FMath::Clamp(InLimits.TransitionSlots, 0, 16);
    EmergencyCapacity = FMath::Clamp(InLimits.EmergencySlots, 0, 8);
    RequestedSlots = WorldCapacity + EmergencyCapacity;
}

FAudioEmitterPoolStats UAudioEmitterPool::GetStats() const
{
    FAudioEmitterPoolStats Result;
    Result.Capacity = Slots.Num();
    for (const FSlot& Slot : Slots)
    {
        Result.WorldActive += Slot.Lane == EAudioEmitterLane::World
            && Slot.State == ESlotState::Active;
        Result.WorldRetiring += Slot.Lane == EAudioEmitterLane::World
            && Slot.State == ESlotState::PendingRelease;
        Result.EmergencyActive += Slot.Lane == EAudioEmitterLane::Emergency
            && Slot.State == ESlotState::Active;
    }
    for (const FRelease& Release : Pending)
    {
        Result.WorldRetiring += !Release.bRadio && Release.PlayingID
            && !Release.ComponentWeakPtr.IsValid();
    }
    return Result;
}

// 凑齐并注册成功才借出，避免半批占槽
bool UAudioEmitterPool::AcquireBatch(
    const UObject& InSource, int32 InCount, TArray<UAkComponent*>& OutComponents)
{
    check(IsInGameThread());
    check(OutComponents.IsEmpty());
    if (bStopped || InCount <= 0)
    {
        return false;
    }
    if (bBudgetConfigured && GetStats().WorldActive + InCount > RegularLimit)
    {
        return false;
    }
    TArray<FSlot*, TInlineAllocator<4>> Available;
    for (bool bAllowReserved : {false, true})
    {
        for (FSlot& Slot : Slots)
        {
            if ((!bBudgetConfigured || Slot.Lane == EAudioEmitterLane::World)
                && Slot.State == ESlotState::Free && Slot.ComponentWeakPtr.IsValid()
                && (Slot.Source.ResolveObjectPtr() != nullptr) == bAllowReserved)
            {
                Available.Add(&Slot);
                if (Available.Num() == InCount)
                {
                    if (!RegisterBatch(Available))
                    {
                        return false;
                    }
                    for (FSlot* Chosen : Available)
                    {
                        Chosen->Source = FObjectKey(&InSource);
                        Chosen->ParkedGroup = NAME_None;
                        Chosen->State = ESlotState::Active;
                        OutComponents.Add(Chosen->ComponentWeakPtr.Get());
                    }
                    return true;
                }
            }
        }
    }
    RequestedSlots = FMath::Max(RequestedSlots, InCount - Available.Num());
    return false;
}

void UAudioEmitterPool::Warmup(UWorld& InWorld, int32 InCapacity)
{
    check(IsInGameThread());
    if (bStopped || !WandererAudioPlayback::IsReady(InWorld))
    {
        return;
    }
    if (!Host)
    {
        FActorSpawnParameters Parameters;
        Parameters.ObjectFlags |= RF_Transient;
        Host = InWorld.SpawnActor<AActor>(Parameters);
        RequestedSlots = FMath::Max(RequestedSlots, EmitterWarmupBatch);
    }
    Slots.RemoveAllSwap([](const FSlot& Slot) { return !Slot.ComponentWeakPtr.IsValid(); });
    const int32 Capacity = bBudgetConfigured
        ? WorldCapacity + EmergencyCapacity : InCapacity;
    const int32 Count = FMath::Max(0,
        FMath::Min3(RequestedSlots, EmitterWarmupBatch, Capacity - Slots.Num()));
    RequestedSlots = 0;
    for (int32 Index = 0; Host && Index < Count; ++Index)
    {
        EAudioEmitterLane Lane = EAudioEmitterLane::World;
        if (bBudgetConfigured)
        {
            int32 ExistingWorld = 0;
            for (const FSlot& Existing : Slots)
            {
                ExistingWorld += Existing.Lane == EAudioEmitterLane::World;
            }
            Lane = ExistingWorld < WorldCapacity ? EAudioEmitterLane::World
                : EAudioEmitterLane::Emergency;
        }
        AddSlot(Lane);
    }
}

// 复用已创建的组件，降低短音突发时的分配开销
UAkComponent* UAudioEmitterPool::Acquire(
    const UObject& InSource, bool bInPreferPrevious, EAudioEmitterLane InLane)
{
    check(IsInGameThread());
    if (bStopped)
    {
        return nullptr;
    }
    if (bBudgetConfigured && InLane == EAudioEmitterLane::World
        && GetStats().WorldActive >= RegularLimit)
    {
        return nullptr;
    }
    const FObjectKey Source(&InSource);
    FSlot* Unreserved = nullptr;
    FSlot* Reserved = nullptr;
    for (FSlot& Slot : Slots)
    {
        if ((bBudgetConfigured && Slot.Lane != InLane)
            || Slot.State != ESlotState::Free || !Slot.ComponentWeakPtr.IsValid())
        {
            continue;
        }
        if (bInPreferPrevious && Slot.Source == Source)
        {
            Unreserved = &Slot;
            break;
        }
        if (!Slot.Source.ResolveObjectPtr())
        {
            Unreserved = &Slot;
        }
        else if (!Reserved)
        {
            Reserved = &Slot;
        }
    }
    FSlot* Slot = Unreserved ? Unreserved : Reserved;
    if (!Slot)
    {
        ++RequestedSlots;
        return nullptr;
    }
    UAudioPooledEmitterComponent* Component = Slot->ComponentWeakPtr.Get();
    if (!Component->EnsureWwiseRegistration())
    {
        return nullptr;
    }
    Slot->Source = Source;
    Slot->ParkedGroup = NAME_None;
    Slot->State = ESlotState::Active;
    return Component;
}

void UAudioEmitterPool::BeginRelease(const UObject& InSource,
                                    UAkComponent* InComponent,
                                    UAkAudioEvent* InEvent,
                                    uint32 InPlayingID,
                                    int32 InFadeMs,
                                    float InTailSec,
                                    FName InGroup,
                                    bool bInOwnsMedia,
                                    bool bInPreferPrevious)
{
    check(IsInGameThread());
    FRelease& Release = QueueRelease(InSource, InComponent);
    Release.Event.Reset(InEvent);
    Release.Group = InGroup;
    Release.PlayingID = InPlayingID;
    Release.TailSec = InPlayingID ? InTailSec : 0;
    Release.bOwnsMedia = bInOwnsMedia;
    Release.bPreferPrevious = bInPreferPrevious;
    if (UAkComponent* Emitter = Release.ComponentWeakPtr.Get())
    {
        Emitter->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
    }
    WandererAudioBackend::StopPlayback(InPlayingID, InFadeMs);
}

void UAudioEmitterPool::BeginRadioRelease(const UObject& InSource, UAkComponent* InComponent,
    UAkAudioEvent* InPlayEvent, uint32 InPlayID, UAkAudioEvent* InStopEvent, uint32 InStopID)
{
    check(IsInGameThread());
    if (bStopped)
    {
        return;
    }
    FRelease& Release = QueueRelease(InSource, InComponent);
    Release.Event.Reset(InPlayEvent);
    Release.PlayingID = InPlayID;
    Release.StopEvent.Reset(InStopEvent);
    Release.StopPlayingID = InStopID;
    Release.bRadio = true;
    if (UAkComponent* Emitter = Release.ComponentWeakPtr.Get())
    {
        Emitter->SetComponentTickEnabled(false);
    }
}

void UAudioEmitterPool::BeginObjectRelease(const UObject& InSource, UAkComponent* InComponent,
    const FGuid& InInstanceID, TArray<FObjectEmitterPlayingEvent>&& InPlayingEvents, FName InGroup)
{
    check(IsInGameThread());
    FRelease& Release = QueueRelease(InSource, InComponent);
    Release.ObjectEvents = MoveTemp(InPlayingEvents);
    Release.ObjectInstanceID = InInstanceID;
    Release.Group = InGroup;
    if (UAkComponent* Emitter = Release.ComponentWeakPtr.Get())
    {
        Emitter->SetComponentTickEnabled(false);
    }
}

// 尾音排空再复用，防止旧播放串入新声源
void UAudioEmitterPool::RecycleReadyEmitters(TFunctionRef<void(UAkAudioEvent*)> InReleaseMedia)
{
    const double Now = FPlatformTime::Seconds();
    const auto IsPlaying = [](const UAkAudioEvent* Event, uint32 ID)
    {
        return ID && WandererAudioBackend::IsInitialized()
            && WandererAudioBackend::QueryPlaybackActive(Event, ID).Get(true);
    };
    for (int32 Index = Pending.Num() - 1; Index >= 0; --Index)
    {
        FRelease& Release = Pending[Index];
        const auto IsObjectEventPlaying = [&IsPlaying](const FObjectEmitterPlayingEvent& Event)
        {
            return IsPlaying(Event.Event.Get(), Event.PlayingID);
        };
        if (IsPlaying(Release.Event.Get(), Release.PlayingID)
            || IsPlaying(Release.StopEvent.Get(), Release.StopPlayingID)
            || Release.ObjectEvents.ContainsByPredicate(IsObjectEventPlaying))
        {
            continue;
        }
        if (Release.TailDeadlineSec < 0)
        {
            Release.TailDeadlineSec = Now + Release.TailSec;
        }
        if (Now < Release.TailDeadlineSec)
        {
            continue;
        }
        FRelease Finished = MoveTemp(Release);
        Pending.RemoveAtSwap(Index);
        FinishRelease(Finished);
        if (Finished.bOwnsMedia && Finished.Event)
        {
            InReleaseMedia(Finished.Event.Get());
        }
    }
}

void UAudioEmitterPool::SuspendIdleGroup(FName InGroup)
{
    check(IsInGameThread());
    check(!InGroup.IsNone());
    for (FSlot& Slot : Slots)
    {
        if (Slot.State == ESlotState::Free && Slot.ParkedGroup == InGroup)
        {
            if (UAudioPooledEmitterComponent* Component = Slot.ComponentWeakPtr.Get())
            {
                Component->SuspendWwiseRegistration();
            }
            Slot.ParkedGroup = NAME_None;
        }
    }
}

// 移交待排空的媒体租约，避免销池时提前卸载
void UAudioEmitterPool::Shutdown(
    TFunctionRef<void(UAkAudioEvent*, uint32, double, float)> InRetireMedia)
{
    bStopped = true;
    for (const FRelease& Release : Pending)
    {
        WandererAudioBackend::StopPlayback(Release.PlayingID, 0);
        WandererAudioBackend::StopPlayback(Release.StopPlayingID, 0);
        if (Release.bOwnsMedia && Release.Event)
        {
            InRetireMedia(Release.Event.Get(), Release.PlayingID,
                Release.TailDeadlineSec, Release.TailSec);
        }
    }
    Pending.Reset();
    Slots.Reset();
    if (IsValid(Host))
    {
        Host->Destroy();
    }
    Host = nullptr;
}

bool UAudioEmitterPool::HasPendingSource(const UObject& InSource) const
{
    return Pending.ContainsByPredicate([Source = FObjectKey(&InSource)](const FRelease& Release)
    {
        return Release.Source == Source;
    });
}

bool UAudioEmitterPool::HasPendingGroup(FName InGroup) const
{
    return Pending.ContainsByPredicate([InGroup](const FRelease& Release)
    {
        return Release.Group == InGroup;
    });
}

bool UAudioEmitterPool::HasPendingObjectEmitter(const FGuid& InInstanceID) const
{
    return Pending.ContainsByPredicate([&InInstanceID](const FRelease& Release)
    {
        return Release.ObjectInstanceID == InInstanceID;
    });
}

bool UAudioEmitterPool::RegisterBatch(TConstArrayView<FSlot*> InSlots)
{
    TArray<UAudioPooledEmitterComponent*, TInlineAllocator<4>> Reopened;
    for (const FSlot* Slot : InSlots)
    {
        UAudioPooledEmitterComponent* Component = Slot->ComponentWeakPtr.Get();
        if (Component->IsRegisteredWithWwise())
        {
            continue;
        }
        if (!Component->EnsureWwiseRegistration())
        {
            for (UAudioPooledEmitterComponent* Emitter : Reopened)
            {
                Emitter->SuspendWwiseRegistration();
            }
            return false;
        }
        Reopened.Add(Component);
    }
    return true;
}

void UAudioEmitterPool::AddSlot(EAudioEmitterLane InLane)
{
    UAudioPooledEmitterComponent* Component =
        NewObject<UAudioPooledEmitterComponent>(Host, NAME_None, RF_Transient);
    Host->AddInstanceComponent(Component);
    Component->OcclusionRefreshInterval = 0;
    Component->StopWhenOwnerDestroyed = true;
    Component->PrimaryComponentTick.bStartWithTickEnabled = false;
    Component->RegisterComponent();
    Component->SetComponentTickEnabled(false);
    FSlot& Slot = Slots.AddDefaulted_GetRef();
    Slot.ComponentWeakPtr = Component;
    Slot.Lane = InLane;
}

UAudioEmitterPool::FRelease& UAudioEmitterPool::QueueRelease(
    const UObject& InSource, UAkComponent* InComponent)
{
    const FObjectKey Source(&InSource);
    FRelease& Release = Pending.AddDefaulted_GetRef();
    Release.Source = Source;
    if (InComponent)
    {
        FSlot* Slot = Slots.FindByPredicate([InComponent, Source](const FSlot& Item)
        {
            return Item.State == ESlotState::Active && Item.Source == Source
                && Item.ComponentWeakPtr.Get() == InComponent;
        });
        if (Slot)
        {
            Slot->State = ESlotState::PendingRelease;
            Release.ComponentWeakPtr = InComponent;
        }
    }
    return Release;
}

void UAudioEmitterPool::FinishRelease(const FRelease& InRelease)
{
    UAudioPooledEmitterComponent* Component =
        Cast<UAudioPooledEmitterComponent>(InRelease.ComponentWeakPtr.Get());
    if (!Component)
    {
        return;
    }
    if (!ResetPooledEmitter(*Component, InRelease.bRadio))
    {
        // Keep the slot unavailable until a later warmup replaces it.
        Slots.RemoveAllSwap([Component](const FSlot& Slot)
        {
            return Slot.ComponentWeakPtr.Get() == Component;
        });
        Component->DestroyComponent();
        return;
    }
    if (InRelease.bRadio)
    {
        Component->SuspendWwiseRegistration();
    }
    for (FSlot& Slot : Slots)
    {
        if (Slot.ComponentWeakPtr.Get() == Component)
        {
            Slot.State = ESlotState::Free;
            Slot.Source = InRelease.bPreferPrevious ? InRelease.Source : FObjectKey();
            Slot.ParkedGroup = InRelease.Group;
            return;
        }
    }
}
