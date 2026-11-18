#include "Components/WorldAudio/WandererAudioSourceComponent.h"

#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

#include "AkAudioEvent.h"
#include "AkComponent.h"
#include "AkRoomComponent.h"
#include "AkSpatialAudioVolume.h"

#include "Components/WorldAudio/WorldAudioDiagnosticUtils.h"
#include "Core/Diagnostics/DiagnosticRecorder.h"
#include "Core/Diagnostics/DiagnosticTraceScope.h"
#include "Data/Audio/WorldAudioPlaybackData.h"
#include "Interfaces/Audio/AudioPlaybackService.h"
#include "Systems/Audio/WandererAudioSubsystem.h"

FString UWandererAudioSourceComponent::GetActiveEventName() const
{
    return PlayingID && LoadedEvent ? LoadedEvent->GetName() : FString();
}

bool UWandererAudioSourceComponent::IsPlaying() const
{
    return WandererAudioPlayback::IsPlaybackActive(LoadedEvent, PlayingID);
}

bool UWandererAudioSourceComponent::TryGetPlaybackPosition(FVector& OutPosition) const
{
    OutPosition = FVector::ZeroVector;
    if (!OwnedEmitter || PostedObjectWeakPtr.Get() != OwnedEmitter.Get() || !IsPlaying())
    {
        return false;
    }
    OutPosition = OwnedEmitter->GetComponentLocation();
    return !OutPosition.ContainsNaN();
}

bool UWandererAudioSourceComponent::GetDebugPlaybackPosition(FVector& OutPosition) const
{
    return TryGetPlaybackPosition(OutPosition);
}

bool UWandererAudioSourceComponent::PrepareForAdmission(bool bInNight)
{
    const FWandererWorldAudioProfileRow* Row = GetProfile();
    if (!Row)
    {
        return false;
    }
    const TSoftObjectPtr<UAkAudioEvent>& Desired =
        bInNight && !Row->NightPlayEvent.IsNull() ? Row->NightPlayEvent : Row->PlayEvent;
    return IsPlaying() || PrepareEvent(Desired);
}

void UWandererAudioSourceComponent::UpdatePlayback(bool bInAdmitted, FName InReason, bool bInNight)
{
#if !UE_BUILD_SHIPPING
    FDiagnosticTraceScope Trace;
    ON_SCOPE_EXIT
    {
        WorldAudioDiagnosticUtils::ReportState(this, bInAdmitted, InReason, bInNight);
    };
#endif
    const FWandererWorldAudioProfileRow* Row = GetProfile();
    const UWorld* World = GetWorld();
    if (!World || !WandererAudioPlayback::IsReady(*World))
    {
        ShutdownPlayback();
        Status = TEXT("Audio unavailable");
        return;
    }
    if (PlayingID && !IsPlaying())
    {
        RequestStop(0);
    }
    if (!bInAdmitted || !Row)
    {
        RequestStop(
            Row ? FMath::RoundToInt(Row->FadeOutSec * WorldAudioPlayback::MsPerSec)
                : 0);
        if (!InReason.IsNone())
        {
            Status = InReason.ToString();
        }
        return;
    }
    const TSoftObjectPtr<UAkAudioEvent>& Desired =
        bInNight && !Row->NightPlayEvent.IsNull() ? Row->NightPlayEvent : Row->PlayEvent;
    if (TryStopForPlaybackChange(
            Desired,
            FMath::RoundToInt(Row->FadeOutSec * WorldAudioPlayback::MsPerSec)))
    {
        return;
    }
    const UWandererAudioSubsystem* System = World->GetSubsystem<UWandererAudioSubsystem>();
    if (System && System->HasPendingEmitterRelease(*this))
    {
        Status = TEXT("Finishing fade or reverb tail");
        return;
    }
    if (PlayingID)
    {
        UpdatePlayingEvent();
        return;
    }
    if (PrepareEvent(Desired))
    {
        StartPreparedEvent();
    }
}

void UWandererAudioSourceComponent::ShutdownPlayback()
{
    RequestStop(0);
    Status = TEXT("Stopped / unloaded");
}

void UWandererAudioSourceComponent::ReleaseEvent()
{
    if (bOwnsResourceLease && LoadedEvent)
    {
        UWorld* World = GetWorld();
        if (UWandererAudioSubsystem* System = World ? World->GetSubsystem<UWandererAudioSubsystem>() : nullptr)
        {
            System->ReleaseEventResource(LoadedEvent);
        }
    }
    bOwnsResourceLease = false;
    if (LoadHandle)
    {
        LoadHandle->CancelHandle();
        LoadHandle.Reset();
    }
    LoadedEvent = nullptr;
    RequestedEvent.Reset();
    PlayingID = 0;
    PostedObjectWeakPtr.Reset();
    AppliedAreaPresence = -1;
}

