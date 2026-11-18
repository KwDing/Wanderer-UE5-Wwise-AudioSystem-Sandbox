#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WandererObjectAudioProfile.generated.h"

class UAkAudioEvent;

USTRUCT(BlueprintType)
struct FObjectAudioEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Audio")
    FName EventID;

    UPROPERTY(EditAnywhere, Category = "Audio")
    TSoftObjectPtr<UAkAudioEvent> EventSoftPtr;

    UPROPERTY(EditAnywhere, Category = "Audio")
    bool bLoop = false;

    /** Reserved for short, time-sensitive requests; loops cannot use the emergency lane. */
    UPROPERTY(EditAnywhere, Category = "Budget")
    bool bEmergency = false;

    UPROPERTY(EditAnywhere, Category = "Budget", meta = (ClampMin = "0.01", Units = "s"))
    float StartDeadlineSec = 0.5f;
};

UCLASS(BlueprintType)
class WANDERER_API UWandererObjectAudioProfile : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Resources")
    FName ResourceGroup;

    UPROPERTY(EditAnywhere, Category = "Budget", meta = (ClampMin = "0", ClampMax = "100"))
    float Priority = 60;

    UPROPERTY(EditAnywhere, Category = "Distance", meta = (ClampMin = "0.01", Units = "m"))
    float ActivationDistanceM = 35;

    UPROPERTY(EditAnywhere, Category = "Distance", meta = (ClampMin = "0", Units = "m"))
    float ReleaseDistanceM = 42;

    UPROPERTY(EditAnywhere, Category = "Playback", meta = (ClampMin = "0", Units = "s"))
    float FadeInSec = 0.35f;

    UPROPERTY(EditAnywhere, Category = "Playback", meta = (ClampMin = "0", Units = "s"))
    float FadeOutSec = 0.35f;

    UPROPERTY(EditAnywhere, Category = "Audio")
    TArray<FObjectAudioEvent> Events;

    UPROPERTY(EditAnywhere, Category = "Audio", meta = (ClampMin = "0"))
    int32 LoopLimit = 1;

    UPROPERTY(EditAnywhere, Category = "Audio", meta = (ClampMin = "0"))
    int32 ShortEventLimit = 2;

    UPROPERTY(EditAnywhere, Category = "Attachment")
    FName EquipmentSocket;

    UPROPERTY(EditAnywhere, Category = "Attachment")
    bool bFallbackToComponent = false;

    bool Validate() const;
};
