#include "Systems/Audio/RadioPlaybackManager.h"

#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"

#include "AkAudioDevice.h"
#include "AkAudioEvent.h"
#include "AkComponent.h"
#include "Wwise/API/WwiseSoundEngineAPI.h"

#include "Core/Diagnostics/DiagnosticRecorder.h"
#include "Core/Diagnostics/DiagnosticTraceScope.h"
#include "Systems/Audio/RadioChannelLayout.h"

namespace RadioPlaybackDefaults
{
    constexpr int32 ContentGroupCountMax = 4;
    constexpr int32 TransitionMs = 50;
    constexpr float EmitterUpdateIntervalSec = 0.05f;
} // namespace RadioPlaybackDefaults

DEFINE_LOG_CATEGORY_STATIC(LogWandererRecorder, Log, All);

const ANSICHAR* URadioPlaybackManager::ResolveRadioSwitch(FName InContentID)
{
    if (InContentID == TEXT("RECORDER_SONG_DEFAULT"))
    {
        return "Song";
    }
    if (InContentID == TEXT("RECORDER_NOISE"))
    {
        return "Noise";
    }
    if (InContentID == TEXT("RECORDER_TAPE_HISS"))
    {
        return "Hiss";
    }
    return nullptr;
}

int32 URadioPlaybackManager::GetChannelMask(
    int32 InNumber, FName InContentID, const TArray<FRadioEmitterSnapshot>& InSources) const
{
    const FRadioSyncGroup* Group = FindSyncGroup(InContentID);
    return FRadioChannelLayout(InSources, InContentID, !Group || Group->bStereo)
        .GetChannelMask(InNumber - 1);
}

int32 URadioPlaybackManager::GetPosition(int32 InNumber, FName InContentID) const
{
    const FRadioSyncGroup* Group = FindSyncGroup(InContentID);
    const int32 Slot = InNumber - 1;
    IWwiseSoundEngineAPI* Engine = IWwiseSoundEngineAPI::Get();
    AkTimeMs Position = 0;
    if (!Group || !Group->PlayingIDs.IsValidIndex(Slot) || !FAkAudioDevice::IsInitialized()
        || !Engine)
    {
        return -1;
    }
    return Engine->GetSourcePlayPosition(Group->PlayingIDs[Slot], &Position, false) == AK_Success
               ? Position
               : -1;
}

bool URadioPlaybackManager::HasContent(FName InContentID) const
{
    return FindSyncGroup(InContentID) != nullptr;
}

bool URadioPlaybackManager::Initialize(AActor* InOwner)
{
    bOperationActive = true;
    ON_SCOPE_EXIT
    {
        FinishOperation();
    };
    OwnerWeakPtr = InOwner;
    PlayRadioEvent = LoadObject<UAkAudioEvent>(
        nullptr, TEXT("/Game/WwiseAudio/Events/Objects/Play_Radio.Play_Radio"));
    if (bShuttingDown)
    {
        return false;
    }
    StopRadioEvent = LoadObject<UAkAudioEvent>(
        nullptr, TEXT("/Game/WwiseAudio/Events/Objects/Stop_Radio.Stop_Radio"));
    SyncGroups.Reserve(FRadioChannelLayout::SlotCount);
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
    const bool bStarted = FindSyncGroup(InContentID) || CreateSyncGroup(InContentID);
    OutStatus = Status;
    return bStarted && !bShuttingDown;
}

bool URadioPlaybackManager::Update(
    const TArray<FRadioEmitterSnapshot>& InSources, float InDeltaTime, TArray<FName>& OutFailed)
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
    FAkAudioDevice* Device = FAkAudioDevice::Get();
    UAkComponent* Listener = Device ? Device->GetSpatialAudioListener() : nullptr;
    const bool bAvailable = FAkAudioDevice::IsInitialized() && IsValid(Listener)
                            && Listener->GetWorld() == OwnerWeakPtr->GetWorld();
    for (int32 Index = SyncGroups.Num() - 1; Index >= 0; --Index)
    {
        if (bAvailable && UpdateSyncGroup(SyncGroups[Index], InDeltaTime))
        {
            continue;
        }
        OutFailed.Add(SyncGroups[Index].ContentID);
        DestroySyncGroup(SyncGroups[Index]);
        SyncGroups.RemoveAt(Index);
        if (bShuttingDown)
        {
            return false;
        }
    }
    return bAvailable && OutFailed.IsEmpty();
}

