#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

/** Module-owned polling observer. Active callbacks pin the service, never the observed world. */
class FInteriorFitAudioSyncService : public TSharedFromThis<FInteriorFitAudioSyncService>
{
  public:
    ~FInteriorFitAudioSyncService();
    void Start();
    void Stop();
    static void Reconcile(UWorld* InWorld, TFunction<bool()> InCanContinue = {});
    static uint64 GetRevision();

  private:
    FTSTicker::FDelegateHandle Handle;
    TWeakObjectPtr<UWorld> LastWorld;
    FString LastSnapshot;
    bool bStopped = true;
    uint64 Generation = 0;

    bool Tick(float InDeltaSeconds);
};