void UWandererAudioSourceComponent::RequestStop(int32 InFadeMs)
{
    UWorld* World = GetWorld();
    UWandererAudioSubsystem* System = World ? World->GetSubsystem<UWandererAudioSubsystem>() : nullptr;
    if (System && (PlayingID || OwnedEmitter))
    {
        System->ReleaseEmitter(*this, OwnedEmitter, LoadedEvent, PlayingID, InFadeMs,
            PostedTailSec, PostedResourceGroup, bOwnsResourceLease, bPreferPreviousEmitter);
        bOwnsResourceLease = false;
        OwnedEmitter = nullptr;
    }
    ReleaseEvent();
}

bool UWandererAudioSourceComponent::TryStopForPlaybackChange(
    const TSoftObjectPtr<UAkAudioEvent>& InDesired, int32 InFadeMs)
{
    if (PlayingID && PostedObjectWeakPtr.Get() != GetPlaybackObject())
    {
        RequestStop(InFadeMs);
        Status = TEXT("Changing playback object");
        return true;
    }
    if (!RequestedEvent.IsNull() && RequestedEvent != InDesired.ToSoftObjectPath())
    {
        RequestStop(InFadeMs);
        Status = TEXT("Changing event");
        return true;
    }
    return false;
}

void UWandererAudioSourceComponent::UpdatePlayingEvent()
{
    UpdateOwnedEmitter();
    if (UAkGameObject* Object = GetPlaybackObject())
    {
        WandererAudioPlayback::SetWorldEmitterParameter(*Object, EWorldAudioEmitterParameter::Priority,
            EffectivePriority, WorldAudioPlayback::PriorityInterpolationMs);
    }
    Status = bProtected ? TEXT("Posted / Protected") : TEXT("Posted (physical/virtual: Wwise)");
}

bool UWandererAudioSourceComponent::PrepareEvent(const TSoftObjectPtr<UAkAudioEvent>& InDesired)
{
    if (GetWorld()->GetTimeSeconds() < RetryAtSec)
    {
        return false;
    }
    if (!LoadHandle && !TryStartEventAssetLoad(InDesired))
    {
        return false;
    }
    const UWandererAudioSubsystem* Audio = GetWorld()->GetSubsystem<UWandererAudioSubsystem>();
    if ((!Audio || !Audio->IsEventDataResident(LoadedEvent))
        && GetWorld()->GetTimeSeconds() - LoadStartedAtSec > WorldAudioPlayback::MediaTimeoutSec)
    {
        ReleaseEvent();
        RetryAtSec = GetWorld()->GetTimeSeconds() + WorldAudioPlayback::TimeoutRetryDelaySec;
        Status = TEXT("Asset/media timeout: check Auto Load / generated banks (retry 10s)");
        WorldAudioDiagnosticUtils::ReportResourceFailure(this, InDesired.ToSoftObjectPath());
        return false;
    }
    if (!LoadHandle->HasLoadCompleted())
    {
        Status = TEXT("Loading asset");
        return false;
    }
    LoadedEvent = Cast<UAkAudioEvent>(LoadHandle->GetLoadedAsset());
    if (!LoadedEvent)
    {
        UE_LOG(LogWandererAudio,
               Warning,
               TEXT("WorldAudio %s: event asset load failed: %s"),
               *GetNameSafe(GetOwner()),
               *RequestedEvent.ToString());
        ReleaseEvent();
        RetryAtSec = GetWorld()->GetTimeSeconds() + WorldAudioPlayback::AssetRetryDelaySec;
        Status = TEXT("Event load failed (retry 5s)");
        WorldAudioDiagnosticUtils::ReportResourceFailure(this, InDesired.ToSoftObjectPath());
        return false;
    }
    if (!LoadedEvent->bAutoLoad && !TryAcquireEventLease(InDesired))
    {
        return false;
    }
    if (!Audio || !Audio->IsEventDataResident(LoadedEvent))
    {
        Status = TEXT("Loading Wwise media");
        return false;
    }
    return true;
}

bool UWandererAudioSourceComponent::TryStartEventAssetLoad(
    const TSoftObjectPtr<UAkAudioEvent>& InDesired)
{
    RequestedEvent = InDesired.ToSoftObjectPath();
    LoadStartedAtSec = GetWorld()->GetTimeSeconds();
    LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
        RequestedEvent, FStreamableDelegate(), FMath::RoundToInt(EffectivePriority));
    if (LoadHandle)
    {
        return true;
    }

    ReleaseEvent();
    RetryAtSec = GetWorld()->GetTimeSeconds() + WorldAudioPlayback::AssetRetryDelaySec;
    Status = TEXT("Event load request failed (retry 5s)");
    WorldAudioDiagnosticUtils::ReportResourceFailure(this, InDesired.ToSoftObjectPath());
    return false;
}

