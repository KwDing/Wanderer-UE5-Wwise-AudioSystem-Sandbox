#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "WandererWorldAudioTypes.generated.h"

class UAkAudioEvent;

UENUM(BlueprintType)
enum class EWandererWorldAudioMode : uint8
{
    Point,
    Room,
    Global,
    Area,
    Spline
};

UENUM(BlueprintType)
enum class EWandererWorldAudioCategory : uint8
{
    Ambience,
    Detail,
    Vehicle,
    Object
};

/** Shared authoring data. Gain, spread and propagation stay in Wwise. */
USTRUCT(BlueprintType)
struct WANDERER_API FWandererWorldAudioProfileRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Playback")
    TSoftObjectPtr<UAkAudioEvent> PlayEvent;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Playback")
    TSoftObjectPtr<UAkAudioEvent> NightPlayEvent;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Playback")
    EWandererWorldAudioMode Mode = EWandererWorldAudioMode::Point;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Budget")
    EWandererWorldAudioCategory Category = EWandererWorldAudioCategory::Ambience;
    /** Authoring contract only: must match Wwise's effective audible tail. */
    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Distance",
        meta = (ClampMin = "0", Units = "m"))
    float AudibleDistanceM = 30;
    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Distance",
        meta = (ClampMin = "0", Units = "m"))
    float ActivationDistanceM = 35;
    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Distance",
        meta = (ClampMin = "0", Units = "m"))
    float ReleaseDistanceM = 42;
    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Budget",
        meta = (ClampMin = "0", ClampMax = "100"))
    float Priority = 60;
    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Budget",
        meta = (ClampMin = "0", ClampMax = "100"))
    float DistancePriorityPenalty = 40;
    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Protection",
        meta = (ClampMin = "0", Units = "m"))
    float ProtectNearM = 3;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Protection")
    bool bProtectInsideRoom = true;
    /** Soft admission cap; protected demand can exceed it and is reported. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Budget", meta = (ClampMin = "1"))
    int32 MaxInstances = 8;
    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Playback",
        meta = (ClampMin = "0", ClampMax = "2", Units = "s", DisplayName = "FadeOutSeconds"))
    float FadeOutSec = .15f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Playback",
        meta = (ClampMin = "0", ClampMax = "2", Units = "s"))
    float FadeInSec = .35f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Playback",
        meta = (ClampMin = "0", Units = "s"))
    float ReverbTailHoldSec = 0;
    /** Native radial spread radius for Area mode; unrelated to boundary gain. */
    UPROPERTY(
        EditAnywhere, BlueprintReadOnly, Category = "Area", meta = (ClampMin = "0.01", Units = "m"))
    float AreaSpreadRadiusM = 3;

    /** Inside distance needed to reach fully non-spatialized lake sound. */
    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Spline",
        meta = (ClampMin = "0.01", Units = "m"))
    float SplineBlendDistanceM = 2;
    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Spline",
        meta = (ClampMin = "0.05", ClampMax = "2", Units = "s"))
    float SplineBlendSeconds = .35f;

    FString Validate() const;
};
