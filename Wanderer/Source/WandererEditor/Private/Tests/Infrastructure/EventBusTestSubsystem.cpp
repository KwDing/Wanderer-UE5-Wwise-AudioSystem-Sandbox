#include "Tests/Infrastructure/EventBusTestSubsystem.h"

#include "Engine/World.h"

bool UEventBusTestSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    return false;
}

UWorld* UEventBusTestSubsystem::GetWorld() const
{
    return WorldWeakPtr.Get();
}

void UEventBusTestSubsystem::OnMessage(FWandererGameEventMessage Message)
{
    ++Received;
    if (bStopOnReceive)
    {
        Deinitialize();
    }
}
