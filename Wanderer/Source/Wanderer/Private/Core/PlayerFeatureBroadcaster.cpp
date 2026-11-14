#include "Core/PlayerFeatureBroadcaster.h"

void FPlayerFeatureBroadcaster::BroadcastStarted(APlayerController& InController)
{
    check(IsInGameThread());
    GetStartedEvent().Broadcast(InController);
}

void FPlayerFeatureBroadcaster::BroadcastStopped(APlayerController& InController)
{
    check(IsInGameThread());
    GetStoppedEvent().Broadcast(InController);
}

FPlayerFeatureBroadcaster::FPlayerLifecycleEvent& FPlayerFeatureBroadcaster::GetStartedEvent()
{
    static FPlayerLifecycleEvent Event;
    return Event;
}

FPlayerFeatureBroadcaster::FPlayerLifecycleEvent& FPlayerFeatureBroadcaster::GetStoppedEvent()
{
    static FPlayerLifecycleEvent Event;
    return Event;
}
