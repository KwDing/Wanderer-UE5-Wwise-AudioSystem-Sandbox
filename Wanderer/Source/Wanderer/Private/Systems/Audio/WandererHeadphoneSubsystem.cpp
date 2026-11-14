#include "Systems/Audio/WandererHeadphoneSubsystem.h"

#include "AkAudioDevice.h"
#include "Wwise/API/WwiseSoundEngineAPI.h"

DEFINE_LOG_CATEGORY_STATIC(LogWandererHeadphone, Log, All);

namespace
{
    constexpr const char* HeadphoneParameter = "UsingHeadphone";
    constexpr float HeadphoneEnabled = 1.0f;
    constexpr float HeadphoneDisabled = 0.0f;
    // Wwise's global RTPC is shared across PIE instances, not scoped to a UWorld.
    TWeakObjectPtr<UWandererHeadphoneSubsystem> OutputOwnerWeakPtr;

    bool ReadHeadphoneOutput(bool& OutEnabled)
    {
        IWwiseSoundEngineAPI* Engine = IWwiseSoundEngineAPI::Get();
        if (!Engine || !Engine->Query || !FAkAudioDevice::IsInitialized())
        {
            return false;
        }
        AkRtpcValue Value = HeadphoneDisabled;
        AK::SoundEngine::Query::RTPCValue_type Scope = AK::SoundEngine::Query::RTPCValue_Global;
        if (Engine->Query->GetRTPCValue(
                HeadphoneParameter, AK_INVALID_GAME_OBJECT, AK_INVALID_PLAYING_ID, Value, Scope)
            != AK_Success)
        {
            return false;
        }
        OutEnabled = Value >= HeadphoneEnabled;
        return true;
    }

    bool WriteHeadphoneOutput(bool bInEnabled)
    {
        IWwiseSoundEngineAPI* Engine = IWwiseSoundEngineAPI::Get();
        if (!Engine || !FAkAudioDevice::IsInitialized())
        {
            UE_LOG(LogWandererHeadphone,
                Warning,
                TEXT("Cannot change UsingHeadphone before Wwise is ready."));
            return false;
        }
        const AKRESULT Result = Engine->SetRTPCValue(
            HeadphoneParameter, bInEnabled ? HeadphoneEnabled : HeadphoneDisabled);
        if (Result != AK_Success)
        {
            UE_LOG(LogWandererHeadphone,
                Warning,
                TEXT("Setting UsingHeadphone failed (%d). Check the loaded Init bank."),
                Result);
            return false;
        }
        return true;
    }
} // namespace

void UWandererHeadphoneSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    ReadHeadphoneOutput(bUsingHeadphone);
}

void UWandererHeadphoneSubsystem::Deinitialize()
{
    check(IsInGameThread());
    if (bStopped)
    {
        return;
    }
    bStopped = true;
    if (OutputOwnerWeakPtr.Get() == this)
    {
        OutputOwnerWeakPtr.Reset();
        if (FAkAudioDevice::IsInitialized() && WriteHeadphoneOutput(false))
        {
            bUsingHeadphone = false;
        }
    }
    Super::Deinitialize();
}

bool UWandererHeadphoneSubsystem::IsUsingHeadphone() const
{
    check(IsInGameThread());
    bool bApplied = bUsingHeadphone;
    if (!bStopped)
    {
        ReadHeadphoneOutput(bApplied);
    }
    return bApplied;
}

bool UWandererHeadphoneSubsystem::TrySetUsingHeadphone(bool bInEnabled)
{
    check(IsInGameThread());
    if (bStopped || !WriteHeadphoneOutput(bInEnabled))
    {
        return false;
    }
    bUsingHeadphone = bInEnabled;
    OutputOwnerWeakPtr = this;
    return true;
}

void UWandererHeadphoneSubsystem::SetUsingHeadphone(bool bEnabled)
{
    TrySetUsingHeadphone(bEnabled);
}
