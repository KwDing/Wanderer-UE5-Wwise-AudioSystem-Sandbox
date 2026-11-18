#include "Systems/Audio/WandererAudioSubsystem.h"

#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

#include "AkAudioEvent.h"
#include "AkComponent.h"

#include "Systems/Audio/AudioEmitterPool.h"
#include "Systems/Audio/AudioEventLeaseManager.h"
#include "Systems/Audio/AudioSchedulingStateTypes.h"
#include "Systems/Audio/ObjectAudioStateTypes.h"
#include "Systems/Audio/WwiseAudioBackend.h"
#include "Interfaces/Audio/AudioPlaybackService.h"

void UWandererAudioSubsystem::CollectObjectCandidates(
    const FVector& InListener, TArray<FAudioSourceCandidate>& OutCandidates)
{
    TArray<FGuid> IDs;
    ObjectEmitters.GetKeys(IDs);
    for (const FGuid& ID : IDs)
    {
        const TSharedPtr<FObjectAudioEmitterState> State = ObjectEmitters.FindRef(ID);
        FTransform Transform;
        if (!State || !ResolveObjectEmitter(State->Handle, Transform))
        {
            continue;
        }
        const float DistanceM = FVector::Distance(InListener, Transform.GetLocation()) / 100.0f;
        const float LimitM = State->bAdmitted
            ? State->ReleaseDistanceM : State->ActivationDistanceM;
        if (State->Playbacks.IsEmpty() || DistanceM > LimitM
            || !IsResourceGroupActive(State->ResourceGroup))
        {
            SetObjectAdmission(ID, false, false);
            continue;
        }
        const bool bHasUrgent = State->Playbacks.ContainsByPredicate(
            [](const FObjectAudioPlayback& Playback)
            { return Playback.Definition.bEmergency && !Playback.bStopping; });
        // Existing regular playback shares its component; idle objects can borrow reserve for shorts.
        const bool bEmergency = State->EmitterWeakPtr.IsValid() ? State->bEmergency
            : bHasUrgent;
        State->EffectivePriority = FMath::Clamp(State->BasePriority
            - 40.0f * DistanceM / State->ActivationDistanceM, 0.0f, 99.0f)
            + (bHasUrgent ? AudioSchedulingDefaults::EmergencyPriority : 0.0f);
        FAudioSourceCandidate& Candidate = OutCandidates.AddDefaulted_GetRef();
        Candidate.ObjectInstanceID = ID;
        Candidate.bEmergency = bEmergency;
        Candidate.Info.OwnerPath = State->OwnerWeakPtr->GetPathName() + ID.ToString();
        Candidate.Info.ProfileKey = ID.ToString();
        Candidate.Info.ResourceGroup = State->ResourceGroup;
        Candidate.Info.Priority = State->EffectivePriority;
        Candidate.Info.bPlaying = State->EmitterWeakPtr.IsValid();
        Candidate.Info.bDetail = true;
        Candidate.Info.MaxInstances = 1;
    }
}

void UWandererAudioSubsystem::SetObjectAdmission(
    const FGuid& InInstanceID, bool bInAdmitted, bool bInEmergency)
{
    const TSharedPtr<FObjectAudioEmitterState> State = ObjectEmitters.FindRef(InInstanceID);
    if (!State)
    {
        return;
    }
    if ((State->bAdmitted || State->bPreparing) && !bInAdmitted)
    {
        YieldObjectEmitter(*State);
    }
    State->bAdmitted = bInAdmitted;
    State->bPreparing = false;
    State->bEmergency = bInEmergency;
    const float Priority = FMath::Clamp(State->EffectivePriority, 0.0f, 99.0f);
    if (bInAdmitted && State->EmitterWeakPtr.IsValid()
        && !FMath::IsNearlyEqual(State->AppliedPriority, Priority))
    {
        WandererAudioPlayback::SetWorldEmitterParameter(*State->EmitterWeakPtr.Get(),
            EWorldAudioEmitterParameter::Priority, Priority, 150);
        State->AppliedPriority = Priority;
    }
}

