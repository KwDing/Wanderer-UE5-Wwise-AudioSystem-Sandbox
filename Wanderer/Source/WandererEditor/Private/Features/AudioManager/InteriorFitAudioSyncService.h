#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

/** Module-owned editor observer. Observed worlds and actors are never kept alive by the service. */
class FInteriorFitAudioSyncService : public TSharedFromThis<FInteriorFitAudioSyncService>
{
  public:
    ~FInteriorFitAudioSyncService();
    void Start();
    void Stop();
    static void Reconcile(UWorld* InWorld, TFunction<bool()> InCanContinue = {});
    static uint64 GetRevision();

  private:
    friend class FIFAudioSyncRevisionTest;

    FTSTicker::FDelegateHandle Handle;
    TWeakObjectPtr<UWorld> LastWorld;
    TMap<FString, TArray<FString>> LastSample;
    TMap<FString, TArray<FString>> AppliedGroups;
    bool bStopped = true;
    bool bDirty = true;
    bool bNeedsConfirmation = true;
    bool bHasSample = false;
    bool bApplying = false;
    uint64 Generation = 0;
    double NextScanAtSec = 0;
    double RetryAtSec = 0;
#if WITH_DEV_AUTOMATION_TESTS
    int32 SampleCount = 0;
    int32 ReconcileCount = 0;
    int32 ReconciledGroupCount = 0;
#endif

    bool Tick(float InDeltaSeconds);
    void PollWorld(UWorld& InWorld, double InNowSec);
    void Sample(UWorld& InWorld, double InNowSec);
    void ResetWorldState();
    void MarkDirty();
    void OnActorChanged(AActor* InActor);
    void OnPropertyChanged(UObject* InObject, FPropertyChangedEvent& InEvent);
};
