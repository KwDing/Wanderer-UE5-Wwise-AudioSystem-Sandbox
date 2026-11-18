#include "Systems/Audio/WandererAudioSubsystem.h"

#include "Components/SceneComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

#include "AkAudioEvent.h"
#include "AkComponent.h"

#include "Data/Audio/WandererObjectAudioProfile.h"
#include "Core/Diagnostics/DiagnosticRecorder.h"
#include "Interfaces/Audio/AudioPlaybackService.h"
#include "Systems/Audio/AudioEmitterPool.h"
#include "Systems/Audio/AudioEventLeaseManager.h"
#include "Systems/Audio/ObjectAudioStateTypes.h"
#include "Systems/Audio/WwiseAudioBackend.h"

namespace
{
    constexpr int32 ObjectRequestsMax = 128;
    constexpr float ObjectLoopRetryDelaySec = 0.5f;
    bool ResolveObjectAttachment(const UWorld* InWorld, const FObjectAudioAttachment& InAttachment,
        FTransform& OutTransform)
    {
        const USceneComponent* Component = InAttachment.ComponentWeakPtr.Get();
        if (!InWorld || !Component || !Component->IsRegistered() || Component->GetWorld() != InWorld)
        {
            return false;
        }
        FName Socket = InAttachment.Socket;
        if (!Socket.IsNone() && !Component->DoesSocketExist(Socket))
        {
            if (!InAttachment.bFallbackToComponent)
            {
                return false;
            }
            Socket = NAME_None;
        }
        OutTransform = InAttachment.RelativeTransform * Component->GetSocketTransform(Socket);
        return !OutTransform.ContainsNaN();
    }

    bool IsObjectEventPlaying(const FObjectAudioPlayback& InPlayback)
    {
        return WandererAudioPlayback::IsPlaybackActive(InPlayback.LeaseWeakPtr.Get(), InPlayback.PlayingID);
    }

    void StopObjectPlayback(FObjectAudioPlayback& InOutPlayback, int32 InFadeMs = 0)
    {
        if (InOutPlayback.bStopping || !InOutPlayback.PlayingID)
        {
            return;
        }
        InOutPlayback.bStopping = true;
        WandererAudioBackend::StopPlayback(InOutPlayback.PlayingID, InFadeMs);
    }
}

FObjectAudioEmitterHandle UWandererAudioSubsystem::RegisterObjectEmitter(FGuid InInstanceID, AActor& InOwner,
    const FObjectAudioAttachment& InAttachment, const UWandererObjectAudioProfile* InProfile)
{
    check(IsInGameThread());
    FTransform Transform;
    if (bDeinitializing || !InInstanceID.IsValid() || !IsValid(&InOwner) || InOwner.IsActorBeingDestroyed()
        || InOwner.GetWorld() != GetWorld() || ObjectEmitters.Contains(InInstanceID)
        || (InProfile && !InProfile->Validate()) || !ResolveObjectAttachment(GetWorld(), InAttachment, Transform))
    {
        return {};
    }
    auto State = MakeShared<FObjectAudioEmitterState>();
    State->Handle = {InInstanceID, ++ObjectEmitterGeneration, GetWorld()};
    State->OwnerWeakPtr = &InOwner;
    State->Attachment = InAttachment;
    if (InProfile)
    {
        State->Events = InProfile->Events;
        State->LoopLimit = InProfile->LoopLimit;
        State->ShortEventLimit = InProfile->ShortEventLimit;
        State->BasePriority = InProfile->Priority;
        State->ActivationDistanceM = InProfile->ActivationDistanceM;
        State->ReleaseDistanceM = InProfile->ReleaseDistanceM;
        State->FadeInSec = InProfile->FadeInSec;
        State->FadeOutSec = InProfile->FadeOutSec;
        State->ResourceGroup = InProfile->ResourceGroup;
    }
    ObjectEmitters.Add(InInstanceID, State);
    return State->Handle;
}

bool UWandererAudioSubsystem::UpdateObjectEmitter(const FObjectAudioEmitterHandle& InHandle,
    const FObjectAudioAttachment& InAttachment)
{
    check(IsInGameThread());
    FObjectAudioEmitterState* State = FindObjectEmitter(InHandle);
    FTransform Transform;
    if (!State || !State->OwnerWeakPtr.IsValid() || !ResolveObjectAttachment(GetWorld(), InAttachment, Transform))
    {
        return false;
    }
    State->Attachment = InAttachment;
    return true;
}

