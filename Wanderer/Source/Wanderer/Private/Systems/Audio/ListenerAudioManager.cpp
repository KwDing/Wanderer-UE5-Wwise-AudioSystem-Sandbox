#include "Systems/Audio/ListenerAudioManager.h"

#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeExit.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/StrongObjectPtr.h"

#include "AkAudioEvent.h"

#include "Core/Diagnostics/DiagnosticRecorder.h"
#include "Interfaces/Audio/AudioOutputService.h"
#include "Systems/Audio/AudioEventLeaseManager.h"
#include "Systems/Audio/WwiseAudioBackend.h"

namespace
{
    constexpr double MixRetryIntervalSec = 0.5;
    constexpr double WaterStateTimeUnsetSec = -1.0;
    TWeakObjectPtr<UListenerAudioManager> UnderwaterMixOwner;
    TWeakObjectPtr<UListenerAudioManager> MenuMixOwner;
    TStrongObjectPtr<UAkAudioEvent> DeferredMenuResumeEvent;
    TStrongObjectPtr<UAudioEventLeaseManager> DeferredMenuResumeLeases;
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
        bWaterResetPending = !WandererAudioOutput::TrySetListenerUnderwater(false);
        return !bWaterResetPending;
    }

    bool TryPostMenuEvent(UAkAudioEvent* InEvent, UAudioEventLeaseManager* InLeases,
        const bool* InStopFlag = nullptr)
    {
        const TStrongObjectPtr<UAkAudioEvent> Event(InEvent);
        if (!Event.IsValid() || !WandererAudioOutput::IsReady()
            || !UAudioEventLeaseManager::IsReady(Event.Get()) || (InStopFlag && *InStopFlag))
        {
            return false;
        }
        const bool bKeepPlaying = InLeases && InLeases->Acquire(Event.Get());
        const uint32 PlayingID = UAudioEventLeaseManager::Post(*Event);
        if (bKeepPlaying)
        {
            InLeases->Retire(Event.Get(), PlayingID, -1, 0);
        }
        return PlayingID != 0;
    }

    bool RetryMenuRecovery()
    {
        if (bRecoveringMenu)
        {
            return false;
        }
        const TGuardValue<bool> Guard(bRecoveringMenu, true);
        if (!WandererAudioBackend::IsDeviceReady())
        {
            return !bResumePending && !bDuckResetPending;
        }
        if (bDuckResetPending)
        {
            bDuckResetPending = !WandererAudioOutput::TrySetMenuMix(EListenerMenuMode::Closed);
        }
        if (bResumePending && TryPostMenuEvent(DeferredMenuResumeEvent.Get(), DeferredMenuResumeLeases.Get()))
        {
            bResumePending = false;
            DeferredMenuResumeEvent.Reset();
            if (DeferredMenuResumeLeases)
            {
                DeferredMenuResumeLeases->Stop();
                DeferredMenuResumeLeases.Reset();
            }
        }
        return !bResumePending && !bDuckResetPending;
    }
}

void UListenerAudioManager::PrepareMenuEvents(const TSoftObjectPtr<UAkAudioEvent>& InPause,
    const TSoftObjectPtr<UAkAudioEvent>& InResume)
{
    if (bStopped || MenuEventLeases || InPause.IsNull() || InResume.IsNull())
    {
        return;
    }
    PauseMenuReference = InPause;
    ResumeMenuReference = InResume;
    // A recovery claim can outlive the controller and must not retain its outer chain.
    MenuEventLeases = NewObject<UAudioEventLeaseManager>();
    const TArray<FSoftObjectPath> Paths = {InPause.ToSoftObjectPath(), InResume.ToSoftObjectPath()};
    MenuLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths);
    MenuPreloadTicker = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UListenerAudioManager::AdvanceMenuPreload), .05f);
}

void UListenerAudioManager::SetMenuMode(EListenerMenuMode InMode)
{
    if (bStopped)
    {
        return;
    }
    RequestedMenuMode = InMode;
    bAwaitingMenuPair = InMode == EListenerMenuMode::Paused
        && (!UAudioEventLeaseManager::IsReady(PreparedPauseEvent)
            || !UAudioEventLeaseManager::IsReady(PreparedResumeEvent));
    if (!bAwaitingMenuPair)
    {
        SetMenuState(InMode, PreparedPauseEvent, PreparedResumeEvent);
    }
}

bool UListenerAudioManager::AdvanceMenuPreload(float InDeltaSec)
{
    if (bStopped || !MenuEventLeases)
    {
        return false;
    }
    if (MenuLoadHandle)
    {
        if (!MenuLoadHandle->HasLoadCompleted())
        {
            return true;
        }
        if (MenuEventLeases->Acquire(PauseMenuReference.Get()))
        {
            PreparedPauseEvent = PauseMenuReference.Get();
        }
        if (MenuEventLeases->Acquire(ResumeMenuReference.Get()))
        {
            PreparedResumeEvent = ResumeMenuReference.Get();
        }
        MenuLoadHandle.Reset();
    }
    const EAudioEventResourceStatus PauseStatus = MenuEventLeases->Advance(PreparedPauseEvent);
    const EAudioEventResourceStatus ResumeStatus = MenuEventLeases->Advance(PreparedResumeEvent);
    if (bAwaitingMenuPair && PauseStatus == EAudioEventResourceStatus::Ready
        && ResumeStatus == EAudioEventResourceStatus::Ready)
    {
        SetMenuMode(RequestedMenuMode);
    }
    RetryPendingMix();
    return !bStopped;
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
    if (bResumePending && !DeferredMenuResumeEvent && UAudioEventLeaseManager::IsReady(InResume))
    {
        DeferredMenuResumeEvent.Reset(InResume);
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
    const bool bNewClaim = !MenuMixOwner.IsValid();
    MenuMixOwner = this;
    const bool bMixApplied = WandererAudioOutput::TrySetMenuMix(InMode);
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
            TEXT("Cannot apply menu mix (%s). Check Wwise initialization and Windows Init bank."),
            bPauseMenu ? TEXT("Paused") : bMenuOpen ? TEXT("Focused") : TEXT("Closed"));
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
    FTSTicker::GetCoreTicker().RemoveTicker(MenuPreloadTicker);
    if (MenuLoadHandle)
    {
        MenuLoadHandle->CancelHandle();
        MenuLoadHandle.Reset();
    }
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
            DeferredMenuResumeEvent.Reset(ResumeEvent.Get());
            if (MenuEventLeases && PreparedPauseEvent)
            {
                MenuEventLeases->Release(PreparedPauseEvent);
            }
            DeferredMenuResumeLeases.Reset(MenuEventLeases);
            MenuEventLeases = nullptr;
            bMenuAudioPaused = false;
        }
        MenuMixOwner.Reset();
    }
    ResetWater(true);
    const bool bMenuRecovered = RetryMenuRecovery();
    if (MenuEventLeases)
    {
        MenuEventLeases->Stop();
        MenuEventLeases = nullptr;
    }
    PreparedPauseEvent = nullptr;
    PreparedResumeEvent = nullptr;
    ResumeEvent = nullptr;
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
    if (!WandererAudioBackend::IsDeviceReady())
    {
        bWaterStateApplied = false;
        return;
    }
    if (WandererAudioOutput::TrySetListenerUnderwater(bInUnderwater))
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
    if (TryPostMenuEvent(InEvent, MenuEventLeases, bInPaused ? &bStopped : nullptr))
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
