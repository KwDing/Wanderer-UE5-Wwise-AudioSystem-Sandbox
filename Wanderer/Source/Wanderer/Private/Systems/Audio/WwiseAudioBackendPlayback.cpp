#include "Systems/Audio/WwiseAudioBackend.h"

#include "Async/Async.h"

#include "AkAudioDevice.h"
#include "AkAudioEvent.h"
#include "AkComponent.h"
#include "AkGameObject.h"
#include "AK/SoundEngine/Common/AkSpeakerConfig.h"
#include "Wwise/API/WwiseSoundEngineAPI.h"
#include "Wwise/Loaded/WwiseLoadedEvent.h"
#include "Wwise/WwiseExternalSourceManager.h"

#include "Interfaces/Audio/AudioOutputService.h"
#include "Interfaces/Audio/AudioPlaybackService.h"
#include "Types/ListenerAudioTypes.h"
#include "Types/RadioControlTypes.h"

namespace
{
    constexpr const char* RadioPlaybackSwitch = "RadioPlayback";
    constexpr const char* RadioGainParameter = "DeviceGain";
    constexpr const TCHAR* WorldEmitterParameters[] = {
        TEXT("WorldAudio_Priority"), TEXT("AmbientArea_Inside"), TEXT("AmbientSpline_Inside"),
        TEXT("BuildingAmbience_Mode"), TEXT("BuildingAmbience_Gain")};
    static_assert(UE_ARRAY_COUNT(WorldEmitterParameters)
        == static_cast<int32>(EWorldAudioEmitterParameter::BuildingGain) + 1);

    FAkAudioDevice* GetReadyWwiseDevice()
    {
        return WandererAudioBackend::IsInitialized() ? FAkAudioDevice::Get() : nullptr;
    }

    IWwiseSoundEngineAPI* GetReadyWwiseEngine()
    {
        return WandererAudioBackend::IsInitialized() ? IWwiseSoundEngineAPI::Get() : nullptr;
    }

    // 只传值回游戏线程，避免回调访问已销毁宿主
    void OnWwiseManagedEventEnded(AkCallbackType InType, AkEventCallbackInfo* InInfo, void*, void*)
    {
        if (InType != AK_EndOfEvent)
        {
            return;
        }
        // Queue value IDs so a fast callback cannot remove an ID before Post records it.
        AsyncTask(ENamedThreads::GameThread, [EventID = InInfo->eventID, PlayingID = InInfo->playingID]
        {
            if (FAkAudioDevice* Device = FAkAudioDevice::Get())
            {
                Device->RemovePlayingID(EventID, PlayingID);
            }
            if (auto External = IWwiseExternalSourceManager::Get())
            {
                External->OnEndOfEvent(PlayingID);
            }
        });
    }
}

bool WandererAudioBackend::IsInitialized()
{
    return FAkAudioDevice::IsInitialized();
}

bool WandererAudioBackend::IsDeviceReady()
{
    return GetReadyWwiseDevice() != nullptr;
}

bool WandererAudioBackend::IsSoundEngineReady()
{
    return GetReadyWwiseEngine() != nullptr;
}

TOptional<bool> WandererAudioBackend::QueryPlaybackActive(const UAkAudioEvent* InEvent, uint32 InPlayingID)
{
    if (FAkAudioDevice* Device = GetReadyWwiseDevice())
    {
        return InPlayingID && InEvent && Device->IsPlayingIDActive(InEvent->GetShortID(), InPlayingID);
    }
    return {};
}

int32 WandererAudioBackend::GetPlaybackPositionMs(uint32 InPlayingID)
{
    IWwiseSoundEngineAPI* Engine = GetReadyWwiseEngine();
    AkTimeMs PositionMs = 0;
    return InPlayingID && Engine
        && Engine->GetSourcePlayPosition(InPlayingID, &PositionMs, false) == AK_Success
        ? PositionMs : -1;
}