bool UWandererAudioSubsystem::ResolveObjectEmitter(const FObjectAudioEmitterHandle& InHandle,
    FTransform& OutTransform, const AActor* InOwnerScope) const
{
    check(IsInGameThread());
    const FObjectAudioEmitterState* State = FindObjectEmitter(InHandle);
    const AActor* Owner = State ? State->OwnerWeakPtr.Get() : nullptr;
    return Owner && !Owner->IsActorBeingDestroyed()
        && (!InOwnerScope || Owner == InOwnerScope || Owner->GetOwner() == InOwnerScope)
        && ResolveObjectAttachment(GetWorld(), State->Attachment, OutTransform);
}

EObjectAudioRequestResult UWandererAudioSubsystem::PostObjectEvent(
    const FObjectAudioEmitterHandle& InHandle, FName InEventID)
{
    check(IsInGameThread());
    FTransform Transform;
    FObjectAudioEmitterState* State = FindObjectEmitter(InHandle);
    if (bDeinitializing || !State || !ResolveObjectEmitter(InHandle, Transform))
    {
        return EObjectAudioRequestResult::InvalidEmitter;
    }
    const FObjectAudioEvent* Event = State->Events.FindByPredicate(
        [InEventID](const FObjectAudioEvent& Entry) { return Entry.EventID == InEventID; });
    if (!Event)
    {
        return EObjectAudioRequestResult::UnknownEvent;
    }
    if (State->bEmergency && State->EmitterWeakPtr.IsValid() && !Event->bEmergency)
    {
        return EObjectAudioRequestResult::AtCapacity;
    }
    int32 ClassCount = 0;
    for (const FObjectAudioPlayback& Playback : State->Playbacks)
    {
        if (Event->bLoop && Playback.Definition.EventID == InEventID)
        {
            return EObjectAudioRequestResult::AlreadyRequested;
        }
        ClassCount += Playback.Definition.bLoop == Event->bLoop ? 1 : 0;
    }
    if (ClassCount >= (Event->bLoop ? State->LoopLimit : State->ShortEventLimit)
        || ObjectPlaybackCount >= ObjectRequestsMax)
    {
        return EObjectAudioRequestResult::AtCapacity;
    }
    FObjectAudioPlayback& Playback = State->Playbacks.AddDefaulted_GetRef();
    Playback.Definition = *Event;
    Playback.RequestedAtSec = GetWorld()->GetTimeSeconds();
    bAdmissionDirty = true;
    ++ObjectPlaybackCount;
    ++State->PlaybackRevision;
    return EObjectAudioRequestResult::LoadingMedia;
}

void UWandererAudioSubsystem::StopObjectEvent(const FObjectAudioEmitterHandle& InHandle, FName InEventID)
{
    check(IsInGameThread());
    FObjectAudioEmitterState* State = FindObjectEmitter(InHandle);
    if (!State)
    {
        return;
    }
    for (int32 Index = State->Playbacks.Num() - 1; Index >= 0; --Index)
    {
        FObjectAudioPlayback& Playback = State->Playbacks[Index];
        if (Playback.Definition.EventID != InEventID)
        {
            continue;
        }
        if (Playback.PlayingID)
        {
            StopObjectPlayback(Playback);
            continue;
        }
        if (Playback.LoadHandle)
        {
            Playback.LoadHandle->CancelHandle();
        }
        ReleaseEventResource(Playback.LeaseWeakPtr.Get());
        State->Playbacks.RemoveAt(Index);
        --ObjectPlaybackCount;
        ++State->PlaybackRevision;
    }
}

