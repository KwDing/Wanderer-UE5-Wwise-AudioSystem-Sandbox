#include "Systems/Audio/RadioPlaybackManager.h"

#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeExit.h"

#include "AkAudioEvent.h"
#include "AkComponent.h"

#include "Core/Diagnostics/DiagnosticRecorder.h"
#include "Core/Diagnostics/DiagnosticTraceScope.h"
#include "Data/Audio/WandererRadioAudioSettings.h"
#include "Systems/Audio/AudioEmitterPool.h"
#include "Systems/Audio/AudioEventLeaseManager.h"
#include "Systems/Audio/RadioChannelLayout.h"
#include "Systems/Audio/WwiseAudioBackend.h"

namespace RadioPlaybackDefaults
{
    constexpr int32 ContentGroupCountMax = 4;
    constexpr double MediaTimeoutSec = 15.0;
    constexpr int32 TransitionMs = 50;
    constexpr float EmitterUpdateIntervalSec = 0.05f;
} // namespace RadioPlaybackDefaults

DEFINE_LOG_CATEGORY_STATIC(LogWandererRadioPlayback, Log, All);

TOptional<ERadioContent> URadioPlaybackManager::ResolveRadioContent(FName InContentID)
{
    if (InContentID == TEXT("RECORDER_SONG_DEFAULT"))
    {
        return ERadioContent::Song;
    }
    if (InContentID == TEXT("RECORDER_NOISE"))
    {
        return ERadioContent::Noise;
    }
    if (InContentID == TEXT("RECORDER_TAPE_HISS"))
    {
        return ERadioContent::Hiss;
    }
    return {};
}

ERadioChannel URadioPlaybackManager::GetChannel(
    int32 InNumber, FName InContentID, const TArray<FRadioEmitterSnapshot>& InSources) const
{
    const FRadioSyncGroup* Group = FindSyncGroup(InContentID);
    return FRadioChannelLayout(InSources, InContentID, !Group || Group->bStereo)
        .GetChannel(InNumber - 1);
}

int32 URadioPlaybackManager::GetPosition(int32 InNumber, FName InContentID) const
{
    const FRadioSyncGroup* Group = FindSyncGroup(InContentID);
    if (InNumber <= 0 || InNumber > FRadioChannelLayout::SlotCount)
    {
        return -1;
    }
    const int32 PositionMs = Group
        ? WandererAudioBackend::GetPlaybackPositionMs(Group->PlayingID) : -1;
    const int32* Saved = SavedPositionsMs.Find(InContentID);
    return PositionMs >= 0 ? PositionMs : Saved ? *Saved : -1;
}

bool URadioPlaybackManager::HasContent(FName InContentID) const
{
    return FindSyncGroup(InContentID) != nullptr || SavedPositionsMs.Contains(InContentID);
}

ERadioPlaybackPhase URadioPlaybackManager::GetPlaybackPhase(FName InContentID) const
{
    if (PendingStarts.Contains(InContentID))
    {
        return MediaStatus == EAudioEventResourceStatus::Ready
            ? ERadioPlaybackPhase::WaitingForEmitters : ERadioPlaybackPhase::LoadingMedia;
    }
    if (FindSyncGroup(InContentID))
    {
        return bWaitingForListener ? ERadioPlaybackPhase::WaitingForListener
                                   : ERadioPlaybackPhase::Ready;
    }
    return ERadioPlaybackPhase::Stopped;
}

bool URadioPlaybackManager::Initialize(AActor* InOwner, UAudioEmitterPool& InPool)
{
    if (!IsValid(InOwner) || bShuttingDown)
    {
        return false;
    }
    bOperationActive = true;
    ON_SCOPE_EXIT
    {
        FinishOperation();
    };
    OwnerWeakPtr = InOwner;
    EmitterPoolWeakPtr = &InPool;
    SyncGroups.Reserve(RadioPlaybackDefaults::ContentGroupCountMax);
    RequestMedia();
    return !bShuttingDown;
}

