#pragma once

#include "CoreMinimal.h"
/** Process-wide synchronous notifications. Registrants remove their own handles. */
class WANDERER_API FModuleLifecycleBroadcaster
{
  public:
    DECLARE_MULTICAST_DELEGATE(FModuleEvent);
    static void BroadcastStarted();
    static void BroadcastStopped();
    static FModuleEvent& GetStartedEvent();
    static FModuleEvent& GetStoppedEvent();
};