bool UWandererAudioSubsystem::PrepareObjectAdmission(const FGuid& InInstanceID, bool bInEmergency)
{
    const TSharedPtr<FObjectAudioEmitterState> State = ObjectEmitters.FindRef(InInstanceID);
    if (!State || State->Playbacks.IsEmpty())
    {
        return false;
    }
    State->bPreparing = true;
    State->bAdmitted = false;
    State->bEmergency = bInEmergency;
    bool bReady = true;
    for (int32 Index = State->Playbacks.Num() - 1; Index >= 0; --Index)
    {
        FObjectAudioPlayback& Playback = State->Playbacks[Index];
        if (bInEmergency && !Playback.Definition.bEmergency)
        {
            continue;
        }
        const bool bExpired = !Playback.Definition.bLoop && GetWorld()->GetTimeSeconds()
            - Playback.RequestedAtSec > Playback.Definition.StartDeadlineSec;
        const auto Status = bExpired ? EAudioEventResourceStatus::Failed
            : PrepareObjectPlayback(Playback, State->EffectivePriority, bInEmergency);
        bReady &= Status == EAudioEventResourceStatus::Ready;
        if (Status == EAudioEventResourceStatus::Failed)
        {
            State->LastFailure = bExpired
                ? EObjectAudioFailure::Expired : EObjectAudioFailure::MediaUnavailable;
            State->LastFailedEvent = Playback.Definition.EventID;
            ReleaseEventResource(Playback.LeaseWeakPtr.Get());
            if (Playback.LoadHandle)
            {
                Playback.LoadHandle->CancelHandle();
            }
            State->Playbacks.RemoveAt(Index);
            --ObjectPlaybackCount;
        }
    }
    return bReady && !State->Playbacks.IsEmpty();
}

void UWandererAudioSubsystem::YieldObjectEmitter(FObjectAudioEmitterState& InOutEmitter)
{
    TArray<FObjectEmitterPlayingEvent> PlayingEvents;
    for (int32 Index = InOutEmitter.Playbacks.Num() - 1; Index >= 0; --Index)
    {
        FObjectAudioPlayback& Playback = InOutEmitter.Playbacks[Index];
        if (Playback.LoadHandle)
        {
            Playback.LoadHandle->CancelHandle();
            Playback.LoadHandle.Reset();
        }
        UAkAudioEvent* Event = Playback.LeaseWeakPtr.Get();
        if (Event && Playback.PlayingID && EventLeaseManager)
        {
            PlayingEvents.Add({TStrongObjectPtr<UAkAudioEvent>(Event), Playback.PlayingID});
            WandererAudioBackend::StopPlayback(Playback.PlayingID,
                FMath::RoundToInt(InOutEmitter.FadeOutSec * 1000));
            EventLeaseManager->Retire(Event, Playback.PlayingID, -1, 0);
        }
        else
        {
            ReleaseEventResource(Event);
        }
        const bool bRemoveIntent = Playback.bStopping
            || (Playback.PlayingID && !Playback.Definition.bLoop);
        Playback.LeaseWeakPtr.Reset();
        Playback.PlayingID = 0;
        Playback.bStopping = false;
        Playback.RetryAtSec = 0;
        if (bRemoveIntent)
        {
            InOutEmitter.Playbacks.RemoveAt(Index);
            --ObjectPlaybackCount;
        }
    }
    if (EmitterPool && InOutEmitter.EmitterWeakPtr.IsValid())
    {
        EmitterPool->BeginObjectRelease(*this, InOutEmitter.EmitterWeakPtr.Get(),
            InOutEmitter.Handle.InstanceID, MoveTemp(PlayingEvents), InOutEmitter.ResourceGroup);
    }
    InOutEmitter.EmitterWeakPtr.Reset();
    InOutEmitter.AppliedPriority = -1;
    ++InOutEmitter.PlaybackRevision;
}

bool UWandererAudioSubsystem::IsObjectGroupPlaying(FName InGroup) const
{
    for (const auto& Pair : ObjectEmitters)
    {
        if (Pair.Value->ResourceGroup == InGroup
            && Pair.Value->Playbacks.ContainsByPredicate([](const FObjectAudioPlayback& Playback)
                { return Playback.PlayingID != 0; }))
        {
            return true;
        }
    }
    return false;
}

int32 UWandererAudioSubsystem::GetObjectActiveCount() const
{
    int32 Count = 0;
    for (const auto& Pair : ObjectEmitters)
    {
        Count += !Pair.Value->bEmergency && Pair.Value->EmitterWeakPtr.IsValid();
    }
    return Count;
}