void URadioPlaybackManager::Shutdown()
{
    if (bShuttingDown)
    {
        return;
    }
    bShuttingDown = true;
    // Scene updates can synchronously notify listeners while a slot reference is in use.
    if (!bOperationActive)
    {
        ReleaseResources();
    }
}

bool URadioPlaybackManager::Start(
    FName InContentID, const TArray<FRadioEmitterSnapshot>& InSources, FString& OutStatus)
{
    if (bShuttingDown || bOperationActive || !OwnerWeakPtr.IsValid())
    {
        return false;
    }
    bOperationActive = true;
    ON_SCOPE_EXIT
    {
        FinishOperation();
    };
    Sources = InSources;
    if (!ResolveRadioContent(InContentID).IsSet())
    {
        OutStatus = TEXT("Unknown radio content.");
        return false;
    }
    if (!bResourcesRequested || MediaStatus == EAudioEventResourceStatus::Failed)
    {
        RequestMedia();
    }
    if (MediaStatus == EAudioEventResourceStatus::Pending)
    {
        PendingStarts.Add(InContentID);
        OutStatus = TEXT("Loading radio audio.");
        return true;
    }
    const EGroupStart Result = FindSyncGroup(InContentID)
        ? EGroupStart::Started : CreateSyncGroup(InContentID);
    if (Result == EGroupStart::WaitingForEmitters)
    {
        PendingStarts.Add(InContentID);
    }
    OutStatus = Status;
    return Result != EGroupStart::Failed && !bShuttingDown;
}

bool URadioPlaybackManager::Update(
    const TArray<FRadioEmitterSnapshot>& InSources, float, TArray<FName>& OutFailed)
{
    if (bShuttingDown || bOperationActive || !OwnerWeakPtr.IsValid())
    {
        return false;
    }
    bOperationActive = true;
    ON_SCOPE_EXIT
    {
        FinishOperation();
    };
    Sources = InSources;
    if (Sources.IsEmpty())
    {
        ParkPlayback();
        return !bShuttingDown;
    }
    if (!bResourcesRequested && bWithinLoadRange)
    {
        RequestMedia();
    }
    if (bWithinLoadRange)
    {
        AdvanceMedia();
        UpdatePendingStarts(OutFailed);
    }
    else if (bHasSpatialSample && SyncGroups.IsEmpty())
    {
        ReleaseMedia();
        bResourcesRequested = false;
        MediaStatus = EAudioEventResourceStatus::Pending;
    }
    AActor* Owner = OwnerWeakPtr.Get();
    if (bShuttingDown || !Owner)
    {
        return false;
    }

    const bool bHasListener = WandererAudioBackend::HasSpatialListener(Owner->GetWorld());
    const bool bMuteForMissingListener = !bHasListener && !bWaitingForListener;
    bWaitingForListener = !bHasListener;
    for (int32 Index = SyncGroups.Num() - 1; Index >= 0; --Index)
    {
        FRadioSyncGroup& Group = SyncGroups[Index];
        const int32 PositionMs = WandererAudioBackend::GetPlaybackPositionMs(Group.PlayingID);
        if (PositionMs >= 0)
        {
            SavedPositionsMs.Add(Group.ContentID, PositionMs);
        }
        const bool bHasDevice = Sources.ContainsByPredicate(
            [ContentID = Group.ContentID](const FRadioEmitterSnapshot& Source)
            {
                return Source.ContentID == ContentID;
            });
        if (!bHasDevice)
        {
            DestroySyncGroup(Group);
            SyncGroups.RemoveAt(Index);
            if (bShuttingDown)
            {
                return false;
            }
            continue;
        }
        if (IsSyncGroupAlive(Group) && UpdateSyncGroup(Group, bHasListener))
        {
            if (bMuteForMissingListener)
            {
                MuteSyncGroup(Group);
            }
            continue;
        }
        OutFailed.Add(Group.ContentID);
        DestroySyncGroup(Group);
        SyncGroups.RemoveAt(Index);
        if (bShuttingDown)
        {
            return false;
        }
    }
    return OutFailed.IsEmpty();
}

