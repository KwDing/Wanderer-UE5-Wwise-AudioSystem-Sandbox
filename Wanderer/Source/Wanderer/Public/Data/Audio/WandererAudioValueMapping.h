#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WandererAudioValueMapping.generated.h"

class UAkSwitchValue;
class UAkStateValue;

// Separate asset classes make the Editor picker reject the wrong mapping type.
UCLASS(BlueprintType)
class WANDERER_API UWandererAudioNumberMapping : public UDataAsset
{
    GENERATED_BODY()
  public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
    TMap<FName, float> Values;
};

UCLASS(BlueprintType)
class WANDERER_API UWandererAudioSwitchMapping : public UDataAsset
{
    GENERATED_BODY()
  public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
    TMap<FName, TObjectPtr<UAkSwitchValue>> Values;
};

UCLASS(BlueprintType)
class WANDERER_API UWandererAudioStateMapping : public UDataAsset
{
    GENERATED_BODY()
  public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
    TMap<FName, TObjectPtr<UAkStateValue>> Values;
};
