#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "WandererPuddleMaskSettings.generated.h"

/** Offline-baked grayscale mask; no runtime GPU texture readback. */
USTRUCT()
struct FWandererPuddleMask
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere)
    FString MaterialPath;
    UPROPERTY(EditAnywhere)
    int32 Width = 64;
    UPROPERTY(EditAnywhere)
    int32 Height = 64;
    UPROPERTY(EditAnywhere)
    FString SampleHex;
    UPROPERTY(EditAnywhere, meta = (ClampMin = "0", ClampMax = "1"))
    float CoreThreshold = 0.55f;
};

UCLASS(Config = Game, DefaultConfig)
class WANDERER_API UWandererPuddleMaskSettings : public UObject
{
    GENERATED_BODY()
  public:
    UPROPERTY(Config, EditAnywhere, Category = "Surface|Puddle")
    TArray<FWandererPuddleMask> Masks;
};