void URadioPlaybackManager::RequestMedia()
{
    ReleaseMedia();
    bResourcesRequested = true;
    const auto* Settings = GetDefault<UWandererRadioAudioSettings>();
    RequestedPlayEvent = Settings->PlayEvent;
    RequestedStopEvent = Settings->StopEvent;
    const TArray<FSoftObjectPath> Paths{
        RequestedPlayEvent.ToSoftObjectPath(), RequestedStopEvent.ToSoftObjectPath()};
    if (!Paths[0].IsNull() && !Paths[1].IsNull())
    {
        MediaRequest = UAssetManager::GetStreamableManager().RequestAsyncLoad(
            Paths, FStreamableDelegate(), FMath::RoundToInt(LoadPriority));
    }
    MediaStatus = MediaRequest ? EAudioEventResourceStatus::Pending : EAudioEventResourceStatus::Failed;
    MediaDeadlineSec = 0;
    if (!MediaRequest)
    {
        Status = TEXT("Radio Event paths are unavailable.");
    }
}

void URadioPlaybackManager::AdvanceMedia()
{
    if (MediaStatus != EAudioEventResourceStatus::Pending)
    {
        return;
    }
    if (MediaDeadlineSec <= 0)
    {
        MediaDeadlineSec = FPlatformTime::Seconds() + RadioPlaybackDefaults::MediaTimeoutSec;
    }
    if (FPlatformTime::Seconds() >= MediaDeadlineSec)
    {
        Status = TEXT("Radio audio preload timed out.");
        MediaStatus = EAudioEventResourceStatus::Failed;
        ReleaseMedia();
        return;
    }
    if (MediaRequest)
    {
        if (!MediaRequest->HasLoadCompleted())
        {
            return;
        }
        EventLeases = NewObject<UAudioEventLeaseManager>(this);
        PlayRadioEvent = RequestedPlayEvent.Get();
        StopRadioEvent = RequestedStopEvent.Get();
        if (!EventLeases->Acquire(PlayRadioEvent) || !EventLeases->Acquire(StopRadioEvent))
        {
            Status = TEXT("Radio requires valid Play and Stop Events.");
            MediaStatus = EAudioEventResourceStatus::Failed;
            ReleaseMedia();
            return;
        }
        MediaRequest.Reset();
    }
    if (!WandererAudioBackend::IsInitialized())
    {
        return;
    }
    const EAudioEventResourceStatus PlayStatus = EventLeases->Advance(PlayRadioEvent, LoadPriority);
    const EAudioEventResourceStatus StopStatus = EventLeases->Advance(StopRadioEvent, LoadPriority);
    if (PlayStatus == EAudioEventResourceStatus::Failed || StopStatus == EAudioEventResourceStatus::Failed)
    {
        Status = TEXT("Radio media preload failed; check generated SoundBanks.");
        MediaStatus = EAudioEventResourceStatus::Failed;
        ReleaseMedia();
    }
    else if (PlayStatus == EAudioEventResourceStatus::Ready && StopStatus == EAudioEventResourceStatus::Ready)
    {
        MediaStatus = EAudioEventResourceStatus::Ready;
        Status.Reset();
    }
}

void URadioPlaybackManager::ReleaseMedia()
{
    if (MediaRequest)
    {
        MediaRequest->CancelHandle();
        MediaRequest.Reset();
    }
    if (EventLeases)
    {
        EventLeases->Stop();
        EventLeases = nullptr;
    }
    PlayRadioEvent = nullptr;
    StopRadioEvent = nullptr;
    RequestedPlayEvent.Reset();
    RequestedStopEvent.Reset();
}

void URadioPlaybackManager::ParkPlayback()
{
    for (FRadioSyncGroup& Group : SyncGroups)
    {
        const int32 PositionMs = WandererAudioBackend::GetPlaybackPositionMs(Group.PlayingID);
        if (PositionMs >= 0)
        {
            SavedPositionsMs.Add(Group.ContentID, PositionMs);
        }
        DestroySyncGroup(Group);
    }
    SyncGroups.Reset();
    PendingStarts.Reset();
    AdmittedContents.Reset();
    ReleaseMedia();
    bResourcesRequested = false;
    bWaitingForListener = false;
    bWithinLoadRange = false;
    bHasSpatialSample = false;
    MediaStatus = EAudioEventResourceStatus::Pending;
}

