#pragma once

#include "CoreMinimal.h"

class APlayerController;

/** Neutral, game-thread lifecycle multicast. Registrants own and remove their handles. */
class WANDERER_API FPlayerFeatureBroadcaster
{
  public:
    DECLARE_MULTICAST_DELEGATE_OneParam(FPlayerLifecycleEvent, APlayerController&);

    static void BroadcastStarted(APlayerController& InController);
    static void BroadcastStopped(APlayerController& InController);
    static FPlayerLifecycleEvent& GetStartedEvent();
    static FPlayerLifecycleEvent& GetStoppedEvent();
};