bool URadioPlaybackManager::CreateSyncGroup(FName InContentID)
{
    const ANSICHAR* SwitchValue = ResolveRadioSwitch(InContentID);
    if (!FAkAudioDevice::IsInitialized() || !IsValid(PlayRadioEvent) || !IsValid(StopRadioEvent))
    {
        Status = TEXT(
            "Radio audio unavailable; sync Play_Radio/Stop_Radio and generate Windows SoundBanks.");
        return false;
    }
    if (!SwitchValue)
    {
        Status = TEXT("Unknown debug radio content; choose Song, Noise or Hiss.");
        return false;
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
            return false;
        }
    }
    if (SyncGroups.Num() >= RadioPlaybackDefaults::ContentGroupCountMax)
    {
        Status = TEXT("All four content groups are assigned.");
        return false;
    }
    FRadioSyncGroup& Group = SyncGroups.AddDefaulted_GetRef();
    Group.ContentID = InContentID;
    Group.bStereo = InContentID != TEXT("RECORDER_TAPE_HISS");
    Group.PlayingIDs.SetNumZeroed(FRadioChannelLayout::SlotCount);
    if (!StartSyncGroupSlots(Group, SwitchValue))
    {
        DestroySyncGroup(Group);
        SyncGroups.Pop();
        Status =
            TEXT("Radio Switch or Event rejected; check RadioPlayback and generated SoundBanks.");
        return false;
    }
    return true;
}

void URadioPlaybackManager::DestroySyncGroup(FRadioSyncGroup& InOutGroup)
{
    StopSyncGroup(InOutGroup);
    for (UAkComponent* Emitter : InOutGroup.Emitters)
    {
        if (IsValid(Emitter))
        {
            Emitter->DestroyComponent();
        }
    }
    InOutGroup.Emitters.Reset();
    InOutGroup.PlayingIDs.Reset();
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
}

void URadioPlaybackManager::ReleaseResources()
{
    for (FRadioSyncGroup& Group : SyncGroups)
    {
        DestroySyncGroup(Group);
    }
    SyncGroups.Reset();
    Sources.Reset();
    PlayRadioEvent = nullptr;
    StopRadioEvent = nullptr;
    OwnerWeakPtr.Reset();
}

bool URadioPlaybackManager::StartSyncGroupSlots(
    FRadioSyncGroup& InOutGroup, const ANSICHAR* InSwitchValue)
{
    FDiagnosticTraceScope Trace;
    IWwiseSoundEngineAPI* Engine = IWwiseSoundEngineAPI::Get();
    if (!Engine)
    {
        return false;
    }
    for (int32 Slot = 0; Slot < FRadioChannelLayout::SlotCount; ++Slot)
    {
        UAkComponent* Emitter = NewObject<UAkComponent>(OwnerWeakPtr.Get());
        InOutGroup.Emitters.Add(Emitter);
        Emitter->bUseReverbVolumes = true;
        Emitter->OcclusionRefreshInterval = 0;
        Emitter->RegisterComponent();
        if (bShuttingDown || !IsValid(Emitter))
        {
            return false;
        }
        Emitter->SetComponentTickInterval(RadioPlaybackDefaults::EmitterUpdateIntervalSec);
        Emitter->SetRTPCValue(nullptr, 0, 0, TEXT("DeviceGain"));
        if (Engine->SetSwitch("RadioPlayback", InSwitchValue, Emitter->GetAkGameObjectID())
            != AK_Success)
        {
            return false;
        }
    }
    // No asynchronous callback. All content slots enter the same Wwise command batch.
    for (int32 Slot = 0; Slot < FRadioChannelLayout::SlotCount; ++Slot)
    {
        InOutGroup.PlayingIDs[Slot] = PlayRadioEvent->PostOnComponent(
            InOutGroup.Emitters[Slot], FOnAkPostEventCallback(), 0, true);
        WANDERER_DIAGNOSTIC(this,
            EDiagnosticDomain::Audio,
            TEXT("radio-post-result"),
            !InOutGroup.PlayingIDs[Slot],
            FString::Printf(TEXT("content=%s slot=%d event=%s switch=RadioPlayback:%hs "
                                 "playingID=%d target=%s"),
                *InOutGroup.ContentID.ToString(),
                Slot,
                *GetPathNameSafe(PlayRadioEvent),
                InSwitchValue,
                InOutGroup.PlayingIDs[Slot],
                *GetPathNameSafe(InOutGroup.Emitters[Slot])));
        if (bShuttingDown || !InOutGroup.PlayingIDs[Slot])
        {
            return false;
        }
    }
    return true;
}

void URadioPlaybackManager::StopSyncGroup(FRadioSyncGroup& InOutGroup)
{
    FAkAudioDevice* Device = FAkAudioDevice::IsInitialized() ? FAkAudioDevice::Get() : nullptr;
    if (!Device)
    {
        return;
    }
    for (int32 Slot = 0; Slot < InOutGroup.PlayingIDs.Num(); ++Slot)
    {
        const int32 ID = InOutGroup.PlayingIDs[Slot];
        if (!ID)
        {
            continue;
        }
        UAkComponent* Emitter =
            InOutGroup.Emitters.IsValidIndex(Slot) ? InOutGroup.Emitters[Slot].Get() : nullptr;
        if (!IsValid(Emitter) || !IsValid(StopRadioEvent)
            || !StopRadioEvent->PostOnComponent(Emitter, FOnAkPostEventCallback(), 0, true))
        {
            Device->StopPlayingID(ID, 0);
        }
    }
}