bool WandererAudioBackend::RestoreRadioPosition(
    const UAkAudioEvent& InEvent, const UAkComponent& InEmitter,
    uint32 InPlayingID, int32 InPositionMs)
{
    IWwiseSoundEngineAPI* Engine = GetReadyWwiseEngine();
    return Engine && InPlayingID && InPositionMs >= 0
        && Engine->SeekOnEvent(InEvent.GetShortID(), InEmitter.GetAkGameObjectID(),
            static_cast<AkTimeMs>(InPositionMs), false, InPlayingID) == AK_Success;
}

void WandererAudioBackend::StopPlayback(uint32 InPlayingID, int32 InFadeMs)
{
    if (FAkAudioDevice* Device = InPlayingID ? GetReadyWwiseDevice() : nullptr)
    {
        Device->StopPlayingID(InPlayingID, InFadeMs);
    }
}

void WandererAudioBackend::SetPlaybackPaused(
    uint32 InPlayingID, bool bInPaused, int32 InTransitionMs)
{
    IWwiseSoundEngineAPI* Engine = GetReadyWwiseEngine();
    if (!Engine || !InPlayingID)
    {
        return;
    }
    const auto Action = bInPaused ? AkActionOnEventType_Pause : AkActionOnEventType_Resume;
    Engine->ExecuteActionOnPlayingID(
        Action, InPlayingID, InTransitionMs, AkCurveInterpolation_Linear);
}

bool WandererAudioBackend::TryQueryPriorityRoom(
    const UWorld* InWorld, const FVector& InLocation, UAkRoomComponent*& OutRoom)
{
    OutRoom = nullptr;
    FAkAudioDevice* Device = InWorld ? FAkAudioDevice::Get() : nullptr;
    if (!Device)
    {
        return false;
    }
    const TArray<UAkRoomComponent*> Rooms = Device->FindRoomComponentsAtLocation(InLocation, InWorld);
    OutRoom = Rooms.IsEmpty() ? nullptr : Rooms[0];
    return true;
}

bool WandererAudioBackend::HasSpatialListener(const UWorld* InWorld)
{
    FAkAudioDevice* Device = InWorld ? GetReadyWwiseDevice() : nullptr;
    const UAkComponent* Listener = Device ? Device->GetSpatialAudioListener() : nullptr;
    return IsValid(Listener) && Listener->IsRegistered() && Listener->GetWorld() == InWorld;
}

void WandererAudioBackend::UpdateEmitterRoom(UAkComponent& InEmitter, const FVector& InLocation)
{
    InEmitter.UpdateSpatialAudioRoom(InLocation);
}

bool WandererAudioBackend::FadeInPlayback(uint32 InPlayingID, int32 InFadeMs)
{
    IWwiseSoundEngineAPI* Engine = GetReadyWwiseEngine();
    if (!Engine || !InPlayingID)
    {
        return false;
    }
    // Queue before the next audio render; Wwise owns the resume fade and all voice paths.
    Engine->ExecuteActionOnPlayingID(AkActionOnEventType_Pause,
        InPlayingID, 0, AkCurveInterpolation_Linear);
    Engine->ExecuteActionOnPlayingID(AkActionOnEventType_Resume,
        InPlayingID, InFadeMs, AkCurveInterpolation_Linear);
    return true;
}

const ANSICHAR* WandererAudioBackend::GetRadioSwitchName(ERadioContent InContent)
{
    switch (InContent)
    {
    case ERadioContent::Song:
        return "Song";
    case ERadioContent::Noise:
        return "Noise";
    case ERadioContent::Hiss:
        return "Hiss";
    default:
        return nullptr;
    }
}

int32 WandererAudioBackend::GetRadioChannelMask(ERadioChannel InChannel)
{
    switch (InChannel)
    {
    case ERadioChannel::Stereo:
        return AK_SPEAKER_SETUP_STEREO;
    case ERadioChannel::Left:
        return AK_SPEAKER_FRONT_LEFT;
    case ERadioChannel::Right:
        return AK_SPEAKER_FRONT_RIGHT;
    case ERadioChannel::Center:
        return AK_SPEAKER_FRONT_CENTER;
    default:
        return 0;
    }
}

