#include "Systems/Audio/WandererAudioSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

#include "Core/WandererEventBusSubsystem.h"
#include "Systems/Audio/AudioSpaceQueryProvider.h"

UWandererAudioSubsystem* UWandererAudioSubsystem::GetAudioSubsystem(
    const UObject* WorldContextObject)
{
    UWorld* World =
        GEngine && WorldContextObject
            ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
            : nullptr;
    return World ? World->GetSubsystem<UWandererAudioSubsystem>() : nullptr;
}

bool UWandererAudioSubsystem::CaptureSpaceInfo(APawn* Pawn, float MaximumDistanceMeters,
                                               TArray<FWandererSpaceDirection>& OutDirections)
{
    UWorld* World = GetWorld();
    if (!FAudioSpaceQueryProvider::Capture(World, Pawn, MaximumDistanceMeters, OutDirections))
    {
        return false;
    }
    const FVector Origin = Pawn->GetActorLocation();
    UGameInstance* Instance = World->GetGameInstance();
    UWandererEventBusSubsystem* Bus =
        Instance ? Instance->GetSubsystem<UWandererEventBusSubsystem>() : nullptr;
    if (!Bus)
    {
        return false;
    }
    FWandererGameEventMessage Message;
    Message.Type = EWandererGameEvent::SpaceInfo;
    Message.Source = TEXT("AudioEnvironment");
    Message.SpaceDirections = OutDirections;
    Message.Text = FString::Printf(TEXT("Pawn=%s Origin(cm)=%s Range=%.2fm"), *Pawn->GetName(),
                                   *Origin.ToCompactString(), MaximumDistanceMeters);
    Bus->Publish(Message);
    return true;
}