void UWandererAudioSubsystem::UnregisterObjectEmitter(const FObjectAudioEmitterHandle& InHandle)
{
    check(IsInGameThread());
    if (!FindObjectEmitter(InHandle))
    {
        return;
    }
    TSharedPtr<FObjectAudioEmitterState> State;
    ObjectEmitters.RemoveAndCopyValue(InHandle.InstanceID, State);
    State->bRegistered = false;
    ObjectPlaybackCount -= State->Playbacks.Num();
    TArray<FObjectEmitterPlayingEvent> PlayingEvents;
    for (FObjectAudioPlayback& Playback : State->Playbacks)
    {
        if (Playback.LoadHandle)
        {
            Playback.LoadHandle->CancelHandle();
        }
        UAkAudioEvent* Event = Playback.LeaseWeakPtr.Get();
        if (Event && Playback.PlayingID && EventLeaseManager)
        {
            PlayingEvents.Add({TStrongObjectPtr<UAkAudioEvent>(Event), Playback.PlayingID});
            StopObjectPlayback(Playback);
            EventLeaseManager->Retire(Event, Playback.PlayingID, -1, 0);
        }
        else
        {
            ReleaseEventResource(Event);
        }
    }
    if (EmitterPool && State->EmitterWeakPtr.IsValid())
    {
        EmitterPool->BeginObjectRelease(*this, State->EmitterWeakPtr.Get(),
            InHandle.InstanceID, MoveTemp(PlayingEvents), State->ResourceGroup);
    }
}

FObjectAudioEmitterStats UWandererAudioSubsystem::GetObjectEmitterStats(
    const FObjectAudioEmitterHandle& InHandle) const
{
    check(IsInGameThread());
    FObjectAudioEmitterStats Result;
    if (const FObjectAudioEmitterState* State = FindObjectEmitter(InHandle))
    {
        Result.ConfiguredEvents = State->Events.Num();
        Result.PhysicalSlots = State->EmitterWeakPtr.IsValid() ? 1 : 0;
        Result.bAdmitted = State->bAdmitted;
        Result.bEmergency = State->bEmergency;
        Result.Priority = State->EffectivePriority;
        Result.LastFailedEvent = State->LastFailedEvent;
        Result.LastFailure = State->LastFailure;
        for (const FObjectAudioPlayback& Playback : State->Playbacks)
        {
            if (Playback.PlayingID)
            {
                ++Result.PlayingInstances;
            }
            else
            {
                ++Result.PendingInstances;
            }
        }
    }
    return Result;
}

FObjectAudioEmitterState* UWandererAudioSubsystem::FindObjectEmitter(const FObjectAudioEmitterHandle& InHandle) const
{
    const auto* State = ObjectEmitters.Find(InHandle.InstanceID);
    return InHandle.IsSet() && State
        && InHandle.WorldWeakPtr.HasSameIndexAndSerialNumber((*State)->Handle.WorldWeakPtr)
        && (*State)->Handle.Generation == InHandle.Generation ? State->Get() : nullptr;
}

void UWandererAudioSubsystem::UpdateObjectEmitters(bool bInAllowNewPosts)
{
    TArray<FGuid, TInlineAllocator<8>> InstanceIDs;
    for (const auto& Pair : ObjectEmitters)
    {
        InstanceIDs.Add(Pair.Key);
    }
    for (const FGuid& ID : InstanceIDs)
    {
        const TSharedPtr<FObjectAudioEmitterState> State = ObjectEmitters.FindRef(ID);
        if (!State)
        {
            continue;
        }
        FTransform Transform;
        if (!ResolveObjectEmitter(State->Handle, Transform))
        {
            UnregisterObjectEmitter(State->Handle);
            continue;
        }
        AdvanceObjectPlayback(*State, Transform, bInAllowNewPosts);
    }
}

void UWandererAudioSubsystem::ShutdownObjectEmitters()
{
    while (!ObjectEmitters.IsEmpty())
    {
        const FObjectAudioEmitterHandle Handle = ObjectEmitters.CreateConstIterator().Value()->Handle;
        UnregisterObjectEmitter(Handle);
    }
}