bool WandererAudioBackend::ConfigureRadioEmitter(UAkComponent& InEmitter, ERadioContent InContent)
{
    IWwiseSoundEngineAPI* Engine = GetReadyWwiseEngine();
    const ANSICHAR* Value = GetRadioSwitchName(InContent);
    if (!Engine || !Value)
    {
        return false;
    }
    const AkGameObjectID ObjectID = InEmitter.GetAkGameObjectID();
    return Engine->SetRTPCValue(RadioGainParameter, 0, ObjectID, 0) == AK_Success
        && Engine->SetSwitch(RadioPlaybackSwitch, Value, ObjectID) == AK_Success;
}

bool WandererAudioBackend::SetRadioEmitterGain(
    const UAkComponent& InEmitter, float InGain, int32 InTransitionMs)
{
    IWwiseSoundEngineAPI* Engine = GetReadyWwiseEngine();
    return Engine && Engine->SetRTPCValue(
        RadioGainParameter, InGain, InEmitter.GetAkGameObjectID(), InTransitionMs) == AK_Success;
}

bool WandererAudioBackend::UpdateRadioEmitter(
    UAkComponent& InEmitter, const TArray<FTransform>& InPositions,
    TConstArrayView<ERadioChannel> InChannels, float InGain, int32 InTransitionMs)
{
    FAkAudioDevice* Device = GetReadyWwiseDevice();
    if (!Device || InPositions.IsEmpty() || InPositions.Num() != InChannels.Num())
    {
        return false;
    }
    UpdateEmitterRoom(InEmitter, InPositions[0].GetLocation());
    TArray<FAkChannelMask> Masks;
    Masks.Reserve(InChannels.Num());
    for (ERadioChannel Channel : InChannels)
    {
        Masks.AddDefaulted_GetRef().ChannelMask = GetRadioChannelMask(Channel);
    }
    return Device->SetMultiplePositions(
        &InEmitter, Masks, InPositions, EAkMultiPositionType::MultiDirections) == AK_Success
        && SetRadioEmitterGain(InEmitter, InGain, InTransitionMs);
}

bool WandererAudioBackend::ResetPooledEmitterParameters(UAkComponent& InEmitter, bool bInRadio)
{
    InEmitter.SetGameObjectRadius(0, 0);
    UpdateEmitterRoom(InEmitter, InEmitter.GetComponentLocation());
    FAkAudioDevice* Device = GetReadyWwiseDevice();
    IWwiseSoundEngineAPI* Engine = GetReadyWwiseEngine();
    if (!Device || !Engine)
    {
        return false;
    }
    const AkGameObjectID ObjectID = InEmitter.GetAkGameObjectID();
    if (Engine->SetGameObjectAuxSendValues(ObjectID, nullptr, 0) != AK_Success)
    {
        return false;
    }
    if (bInRadio)
    {
        // Restore the authored default Switch and replace any channel-masked positions.
        AkSoundPosition Position;
        Device->FVectorsToAKWorldTransform(FVector::ZeroVector, FVector::ForwardVector,
            FVector::UpVector, Position);
        if (Device->ResetRTPCValue(TEXT("DeviceGain"), ObjectID, 0) != AK_Success
            || Engine->SetSwitch(RadioPlaybackSwitch, GetRadioSwitchName(ERadioContent::Song), ObjectID) != AK_Success
            || Engine->SetPosition(ObjectID, Position) != AK_Success)
        {
            return false;
        }
    }
    for (const TCHAR* Parameter : WorldEmitterParameters)
    {
        const AKRESULT Result = Device->ResetRTPCValue(Parameter, ObjectID, 0);
        if (Result != AK_Success && Result != AK_IDNotFound)
        {
            return false;
        }
    }
    return true;
}

bool WandererAudioBackend::IsFiniteEvent(const UAkAudioEvent& InEvent)
{
    return InEvent.GetShortID() && !InEvent.IsInfinite;
}