bool URadioPlaybackManager::UpdateDeviceEmitter(UAkComponent& InOutEmitter,
    const FRadioEmitterSnapshot& InSource,
    int32 InChannelMask,
    float InGain)
{
    FAkAudioDevice* Device = FAkAudioDevice::Get();
    IWwiseSoundEngineAPI* Engine = IWwiseSoundEngineAPI::Get();
    if (!Device || !Engine)
    {
        return false;
    }
    const FTransform Position = InSource.Transform;
    InOutEmitter.SetWorldTransform(Position);
    if (bShuttingDown || !IsValid(&InOutEmitter))
    {
        return false;
    }
    InOutEmitter.UpdateSpatialAudioRoom(Position.GetLocation());
    TArray<FTransform> Positions = {Position};
    FAkChannelMask Mask;
    Mask.ChannelMask = InChannelMask;
    TArray<FAkChannelMask> Masks = {Mask};
    return Device->SetMultiplePositions(
               &InOutEmitter, Masks, Positions, EAkMultiPositionType::SingleSource)
               == AK_Success
           && Engine->SetRTPCValue("DeviceGain",
                  InGain,
                  InOutEmitter.GetAkGameObjectID(),
                  RadioPlaybackDefaults::TransitionMs)
                  == AK_Success;
}

bool URadioPlaybackManager::UpdateSyncGroup(FRadioSyncGroup& InOutGroup, float InDeltaTime)
{
    FAkAudioDevice* Device = FAkAudioDevice::Get();
    IWwiseSoundEngineAPI* Engine = IWwiseSoundEngineAPI::Get();
    if (!Device || !Engine || InOutGroup.Emitters.Num() != FRadioChannelLayout::SlotCount
        || InOutGroup.PlayingIDs.Num() != FRadioChannelLayout::SlotCount)
    {
        return false;
    }
    const FRadioChannelLayout Layout(Sources, InOutGroup.ContentID, InOutGroup.bStereo);
    for (int32 Slot = 0; Slot < FRadioChannelLayout::SlotCount; ++Slot)
    {
        if (!UpdateSyncSlot(InOutGroup, Layout, Slot, InDeltaTime))
        {
            return false;
        }
    }
    const bool bAnyPlaying = Layout.HasPlaybackRequest();
    if (InOutGroup.bPaused == bAnyPlaying)
    {
        for (int32 ID : InOutGroup.PlayingIDs)
        {
            Engine->ExecuteActionOnPlayingID(
                bAnyPlaying ? AkActionOnEventType_Resume : AkActionOnEventType_Pause,
                ID,
                RadioPlaybackDefaults::TransitionMs,
                AkCurveInterpolation_Linear);
        }
        InOutGroup.bPaused = !bAnyPlaying;
    }
    return true;
}

bool URadioPlaybackManager::UpdateSyncSlot(FRadioSyncGroup& InOutGroup,
    const FRadioChannelLayout& InLayout,
    int32 InSlot,
    float InDeltaTime)
{
    FAkAudioDevice* Device = FAkAudioDevice::Get();
    IWwiseSoundEngineAPI* Engine = IWwiseSoundEngineAPI::Get();

    if (!IsValid(InOutGroup.Emitters[InSlot]) || !InOutGroup.PlayingIDs[InSlot]
        || !IsValid(PlayRadioEvent))
    {
        return false;
    }
    if (InDeltaTime > 0
        && !Device->IsPlayingIDActive(PlayRadioEvent->GetShortID(), InOutGroup.PlayingIDs[InSlot]))
    {
        UE_LOG(LogWandererRecorder,
            Warning,
            TEXT("Content %s slot %d PlayingID %d ended"),
            *InOutGroup.ContentID.ToString(),
            InSlot,
            InOutGroup.PlayingIDs[InSlot]);
        return false;
    }
    if (const FRadioEmitterSnapshot* Member = InLayout.GetMember(InSlot))
    {
        // Paused devices remain in the channel assignment; a partner does not change sides.
        if (!UpdateDeviceEmitter(*InOutGroup.Emitters[InSlot],
                *Member,
                InLayout.GetChannelMask(InSlot),
                Member->bPlaying ? InLayout.GetGain() : 0.0f))
        {
            UE_CLOG(!bShuttingDown,
                LogWandererRecorder,
                Warning,
                TEXT("Position/gain failed for slot %d"),
                InSlot);
            return false;
        }
    }
    else if (Engine->SetRTPCValue("DeviceGain",
                 0,
                 InOutGroup.Emitters[InSlot]->GetAkGameObjectID(),
                 RadioPlaybackDefaults::TransitionMs)
             != AK_Success)
    {
        return false;
    }
    return true;
}
