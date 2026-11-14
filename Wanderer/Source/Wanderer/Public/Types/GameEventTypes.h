#pragma once

#include "CoreMinimal.h"

#include "Types/Audio/WandererAudioLogChannel.h"

#include "GameEventTypes.generated.h"

class UWorld;

UENUM(BlueprintType)
enum class EWandererGameEvent : uint8
{
    EnterScene,
    GroundSurface,
    SpaceInfo,
    GroundWetness,
    AudioDiagnostic
};

USTRUCT(BlueprintType)
struct WANDERER_API FWandererSpaceDirection
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Wanderer|Audio")
    FName Direction;
    UPROPERTY(BlueprintReadOnly, Category = "Wanderer|Audio")
    bool bHit = false;
    /** -1 means no blocking geometry within the requested range. */
    UPROPERTY(BlueprintReadOnly, Category = "Wanderer|Audio")
    float DistanceMeters = -1.0f;
    UPROPERTY(BlueprintReadOnly, Category = "Wanderer|Audio")
    FVector Position = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category = "Wanderer|Audio")
    FString ActorName;
};

USTRUCT(BlueprintType)
struct WANDERER_API FWandererGameEventMessage
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Wanderer|Events")
    EWandererGameEvent Type = EWandererGameEvent::EnterScene;
    UPROPERTY(BlueprintReadWrite, Category = "Wanderer|Events")
    FName Source;
    UPROPERTY(BlueprintReadWrite, Category = "Wanderer|Events")
    FName Scene;
    UPROPERTY(BlueprintReadWrite, Category = "Wanderer|Events")
    FName PreviousSurface;
    UPROPERTY(BlueprintReadWrite, Category = "Wanderer|Events")
    FName Surface;
    UPROPERTY(BlueprintReadWrite, Category = "Wanderer|Events")
    FName PreviousWetness;
    UPROPERTY(BlueprintReadWrite, Category = "Wanderer|Events")
    FName Wetness;
    UPROPERTY(BlueprintReadWrite, Category = "Wanderer|Events")
    FString Text;
    UPROPERTY(BlueprintReadOnly, Category = "Wanderer|Events")
    TArray<FWandererSpaceDirection> SpaceDirections;

    UPROPERTY(BlueprintReadOnly, Category = "Wanderer|Audio")
    EWandererAudioLogChannel AudioChannel = EWandererAudioLogChannel::Configuration;
    UPROPERTY(BlueprintReadOnly, Category = "Wanderer|Audio")
    FName AudioLevel;
};

// Reflected listeners receive a value; const-reference struct delegate parameters are
// treated as output parameters by UE 5.5's Python bridge.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FWandererGameEventReceived, FWandererGameEventMessage, Message);
DECLARE_MULTICAST_DELEGATE_TwoParams(
    FWandererGameEventObserved, UWorld*, const FWandererGameEventMessage&);
