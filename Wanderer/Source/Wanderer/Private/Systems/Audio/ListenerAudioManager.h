#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "UObject/Object.h"

#include "Types/ListenerAudioTypes.h"

#include "ListenerAudioManager.generated.h"

class UAkAudioEvent;
class UAudioEventLeaseManager;
struct FStreamableHandle;

/**
 * Controller-owned audio session; owns global Wwise mix claims, never queries gameplay
 * entities.
 */
UCLASS()
class UListenerAudioManager : public UObject
{
    GENERATED_BODY()

public:
    void PrepareMenuEvents(const TSoftObjectPtr<UAkAudioEvent>& InPause,
        const TSoftObjectPtr<UAkAudioEvent>& InResume);
    void SetMenuMode(EListenerMenuMode InMode);
    void UpdateWater(const FListenerWaterSample& InSample);
    void ResetWater(bool bInReleaseOwnership);
    void RetryPendingMix();
    void SetMenuState(EListenerMenuMode InMode, UAkAudioEvent* InPause, UAkAudioEvent* InResume);
    void Stop(UAkAudioEvent* InResume);
    bool AreEarsUnderwater() const;

private:
    friend class FListenerAudioStopTest;
    friend class FListenerMixRecoveryTest;

    UPROPERTY(Transient)
    TObjectPtr<UAkAudioEvent> ResumeEvent;
    UPROPERTY(Transient)
    TObjectPtr<UAkAudioEvent> PreparedPauseEvent;
    UPROPERTY(Transient)
    TObjectPtr<UAkAudioEvent> PreparedResumeEvent;
    UPROPERTY(Transient)
    TObjectPtr<UAudioEventLeaseManager> MenuEventLeases;

    TSharedPtr<FStreamableHandle> MenuLoadHandle;
    TSoftObjectPtr<UAkAudioEvent> PauseMenuReference;
    TSoftObjectPtr<UAkAudioEvent> ResumeMenuReference;
    FTSTicker::FDelegateHandle MenuPreloadTicker;
    EListenerMenuMode RequestedMenuMode = EListenerMenuMode::Closed;
    bool bAwaitingMenuPair = false;
    EListenerMenuMode PendingMenuMode = EListenerMenuMode::Closed;
    bool bMenuResumePending = false;
    bool bStopped = false;
    bool bInDeviceCall = false;
    bool bStopPending = false;
    bool bResetWaterPending = false;
    bool bReleaseWaterPending = false;
    bool bEarsUnderwater = false;
    bool bWaterStateApplied = false;
    bool bPendingUnderwater = false;
    bool bMenuAudioPaused = false;
    double WaterStatePendingSinceSec = 0.0;
    double WaterStateAppliedAtSec = -1.0;
    double MixRetryNextAtSec = 0.0;

    void FinishDeviceCall();
    void ReleaseMixClaims();
    bool AdvanceMenuPreload(float InDeltaSec);
    void ApplyWater(bool bInUnderwater, double InSampleTimeSec);
    bool SetMenuPaused(bool bInPaused, UAkAudioEvent* InEvent);
    bool ShouldApplyWater(const FListenerWaterSample& InSample, bool bInSubmerged) const;
};
