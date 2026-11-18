#pragma once

#include "CoreMinimal.h"
#include "AudioEmitterTypes.generated.h"

class USceneComponent;
class UWorld;

enum class EAudioEmitterLane : uint8
{
    World,
    Emergency
};

struct FAudioEmitterPoolStats
{
    int32 Capacity = 0;
    int32 WorldActive = 0;
    int32 WorldRetiring = 0;
    int32 EmergencyActive = 0;
};

struct FObjectAudioEmitterHandle
{
    FGuid InstanceID;
    uint64 Generation = 0;
    TWeakObjectPtr<UWorld> WorldWeakPtr;

    bool IsSet() const { return Generation != 0; }
};

struct FObjectAudioAttachment
{
    TWeakObjectPtr<USceneComponent> ComponentWeakPtr;
    FName Socket;
    FTransform RelativeTransform;
    bool bFallbackToComponent = false;
};

UENUM(BlueprintType)
enum class EObjectAudioRequestResult : uint8
{
    InvalidEmitter,
    UnknownEvent,
    AlreadyRequested,
    AtCapacity,
    LoadingMedia
};

UENUM(BlueprintType)
enum class EObjectAudioFailure : uint8
{
    None,
    MediaUnavailable,
    Expired,
    PostRejected,
    PlaybackEnded
};

USTRUCT(BlueprintType)
struct FObjectAudioEmitterStats
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    int32 ConfiguredEvents = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    int32 PendingInstances = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    int32 PlayingInstances = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    int32 PhysicalSlots = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    bool bAdmitted = false;

    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    bool bEmergency = false;

    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    float Priority = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    FName LastFailedEvent;

    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    EObjectAudioFailure LastFailure = EObjectAudioFailure::None;
};