uint32 WandererAudioBackend::PostIntegratedEvent(UAkAudioEvent& InEvent, UAkGameObject* InTarget)
{
    return InTarget
        ? InEvent.PostOnGameObject(InTarget, nullptr, nullptr, nullptr, AkCallbackType(0), nullptr)
        : InEvent.PostAmbient(nullptr, nullptr, nullptr, AkCallbackType(0), nullptr);
}

uint32 WandererAudioBackend::PostManagedEvent(const FWwiseLoadedEventInfo& InMedia, UAkGameObject* InTarget)
{
    FAkAudioDevice* Device = GetReadyWwiseDevice();
    IWwiseSoundEngineAPI* Engine = GetReadyWwiseEngine();
    auto External = IWwiseExternalSourceManager::Get();
    if (!Device || !Engine || !External)
    {
        return 0;
    }
    const FWwiseEventCookedData* Data = InMedia.LocalizedEventCookedData.EventLanguageMap.Find(InMedia.LanguageRef);
    TArray<FWwiseExternalSourceCookedData> Sources;
    if (Data)
    {
        Sources = Data->ExternalSources;
        for (const auto& AudioNode : Data->AudioNodes)
        {
            Sources.Append(AudioNode.Value.ExternalSources);
        }
    }
    TArray<AkExternalSourceInfo> SourceInfo;
    const TArray<uint32> SourceMedia = External->PrepareExternalSourceInfos(SourceInfo, MoveTemp(Sources));
    if (InTarget)
    {
        InTarget->UpdateObstructionAndOcclusion();
    }
    const uint32 EventID = InMedia.LocalizedEventCookedData.EventId;
    const uint32 PlayingID = Engine->PostEvent(EventID,
        InTarget ? InTarget->GetAkGameObjectID() : DUMMY_GAMEOBJ, AK_EndOfEvent,
        &OnWwiseManagedEventEnded, nullptr, SourceInfo.Num(), SourceInfo.GetData());
    External->BindPlayingIdToExternalSources(PlayingID, SourceMedia);
    if (PlayingID)
    {
        Device->AddPlayingID(EventID, PlayingID, EAkAudioContext::GameplayAudio);
        if (InTarget)
        {
            InTarget->EventPosted();
        }
    }
    return PlayingID;
}

bool WandererAudioPlayback::IsPlaybackActive(const UAkAudioEvent* InEvent, uint32 InPlayingID)
{
    return InEvent && InPlayingID && WandererAudioBackend::QueryPlaybackActive(InEvent, InPlayingID).Get(false);
}

void WandererAudioPlayback::SetWorldEmitterParameter(UAkGameObject& InTarget,
    EWorldAudioEmitterParameter InParameter, float InValue, int32 InTransitionMs)
{
    const uint8 Index = static_cast<uint8>(InParameter);
    check(Index < UE_ARRAY_COUNT(WorldEmitterParameters));
    InTarget.SetRTPCValue(nullptr, InValue, InTransitionMs, WorldEmitterParameters[Index]);
}

void WandererAudioPlayback::SetEmitterRadius(UAkComponent& InEmitter, float InOuterRadiusCm, float InInnerRadiusCm)
{
    InEmitter.SetGameObjectRadius(InOuterRadiusCm, InInnerRadiusCm);
}

bool WandererAudioOutput::TrySetMenuMix(EListenerMenuMode InMode)
{
    const TCHAR* State = TEXT("None");
    if (InMode == EListenerMenuMode::Paused)
    {
        State = TEXT("UI_Only");
    }
    else if (InMode == EListenerMenuMode::Focused)
    {
        State = TEXT("UI_Focus");
    }
    FAkAudioDevice* Device = GetReadyWwiseDevice();
    return Device && Device->SetState(TEXT("Audio_Duck"), State) == AK_Success;
}

bool WandererAudioOutput::TrySetListenerUnderwater(bool bInUnderwater)
{
    FAkAudioDevice* Device = GetReadyWwiseDevice();
    return Device && Device->SetState(
        TEXT("ListenerEnvironment"), bInUnderwater ? TEXT("Underwater") : TEXT("AboveWater")) == AK_Success;
}