void UWandererAudioSubsystem::AdvanceObjectPlayback(
    FObjectAudioEmitterState& InOutEmitter, const FTransform& InTransform, bool bInAllowNewPosts)
{
    UAkComponent* Emitter = InOutEmitter.EmitterWeakPtr.Get();
    const uint64 Revision = InOutEmitter.PlaybackRevision;
    if (Emitter && !Emitter->GetComponentTransform().Equals(InTransform))
    {
        Emitter->SetWorldTransform(InTransform);
        if (!InOutEmitter.bRegistered || InOutEmitter.PlaybackRevision != Revision || !IsValid(Emitter))
        {
            return;
        }
        WandererAudioBackend::UpdateEmitterRoom(*Emitter, InTransform.GetLocation());
    }

    for (int32 Index = InOutEmitter.Playbacks.Num() - 1; Index >= 0; --Index)
    {
        FObjectAudioPlayback& Playback = InOutEmitter.Playbacks[Index];
        if (Playback.PlayingID && IsObjectEventPlaying(Playback))
        {
            continue;
        }
        if (Playback.PlayingID && Playback.Definition.bLoop && !Playback.bStopping)
        {
            ReleaseEventResource(Playback.LeaseWeakPtr.Get());
            Playback.LeaseWeakPtr.Reset();
            Playback.PlayingID = 0;
            Playback.RetryAtSec = GetWorld()->GetTimeSeconds() + ObjectLoopRetryDelaySec;
            InOutEmitter.LastFailure = EObjectAudioFailure::PlaybackEnded;
            InOutEmitter.LastFailedEvent = Playback.Definition.EventID;
        }
        if (InOutEmitter.bEmergency && !Playback.Definition.bEmergency)
        {
            continue;
        }
        if (!Playback.PlayingID)
        {
            if (!Playback.Definition.bLoop && GetWorld()->GetTimeSeconds()
                - Playback.RequestedAtSec > Playback.Definition.StartDeadlineSec)
            {
                InOutEmitter.LastFailure = EObjectAudioFailure::Expired;
                InOutEmitter.LastFailedEvent = Playback.Definition.EventID;
            }
            else if (!InOutEmitter.bAdmitted || !bInAllowNewPosts)
            {
                continue;
            }
            else
            {
                const EObjectPlaybackStart Result = StartObjectPlayback(InOutEmitter, Playback, InTransform);
                if (Result == EObjectPlaybackStart::Aborted)
                {
                    return;
                }
                if (Result != EObjectPlaybackStart::Failed)
                {
                    continue;
                }
                InOutEmitter.LastFailedEvent = Playback.Definition.EventID;
            }
        }
        if (Playback.LoadHandle)
        {
            Playback.LoadHandle->CancelHandle();
        }
        UE_LOG(LogWandererAudio, Verbose, TEXT("Object request retired: %s id=%u loop=%d stopping=%d event=%s"),
            *Playback.Definition.EventID.ToString(), Playback.PlayingID,
            Playback.Definition.bLoop, Playback.bStopping, *GetPathNameSafe(Playback.LeaseWeakPtr.Get()));
        ReleaseEventResource(Playback.LeaseWeakPtr.Get());
        InOutEmitter.Playbacks.RemoveAt(Index);
        --ObjectPlaybackCount;
    }
    Emitter = InOutEmitter.EmitterWeakPtr.Get();
    if (Emitter && EmitterPool && !InOutEmitter.Playbacks.ContainsByPredicate(
        [](const FObjectAudioPlayback& Playback) { return Playback.PlayingID != 0; }))
    {
        InOutEmitter.EmitterWeakPtr.Reset();
        EmitterPool->BeginObjectRelease(*this, Emitter, InOutEmitter.Handle.InstanceID, {},
            InOutEmitter.ResourceGroup);
    }
}

