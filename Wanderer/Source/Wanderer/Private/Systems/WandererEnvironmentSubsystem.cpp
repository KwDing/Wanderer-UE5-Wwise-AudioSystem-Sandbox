#include "Systems/WandererEnvironmentSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"

UWandererEnvironmentSubsystem* UWandererEnvironmentSubsystem::GetEnvironmentSubsystem(
    const UObject* WorldContextObject)
{
    if (!IsInGameThread())
    {
        return nullptr;
    }
    UWorld* World =
        GEngine && WorldContextObject
            ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
            : nullptr;
    return World ? World->GetSubsystem<UWandererEnvironmentSubsystem>() : nullptr;
}

void UWandererEnvironmentSubsystem::SetNight(bool bInNight)
{
    if (IsInGameThread() && !bStopped)
    {
        bNight = bInNight;
    }
}

bool UWandererEnvironmentSubsystem::IsNight() const
{
    return IsInGameThread() && !bStopped && bNight;
}

void UWandererEnvironmentSubsystem::Deinitialize()
{
    bStopped = true;
    bNight = false;
    Super::Deinitialize();
}

bool UWandererEnvironmentSubsystem::DoesSupportWorldType(EWorldType::Type InWorldType) const
{
    return InWorldType == EWorldType::Game || InWorldType == EWorldType::PIE;
}
