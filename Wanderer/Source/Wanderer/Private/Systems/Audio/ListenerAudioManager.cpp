#include "Systems/Audio/ListenerAudioManager.h"

#include "HAL/PlatformTime.h"
#include "Misc/ScopeExit.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/StrongObjectPtr.h"

#include "AkAudioDevice.h"
#include "AkAudioEvent.h"

#include "Core/Diagnostics/DiagnosticRecorder.h"

namespace
{
    constexpr double MixRetryIntervalSec = 0.5;
    constexpr double WaterStateTimeUnsetSec = -1.0;
    TWeakObjectPtr<UListenerAudioManager> UnderwaterMixOwner;
    TWeakObjectPtr<UListenerAudioManager> MenuMixOwner;
    TSoftObjectPtr<UAkAudioEvent> PendingResumeEventSoftPtr;
    bool bResumePending = false;
    bool bDuckResetPending = false;
    bool bWaterResetPending = false;
    bool bRecoveringMenu = false;

    bool RetryWaterRecovery()
    {
        if (!bWaterResetPending)
        {
            return true;
        }
        FAkAudioDevice* Audio = FAkAudioDevice::Get();
        if (Audio && FAkAudioDevice::IsInitialized())
        {
            bWaterResetPending = Audio->SetState(
                TEXT("ListenerEnvironment"), TEXT("AboveWater")) != AK_Success;
        }
        return !bWaterResetPending;
    }

    bool TryPostMenuEvent(UAkAudioEvent* InEvent, const bool* InStopFlag = nullptr)
    {
        const TStrongObjectPtr<UAkAudioEvent> Event(InEvent);
        if (!Event.IsValid() || !FAkAudioDevice::IsInitialized())
        {
            return false;
        }
        if (!Event->IsLoaded())
        {
            Event->LoadData();
        }
        return (!InStopFlag || !*InStopFlag) && Event->IsLoaded()
               && Event->PostAmbient(
                   nullptr, nullptr, nullptr, static_cast<AkCallbackType>(0), nullptr);
    }

    bool RetryMenuRecovery()
    {
        if (bRecoveringMenu)
        {
            return false;
        }
        const TGuardValue<bool> Guard(bRecoveringMenu, true);
        FAkAudioDevice* Audio = FAkAudioDevice::Get();
        if (!Audio || !FAkAudioDevice::IsInitialized())
        {
            return !bResumePending && !bDuckResetPending;
        }
        if (bDuckResetPending)
        {
            bDuckResetPending = Audio->SetState(TEXT("Audio_Duck"), TEXT("None")) != AK_Success;
        }
        if (bResumePending && TryPostMenuEvent(PendingResumeEventSoftPtr.LoadSynchronous()))
        {
            bResumePending = false;
            PendingResumeEventSoftPtr.Reset();
        }
        return !bResumePending && !bDuckResetPending;
    }
}

void UListenerAudioManager::UpdateWater(const FListenerWaterSample& InSample)
{
    const TStrongObjectPtr<UListenerAudioManager> KeepAlive(this);
    if (bStopped)
    {
        return;
    }
    const bool bSubmerged = InSample.bHasSurface
        && (bEarsUnderwater ? InSample.DepthCm > -InSample.HysteresisCm
                           : InSample.DepthCm > InSample.HysteresisCm);
    const double Now = InSample.SampleTimeSec;
    if (bPendingUnderwater != bSubmerged)
    {
        bPendingUnderwater = bSubmerged;
        WaterStatePendingSinceSec = Now;
    }
    // Spatial hysteresis rejects tiny waves; persistence rejects brief larger splashes.
    if (!bWaterStateApplied && !bSubmerged)
    {
        ApplyWater(false, Now);
        return;
    }
    if (ShouldApplyWater(InSample, bSubmerged))
    {
        ApplyWater(bSubmerged, Now);
    }
}

