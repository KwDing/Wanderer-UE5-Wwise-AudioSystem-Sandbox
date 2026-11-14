#include "Core/ModuleLifecycleBroadcaster.h"

void FModuleLifecycleBroadcaster::BroadcastStarted()
{
    check(IsInGameThread());
    GetStartedEvent().Broadcast();
}

void FModuleLifecycleBroadcaster::BroadcastStopped()
{
    check(IsInGameThread());
    GetStoppedEvent().Broadcast();
}

FModuleLifecycleBroadcaster::FModuleEvent& FModuleLifecycleBroadcaster::GetStartedEvent()
{
    static FModuleEvent Event;
    return Event;
}
FModuleLifecycleBroadcaster::FModuleEvent& FModuleLifecycleBroadcaster::GetStoppedEvent()
{
    static FModuleEvent Event;
    return Event;
}