bool UWandererAudioSourceComponent::TryAcquireEventLease(
    const TSoftObjectPtr<UAkAudioEvent>& InDesired)
{
    UWandererAudioSubsystem* System = GetWorld()->GetSubsystem<UWandererAudioSubsystem>();
    if (!bOwnsResourceLease)
    {
        bOwnsResourceLease = System && System->AcquireEventResource(LoadedEvent);
    }
    const EAudioEventResourceStatus LoadStatus = bOwnsResourceLease && System
        ? System->AdvanceEventResource(LoadedEvent, EffectivePriority)
        : EAudioEventResourceStatus::Failed;
    if (LoadStatus == EAudioEventResourceStatus::Ready)
    {
        return true;
    }
    if (LoadStatus == EAudioEventResourceStatus::Pending)
    {
        Status = TEXT("Loading Wwise media");
        return false;
    }

    ReleaseEvent();
    Status = TEXT("Managed event resource load failed");
    WorldAudioDiagnosticUtils::ReportResourceFailure(this, InDesired.ToSoftObjectPath());
    RetryAtSec = GetWorld()->GetTimeSeconds() + WorldAudioPlayback::PlaybackRetryDelaySec;
    return false;
}

void UWandererAudioSourceComponent::StartPreparedEvent()
{
    const UWandererAudioSubsystem* AudioSystem = GetWorld()->GetSubsystem<UWandererAudioSubsystem>();
    if (AudioSystem && !AudioSystem->CanStartWorldPlayback())
    {
        Status = TEXT("Waiting for transition capacity");
        return;
    }
    UAkGameObject* Object = AcquirePlaybackObject();
    if (!Object || !LoadedEvent)
    {
        Status = TEXT("Playback target or emitter pool unavailable");
        return;
    }
    WandererAudioPlayback::SetWorldEmitterParameter(*Object, EWorldAudioEmitterParameter::Priority,
        EffectivePriority, 0);
    if (OwnedEmitter)
    {
        if (UWandererAudioSubsystem* System = GetWorld()->GetSubsystem<UWandererAudioSubsystem>())
        {
            System->ApplyEmitterReverb(*OwnedEmitter, OwnedEmitter->bUseReverbVolumes);
        }
    }
    PreparePlaybackObject(Object);
    WANDERER_DIAGNOSTIC(this,
                        EDiagnosticDomain::Audio,
                        TEXT("audio-admitted"),
                        false,
                        FString::Printf(TEXT("event=%s profile=%s target=%s priority=%.2f"),
                                        *GetPathNameSafe(LoadedEvent),
                                        *Profile.RowName.ToString(),
                                        *GetPathNameSafe(Object),
                                        EffectivePriority));
    UWandererAudioSubsystem* Audio = GetWorld()->GetSubsystem<UWandererAudioSubsystem>();
    PostedResourceGroup = ResourceGroup;
    const FWandererWorldAudioProfileRow* PlaybackRow = GetProfile();
    PlayingID = Audio ? Audio->PostPreparedEvent(
        *LoadedEvent, Object, PlaybackRow ? PlaybackRow->FadeInSec : 0) : 0;
    WANDERER_DIAGNOSTIC(this,
                        EDiagnosticDomain::Audio,
                        TEXT("wwise-post-result"),
                        !PlayingID,
                        FString::Printf(TEXT("event=%s target=%s positionCm=%s playingID=%u "
                                             "room=%s (SDK acceptance is not audibility)"),
                                        *GetPathNameSafe(LoadedEvent),
                                        *GetPathNameSafe(Object),
                                        *Object->GetComponentLocation().ToCompactString(),
                                        PlayingID,
                                        *AcousticRoom.ToSoftObjectPath().ToString()));
    if (!PlayingID)
    {
        Status = TEXT("Post failed (retry 2s)");
        RetryAtSec = GetWorld()->GetTimeSeconds() + WorldAudioPlayback::PlaybackRetryDelaySec;
        UE_LOG(LogWandererAudio,
               Warning,
               TEXT("WorldAudio %s: Post failed for %s"),
               *GetNameSafe(GetOwner()),
               *GetNameSafe(LoadedEvent));
        RequestStop(0);
    }
    else
    {
        const FWandererWorldAudioProfileRow* Row = GetProfile();
        PostedTailSec = Row ? Row->ReverbTailHoldSec : 0;
        if (UAkComponent* Emitter = Cast<UAkComponent>(Object))
        {
            Emitter->SetComponentTickEnabled(true);
        }
        PostedObjectWeakPtr = Object;
        PlaybackStarted(Object);
        Status = TEXT("Posted (physical/virtual: Wwise)");
    }
}