void URadioPlaybackManager::UpdatePendingStarts(TArray<FName>& OutFailed)
{
    for (auto It = PendingStarts.CreateIterator(); It && !bShuttingDown; ++It)
    {
        const FName ContentID = *It;
        if (!Sources.ContainsByPredicate([ContentID](const FRadioEmitterSnapshot& Source)
            {
                return Source.ContentID == ContentID && Source.bPlaying;
            }))
        {
            It.RemoveCurrent();
        }
        else if (MediaStatus != EAudioEventResourceStatus::Pending)
        {
            const EGroupStart Result = MediaStatus == EAudioEventResourceStatus::Failed
                ? EGroupStart::Failed
                : FindSyncGroup(ContentID) ? EGroupStart::Started : CreateSyncGroup(ContentID);
            if (Result != EGroupStart::WaitingForEmitters)
            {
                It.RemoveCurrent();
                if (Result == EGroupStart::Failed)
                {
                    OutFailed.Add(ContentID);
                }
            }
        }
    }
}

bool URadioPlaybackManager::IsSyncGroupAlive(const FRadioSyncGroup& InGroup) const
{
    return IsValid(InGroup.Emitter) && InGroup.Emitter->IsRegistered()
        && InGroup.Emitter->IsRegisteredWithWwise() && IsValid(PlayRadioEvent)
        && InGroup.PlayingID
        && WandererAudioBackend::QueryPlaybackActive(PlayRadioEvent, InGroup.PlayingID).Get(true);
}

void URadioPlaybackManager::MuteSyncGroup(const FRadioSyncGroup& InGroup) const
{
    if (UAkComponent* Emitter = InGroup.Emitter.Get(); IsValid(Emitter))
    {
        Emitter->SetComponentTickEnabled(false);
        WandererAudioBackend::SetRadioEmitterGain(*Emitter, 0, RadioPlaybackDefaults::TransitionMs);
    }
}

URadioPlaybackManager::EGroupStart URadioPlaybackManager::CreateSyncGroup(FName InContentID)
{
    if (!AdmittedContents.Contains(InContentID))
    {
        Status = TEXT("Waiting for world item audio admission.");
        return EGroupStart::WaitingForEmitters;
    }
    const TOptional<ERadioContent> Content = ResolveRadioContent(InContentID);
    if (!WandererAudioBackend::IsInitialized() || MediaStatus != EAudioEventResourceStatus::Ready
        || !UAudioEventLeaseManager::IsReady(PlayRadioEvent) || !UAudioEventLeaseManager::IsReady(StopRadioEvent))
    {
        Status = TEXT(
            "Radio audio unavailable; sync Play_Radio/Stop_Radio and generate Windows SoundBanks.");
        return EGroupStart::Failed;
    }
    if (!Content.IsSet())
    {
        Status = TEXT("Unknown debug radio content; choose Song, Noise or Hiss.");
        return EGroupStart::Failed;
    }
    // Retain paused clocks across retrieval, but never accumulate more than four content groups.
    for (int32 Index = SyncGroups.Num() - 1; Index >= 0; --Index)
    {
        if (SyncGroups.Num() < RadioPlaybackDefaults::ContentGroupCountMax)
        {
            break;
        }
        const FName Existing = SyncGroups[Index].ContentID;
        if (Sources.ContainsByPredicate(
                [Existing](const FRadioEmitterSnapshot& Source)
                {
                    return Source.ContentID == Existing;
                }))
        {
            continue;
        }
        DestroySyncGroup(SyncGroups[Index]);
        SyncGroups.RemoveAt(Index);
        if (bShuttingDown)
        {
            return EGroupStart::Failed;
        }
    }
    if (SyncGroups.Num() >= RadioPlaybackDefaults::ContentGroupCountMax)
    {
        Status = TEXT("All four content groups are assigned.");
        return EGroupStart::Failed;
    }
    UAudioEmitterPool* Pool = EmitterPoolWeakPtr.Get();
    if (!Pool)
    {
        Status = TEXT("The radio emitter pool is unavailable.");
        return EGroupStart::Failed;
    }
    UAkComponent* Emitter = Pool->Acquire(*this, false);
    if (!Emitter)
    {
        Status = TEXT("Waiting for a radio emitter.");
        return EGroupStart::WaitingForEmitters;
    }
    FRadioSyncGroup& Group = SyncGroups.AddDefaulted_GetRef();
    Group.ContentID = InContentID;
    Group.Emitter = Emitter;
    Group.bStereo = Content.GetValue() != ERadioContent::Hiss;
    if (!StartSyncGroupPlayback(Group, Content.GetValue()))
    {
        DestroySyncGroup(Group);
        SyncGroups.Pop();
        Status =
            TEXT("Radio Switch or Event rejected; check RadioPlayback and generated SoundBanks.");
        return EGroupStart::Failed;
    }
    Status.Reset();
    return EGroupStart::Started;
}

