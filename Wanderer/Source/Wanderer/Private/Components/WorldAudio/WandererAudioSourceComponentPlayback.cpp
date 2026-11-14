#include "Components/WorldAudio/WandererAudioSourceComponent.h"

#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

#include "AkAudioDevice.h"
#include "AkAudioEvent.h"
#include "AkComponent.h"
#include "AkRoomComponent.h"
#include "AkSpatialAudioVolume.h"

#include "Components/WorldAudio/WorldAudioDiagnosticUtils.h"
#include "Core/Diagnostics/DiagnosticRecorder.h"
#include "Core/Diagnostics/DiagnosticTraceScope.h"
#include "Data/Audio/WorldAudioPlaybackData.h"
#include "Systems/Audio/WandererAudioSubsystem.h"

FString UWandererAudioSourceComponent::GetActiveEventName() const
{
    return PlayingID && LoadedEvent ? LoadedEvent->GetName() : FString();
}

bool UWandererAudioSourceComponent::IsPlaying() const
{
    FAkAudioDevice* Device = FAkAudioDevice::Get();
    return PlayingID && LoadedEvent && FAkAudioDevice::IsInitialized() && Device
           && Device->IsPlayingIDActive(LoadedEvent->GetShortID(), PlayingID);
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
    if (!FAkAudioDevice::IsInitialized())
    {
        ShutdownPlayback();
        Status = TEXT("Audio unavailable");
        return;
    }
    if (PlayingID && !IsPlaying())
    {
        ReleaseEvent();
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
    if (bStopping)
    {
        Status = TEXT("Finishing fade");
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
    // Force-stop even if a previous fade is pending before releasing media.
    if (PlayingID && FAkAudioDevice::IsInitialized())
    {
        if (FAkAudioDevice* Device = FAkAudioDevice::Get())
        {
            Device->StopPlayingID(PlayingID, 0);
        }
    }
    ReleaseEvent();
    Status = TEXT("Stopped / unloaded");
}

void UWandererAudioSourceComponent::ReleaseEvent()
{
    if (bOwnsResourceLease && LoadedEvent)
    {
        if (UWandererAudioSubsystem* System = GetWorld()->GetSubsystem<UWandererAudioSubsystem>())
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
    bStopping = false;
}

void UWandererAudioSourceComponent::RequestStop(int32 InFadeMs)
{
    if (PlayingID && !bStopping && FAkAudioDevice::IsInitialized())
    {
        if (FAkAudioDevice* Device = FAkAudioDevice::Get())
        {
            WANDERER_DIAGNOSTIC(
                this,
                EDiagnosticDomain::Audio,
                TEXT("stop-request"),
                false,
                FString::Printf(TEXT("playingID=%u fadeMs=%d"), PlayingID, InFadeMs));
            Device->StopPlayingID(PlayingID, InFadeMs);
        }
        bStopping = InFadeMs > 0;
    }
    if (!bStopping || !IsPlaying())
    {
        ReleaseEvent();
    }
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
    if (UAkGameObject* Object = GetPlaybackObject())
    {
        Object->SetRTPCValue(nullptr,
                             EffectivePriority,
                             WorldAudioPlayback::PriorityInterpolationMs,
                             TEXT("WorldAudio_Priority"));
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
    if (GetWorld()->GetTimeSeconds() - LoadStartedAtSec > WorldAudioPlayback::MediaTimeoutSec)
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
    if (!LoadedEvent->bAutoLoad && !bOwnsResourceLease && !TryAcquireEventLease(InDesired))
    {
        return false;
    }
    if (!LoadedEvent->IsLoaded() || !LoadedEvent->IsDataFullyLoaded())
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
    LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(RequestedEvent);
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
    bOwnsResourceLease = System && System->AcquireEventResource(LoadedEvent);
    if (bOwnsResourceLease)
    {
        return true;
    }

    Status = TEXT("Managed event resource load failed");
    WorldAudioDiagnosticUtils::ReportResourceFailure(this, InDesired.ToSoftObjectPath());
    RetryAtSec = GetWorld()->GetTimeSeconds() + WorldAudioPlayback::PlaybackRetryDelaySec;
    return false;
}

void UWandererAudioSourceComponent::StartPreparedEvent()
{
    UAkGameObject* Object = GetPlaybackObject();
    if (!Object || !LoadedEvent)
    {
        Status = TEXT("Playback object unavailable");
        return;
    }
    Object->SetRTPCValue(nullptr, EffectivePriority, 0, TEXT("WorldAudio_Priority"));
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
    PlayingID = Object->PostAkEvent(LoadedEvent, 0, nullptr, nullptr);
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
    }
    else
    {
        PostedObjectWeakPtr = Object;
        PlaybackStarted(Object);
        Status = TEXT("Posted (physical/virtual: Wwise)");
    }
}
