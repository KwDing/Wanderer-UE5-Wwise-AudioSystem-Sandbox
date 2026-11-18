#include "Systems/Audio/WandererHeadphoneSubsystem.h"

#include "Interfaces/Audio/AudioOutputService.h"

namespace
{
    // Wwise's global RTPC is shared across PIE instances.
    TWeakObjectPtr<UWandererHeadphoneSubsystem> OutputOwnerWeakPtr;
}

void UWandererHeadphoneSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    WandererAudioOutput::ReadHeadphoneOutput(bUsingHeadphone);
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
        if (WandererAudioOutput::IsReady() && WandererAudioOutput::WriteHeadphoneOutput(false))
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
        WandererAudioOutput::ReadHeadphoneOutput(bApplied);
    }
    return bApplied;
}

bool UWandererHeadphoneSubsystem::TrySetUsingHeadphone(bool bInEnabled)
{
    check(IsInGameThread());
    if (bStopped || !WandererAudioOutput::WriteHeadphoneOutput(bInEnabled))
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