void URadioPlaybackManager::DestroySyncGroup(FRadioSyncGroup& InOutGroup, int32 InFadeMs)
{
    AdmittedContents.Remove(InOutGroup.ContentID);
    UAkComponent* Emitter = InOutGroup.Emitter.Get();
    const uint32 PlayID = InOutGroup.PlayingID;
    uint32 StopID = 0;
    if (PlayID && WandererAudioBackend::IsDeviceReady())
    {
        if (EventLeases && EventLeases->Acquire(PlayRadioEvent))
        {
            EventLeases->Retire(PlayRadioEvent, PlayID, -1, 0);
        }
        if (InFadeMs == 0 && IsValid(Emitter) && Emitter->IsRegisteredWithWwise() && EventLeases
            && EventLeases->Acquire(StopRadioEvent))
        {
            StopID = UAudioEventLeaseManager::Post(*StopRadioEvent, Emitter);
            EventLeases->Retire(StopRadioEvent, StopID, -1, 0);
        }
        if (!StopID)
        {
            WandererAudioBackend::StopPlayback(PlayID, InFadeMs);
        }
    }
    if (UAudioEmitterPool* Pool = EmitterPoolWeakPtr.Get())
    {
        Pool->BeginRadioRelease(*this, Emitter, PlayRadioEvent, PlayID, StopRadioEvent, StopID);
    }
    InOutGroup.Emitter = nullptr;
    InOutGroup.PlayingID = 0;
}

const FRadioSyncGroup* URadioPlaybackManager::FindSyncGroup(FName InContentID) const
{
    return SyncGroups.FindByPredicate(
        [InContentID](const FRadioSyncGroup& InGroup)
        {
            return InGroup.ContentID == InContentID;
        });
}

void URadioPlaybackManager::FinishOperation()
{
    bOperationActive = false;
    if (bShuttingDown)
    {
        ReleaseResources();
    }
    else
    {
        ApplyPendingAdmissions();
    }
}

void URadioPlaybackManager::ReleaseResources()
{
    for (FRadioSyncGroup& Group : SyncGroups)
    {
        DestroySyncGroup(Group);
    }
    SyncGroups.Reset();
    Sources.Reset();
    PendingStarts.Reset();
    SavedPositionsMs.Reset();
    ReleaseMedia();
    OwnerWeakPtr.Reset();
    EmitterPoolWeakPtr.Reset();
}