void UListenerAudioManager::ResetWater(bool bInReleaseOwnership)
{
    const TStrongObjectPtr<UListenerAudioManager> KeepAlive(this);
    if (bInDeviceCall)
    {
        bResetWaterPending = true;
        bReleaseWaterPending |= bInReleaseOwnership;
        return;
    }
    if (UnderwaterMixOwner.HasSameIndexAndSerialNumber(TWeakObjectPtr<UListenerAudioManager>(this)))
    {
        bWaterResetPending = true;
        if (bInReleaseOwnership)
        {
            UnderwaterMixOwner.Reset();
        }
    }
    bEarsUnderwater = false;
    bWaterStateApplied = false;
    bPendingUnderwater = false;
    WaterStatePendingSinceSec = 0;
    WaterStateAppliedAtSec = WaterStateTimeUnsetSec;
    RetryWaterRecovery();
}

void UListenerAudioManager::RetryPendingMix()
{
    if (bStopped || bInDeviceCall
        || (!bMenuResumePending && !bResumePending && !bDuckResetPending && !bWaterResetPending))
    {
        return;
    }
    const double Now = FPlatformTime::Seconds();
    if (Now < MixRetryNextAtSec)
    {
        return;
    }
    MixRetryNextAtSec = Now + MixRetryIntervalSec;
    const TStrongObjectPtr<UListenerAudioManager> KeepAlive(this);
    RetryWaterRecovery();
    if (bMenuResumePending)
    {
        SetMenuState(PendingMenuMode, nullptr, ResumeEvent.Get());
        return;
    }
    RetryMenuRecovery();
}

void UListenerAudioManager::SetMenuState(
    EListenerMenuMode InMode, UAkAudioEvent* InPause, UAkAudioEvent* InResume)
{
    if (bInDeviceCall || bStopped)
    {
        return;
    }
    const TStrongObjectPtr<UListenerAudioManager> KeepAlive(this);
    bInDeviceCall = true;
    ON_SCOPE_EXIT
    {
        FinishDeviceCall();
    };
    ResumeEvent = InResume;
    if (bResumePending && PendingResumeEventSoftPtr.IsNull() && IsValid(InResume))
    {
        PendingResumeEventSoftPtr = InResume;
    }
    if (!RetryMenuRecovery() || bStopped)
    {
        return;
    }
    const bool bPauseMenu = InMode == EListenerMenuMode::Paused;
    const bool bMenuOpen = bPauseMenu || InMode == EListenerMenuMode::Focused;
    if (bMenuOpen
        && (bStopped || (MenuMixOwner.IsValid() && MenuMixOwner.Get() != this)))
    {
        return;
    }
    // Identity comparison remains valid during the parent controller teardown.
    if (!bMenuOpen
        && !MenuMixOwner.HasSameIndexAndSerialNumber(
            TWeakObjectPtr<UListenerAudioManager>(this)))
    {
        return;
    }
    const TCHAR* State = bPauseMenu ? TEXT("UI_Only") : bMenuOpen ? TEXT("UI_Focus") : TEXT("None");
    const bool bNewClaim = !MenuMixOwner.IsValid();
    MenuMixOwner = this;
    FAkAudioDevice* Audio = FAkAudioDevice::Get();
    const bool bMixApplied = Audio && FAkAudioDevice::IsInitialized()
                            && Audio->SetState(TEXT("Audio_Duck"), State) == AK_Success;
    const bool bWasPending = bMenuResumePending;
    bMenuResumePending = false;
    if (bMixApplied)
    {
        // Native paired bus actions preserve playback cursors and existing pause counts.
        const bool bPauseChanged = SetMenuPaused(bPauseMenu, bPauseMenu ? InPause : InResume);
        bMenuResumePending = !bPauseMenu && !bPauseChanged;
    }
    else
    {
        if (bNewClaim)
        {
            MenuMixOwner.Reset();
        }
        bMenuResumePending = !bPauseMenu && !bNewClaim;
        UE_CLOG(!bWasPending,
            LogWandererAudio,
            Warning,
            TEXT("Cannot apply Audio_Duck/%s. Check Wwise initialization and Windows Init bank."),
            State);
    }
    if (bMenuResumePending)
    {
        PendingMenuMode = InMode;
        UE_CLOG(!bWasPending,
            LogWandererAudio,
            Warning,
            TEXT("Menu SFX recovery pending. Check the generated UI SoundBank."));
    }
    else if (bPauseMenu && bMixApplied && !bMenuAudioPaused)
    {
        UE_LOG(LogWandererAudio,
            Warning,
            TEXT("Cannot pause menu SFX; check the Event asset and generated UI SoundBank."));
    }
    if (bMixApplied && !bMenuOpen && !bMenuAudioPaused)
    {
        MenuMixOwner.Reset();
    }
}