UWandererAudioSubsystem::EObjectPlaybackStart UWandererAudioSubsystem::StartObjectPlayback(
    FObjectAudioEmitterState& InOutEmitter, FObjectAudioPlayback& InOutPlayback,
    const FTransform& InTransform)
{
    const auto Status = PrepareObjectPlayback(
        InOutPlayback, InOutEmitter.EffectivePriority, InOutEmitter.bEmergency);
    if (Status == EAudioEventResourceStatus::Pending)
    {
        return EObjectPlaybackStart::Waiting;
    }
    if (Status != EAudioEventResourceStatus::Ready || !EmitterPool)
    {
        InOutEmitter.LastFailure = EObjectAudioFailure::MediaUnavailable;
        return EObjectPlaybackStart::Failed;
    }
    if (EmitterPool->HasPendingObjectEmitter(InOutEmitter.Handle.InstanceID))
    {
        return EObjectPlaybackStart::Waiting;
    }
    UAkComponent* Emitter = InOutEmitter.EmitterWeakPtr.Get();
    if (!Emitter)
    {
        if (!InOutEmitter.bEmergency && !CanStartWorldPlayback())
        {
            return EObjectPlaybackStart::Waiting;
        }
        Emitter = EmitterPool->Acquire(*this, false, InOutEmitter.bEmergency
            ? EAudioEmitterLane::Emergency : EAudioEmitterLane::World);
        if (!Emitter)
        {
            return EObjectPlaybackStart::Waiting;
        }
        const uint64 Revision = InOutEmitter.PlaybackRevision;
        InOutEmitter.EmitterWeakPtr = Emitter;
        Emitter->SetWorldTransform(InTransform);
        if (!InOutEmitter.bRegistered || InOutEmitter.PlaybackRevision != Revision || !IsValid(Emitter))
        {
            return EObjectPlaybackStart::Aborted;
        }
        WandererAudioBackend::UpdateEmitterRoom(*Emitter, InTransform.GetLocation());
        ApplyEmitterReverb(*Emitter, true);
    }
    WandererAudioPlayback::SetWorldEmitterParameter(*Emitter, EWorldAudioEmitterParameter::Priority,
        FMath::Clamp(InOutEmitter.EffectivePriority, 0.0f, 99.0f), 0);
    InOutEmitter.AppliedPriority = FMath::Clamp(InOutEmitter.EffectivePriority, 0.0f, 99.0f);
    InOutPlayback.PlayingID = PostPreparedEvent(*InOutPlayback.LeaseWeakPtr.Get(), Emitter,
        InOutPlayback.Definition.bLoop ? InOutEmitter.FadeInSec : 0);
    if (!InOutPlayback.PlayingID)
    {
        InOutEmitter.LastFailure = EObjectAudioFailure::PostRejected;
        return EObjectPlaybackStart::Failed;
    }
    Emitter->SetComponentTickEnabled(true);
    InOutEmitter.LastFailure = EObjectAudioFailure::None;
    InOutEmitter.LastFailedEvent = NAME_None;
    UE_LOG(LogWandererAudio, Verbose, TEXT("Object request posted: %s id=%u eventID=%u active=%d"),
        *InOutPlayback.Definition.EventID.ToString(), InOutPlayback.PlayingID,
        InOutPlayback.LeaseWeakPtr->GetShortID(), IsObjectEventPlaying(InOutPlayback));
    return EObjectPlaybackStart::Posted;
}

EAudioEventResourceStatus UWandererAudioSubsystem::PrepareObjectPlayback(
    FObjectAudioPlayback& InOutPlayback, float InPriority, bool bInEmergency)
{
    const bool bRequiresFinite = bInEmergency || InOutPlayback.Definition.bEmergency;
    if (GetWorld()->GetTimeSeconds() < InOutPlayback.RetryAtSec)
    {
        return EAudioEventResourceStatus::Pending;
    }
    if (InOutPlayback.LeaseWeakPtr.IsValid())
    {
        if (bRequiresFinite && !WandererAudioBackend::IsFiniteEvent(*InOutPlayback.LeaseWeakPtr.Get()))
        {
            return EAudioEventResourceStatus::Failed;
        }
        return AdvanceEventResource(InOutPlayback.LeaseWeakPtr.Get(), InPriority);
    }
    if (!InOutPlayback.Definition.EventSoftPtr.Get() && !InOutPlayback.LoadHandle)
    {
        InOutPlayback.LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
            InOutPlayback.Definition.EventSoftPtr.ToSoftObjectPath(), FStreamableDelegate(),
            FMath::RoundToInt(InPriority));
        if (!InOutPlayback.LoadHandle)
        {
            return EAudioEventResourceStatus::Failed;
        }
    }
    if (InOutPlayback.LoadHandle && !InOutPlayback.LoadHandle->HasLoadCompleted())
    {
        return EAudioEventResourceStatus::Pending;
    }
    UAkAudioEvent* Event = InOutPlayback.Definition.EventSoftPtr.Get();
    if (!Event || Event->bAutoLoad
        || (bRequiresFinite && !WandererAudioBackend::IsFiniteEvent(*Event))
        || !AcquireEventResource(Event))
    {
        UE_LOG(LogWandererAudio, Warning,
            TEXT("Object media rejected: event=%s autoLoad=%d infinite=%d shortID=%u emergency=%d"),
            *GetPathNameSafe(Event), Event ? Event->bAutoLoad : false,
            Event ? Event->IsInfinite : false, Event ? Event->GetShortID() : 0, bInEmergency);
        return EAudioEventResourceStatus::Failed;
    }
    InOutPlayback.LeaseWeakPtr = Event;
    InOutPlayback.LoadHandle.Reset();
    return AdvanceEventResource(Event, InPriority);
}