bool URadioPlaybackManager::StartSyncGroupPlayback(
    FRadioSyncGroup& InOutGroup, ERadioContent InContent)
{
    UAkComponent* Emitter = InOutGroup.Emitter.Get();
    if (bShuttingDown || !IsValid(Emitter) || !Emitter->IsRegisteredWithWwise())
    {
        return false;
    }
    Emitter->bUseReverbVolumes = true;
    Emitter->SetComponentTickInterval(RadioPlaybackDefaults::EmitterUpdateIntervalSec);
    if (!WandererAudioBackend::ConfigureRadioEmitter(*Emitter, InContent))
    {
        return false;
    }

    FDiagnosticTraceScope Trace;
    InOutGroup.PlayingID = UAudioEventLeaseManager::Post(*PlayRadioEvent, Emitter);
    WANDERER_DIAGNOSTIC(this,
        EDiagnosticDomain::Audio,
        TEXT("radio-post-result"),
        !InOutGroup.PlayingID,
        FString::Printf(TEXT("content=%s event=%s switch=RadioPlayback:%hs playingID=%u target=%s"),
            *InOutGroup.ContentID.ToString(),
            *GetPathNameSafe(PlayRadioEvent),
            WandererAudioBackend::GetRadioSwitchName(InContent),
            InOutGroup.PlayingID,
            *GetPathNameSafe(Emitter)));
    if (bShuttingDown || !InOutGroup.PlayingID)
    {
        return false;
    }
    const int32 PositionMs = SavedPositionsMs.FindRef(InOutGroup.ContentID);
    if (PositionMs > 0 && !WandererAudioBackend::RestoreRadioPosition(
            *PlayRadioEvent, *Emitter, InOutGroup.PlayingID, PositionMs))
    {
        return false;
    }
    SavedPositionsMs.FindOrAdd(InOutGroup.ContentID);
    return true;
}

bool URadioPlaybackManager::UpdateSyncGroup(FRadioSyncGroup& InOutGroup, bool bInHasListener)
{
    if (!WandererAudioBackend::IsSoundEngineReady())
    {
        return !bInHasListener;
    }
    const FRadioChannelLayout Layout(Sources, InOutGroup.ContentID, InOutGroup.bStereo);
    const bool bShouldPause = !Layout.HasPlaybackRequest();
    if (bShouldPause && !InOutGroup.bPaused)
    {
        // No audible positions remain. Keep the same paused PlayingID for the next placement.
        MuteSyncGroup(InOutGroup);
    }
    if (!bShouldPause && bInHasListener && !UpdateSyncPositions(*InOutGroup.Emitter, Layout))
    {
        return false;
    }
    if (InOutGroup.bPaused != bShouldPause)
    {
        WandererAudioBackend::SetPlaybackPaused(
            InOutGroup.PlayingID, bShouldPause, RadioPlaybackDefaults::TransitionMs);
        InOutGroup.bPaused = bShouldPause;
    }
    return true;
}

bool URadioPlaybackManager::UpdateSyncPositions(
    UAkComponent& InOutEmitter, const FRadioChannelLayout& InLayout)
{
    TArray<FTransform> Positions;
    TArray<ERadioChannel> Channels;
    Positions.Reserve(FRadioChannelLayout::SlotCount);
    Channels.Reserve(FRadioChannelLayout::SlotCount);
    for (int32 Slot = 0; Slot < FRadioChannelLayout::SlotCount; ++Slot)
    {
        const FRadioEmitterSnapshot* Member = InLayout.GetMember(Slot);
        if (Member && Member->bPlaying && !Member->Transform.ContainsNaN())
        {
            Positions.Add(Member->Transform);
            Channels.Add(InLayout.GetChannel(Slot));
        }
    }
    if (Positions.IsEmpty())
    {
        InOutEmitter.SetComponentTickEnabled(false);
        return WandererAudioBackend::SetRadioEmitterGain(
            InOutEmitter, 0, RadioPlaybackDefaults::TransitionMs);
    }

    InOutEmitter.SetWorldTransform(Positions[0]);
    if (bShuttingDown || !IsValid(&InOutEmitter))
    {
        return false;
    }
    if (!WandererAudioBackend::UpdateRadioEmitter(InOutEmitter, Positions, Channels,
            InLayout.GetGain(), RadioPlaybackDefaults::TransitionMs))
    {
        UE_LOG(LogWandererRadioPlayback, Warning, TEXT("Radio position or gain update failed."));
        return false;
    }
    InOutEmitter.SetComponentTickEnabled(true);
    return true;
}