void UListenerAudioManager::Stop(UAkAudioEvent* InResume)
{
    const TStrongObjectPtr<UListenerAudioManager> KeepAlive(this);
    bStopped = true;
    bStopPending = true;
    bMenuResumePending = false;
    if (InResume)
    {
        ResumeEvent = InResume;
    }
    if (!bInDeviceCall)
    {
        ReleaseMixClaims();
    }
}

bool UListenerAudioManager::AreEarsUnderwater() const
{
    return bEarsUnderwater;
}

void UListenerAudioManager::FinishDeviceCall()
{
    bInDeviceCall = false;
    if (bStopPending)
    {
        ReleaseMixClaims();
        return;
    }
    if (bResetWaterPending)
    {
        const bool bRelease = bReleaseWaterPending;
        bResetWaterPending = false;
        bReleaseWaterPending = false;
        ResetWater(bRelease);
    }
}

void UListenerAudioManager::ReleaseMixClaims()
{
    bStopPending = false;
    bMenuResumePending = false;
    if (MenuMixOwner.HasSameIndexAndSerialNumber(
            TWeakObjectPtr<UListenerAudioManager>(this)))
    {
        bDuckResetPending = true;
        if (bMenuAudioPaused)
        {
            // Debt survives the controller. A future request must resume before claiming the mix.
            bResumePending = true;
            PendingResumeEventSoftPtr = ResumeEvent.Get();
            bMenuAudioPaused = false;
        }
        MenuMixOwner.Reset();
    }
    ResetWater(true);
    const bool bMenuRecovered = RetryMenuRecovery();
    UE_CLOG(!bMenuRecovered || bWaterResetPending,
        LogWandererAudio,
        Warning,
        TEXT("Mix cleanup pending; the next listener request will retry "
             "before taking ownership."));
}

void UListenerAudioManager::ApplyWater(bool bInUnderwater, double InSampleTimeSec)
{
    if (bStopped || bInDeviceCall)
    {
        return;
    }
    const TStrongObjectPtr<UListenerAudioManager> KeepAlive(this);
    bInDeviceCall = true;
    ON_SCOPE_EXIT
    {
        FinishDeviceCall();
    };
    if (bWaterResetPending || bStopped)
    {
        return;
    }
    if (UnderwaterMixOwner.IsValid() && UnderwaterMixOwner.Get() != this)
    {
        return;
    }
    if (!UnderwaterMixOwner.IsValid())
    {
        UnderwaterMixOwner = this;
        bWaterStateApplied = false;
    }
    if (bWaterStateApplied && bEarsUnderwater == bInUnderwater)
    {
        return;
    }
    FAkAudioDevice* Audio = FAkAudioDevice::Get();
    if (!Audio || !FAkAudioDevice::IsInitialized())
    {
        bWaterStateApplied = false;
        return;
    }
    if (Audio->SetState(
            TEXT("ListenerEnvironment"), bInUnderwater ? TEXT("Underwater") : TEXT("AboveWater"))
        == AK_Success)
    {
        bEarsUnderwater = bInUnderwater;
        bWaterStateApplied = true;
        WaterStateAppliedAtSec = InSampleTimeSec;
    }
}

bool UListenerAudioManager::SetMenuPaused(bool bInPaused, UAkAudioEvent* InEvent)
{
    if (bMenuAudioPaused == bInPaused)
    {
        return true;
    }
    if (bStopped && bInPaused)
    {
        return false;
    }
    if (TryPostMenuEvent(InEvent, bInPaused ? &bStopped : nullptr))
    {
        bMenuAudioPaused = bInPaused;
        return true;
    }
    return false;
}

bool UListenerAudioManager::ShouldApplyWater(
    const FListenerWaterSample& InSample, bool bInSubmerged) const
{
    const double Now = InSample.SampleTimeSec;
    return !bStopped && bInSubmerged != bEarsUnderwater
           && Now - WaterStatePendingSinceSec >= InSample.HoldSec
           && (WaterStateAppliedAtSec == WaterStateTimeUnsetSec
               || Now - WaterStateAppliedAtSec >= InSample.TransitionIntervalMinSec);
}
