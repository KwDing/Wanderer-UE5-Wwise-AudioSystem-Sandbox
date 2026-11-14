#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "Types/RadioControlTypes.h"

#include "WandererSpeakerSettings.generated.h"

class UStaticMesh;

/** Editable silent catalog and placement limits. Audio backends consume option IDs, not UI labels.
 */
UCLASS(Config = Game, DefaultConfig)
class WANDERER_API UWandererSpeakerSettings : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(Config, EditAnywhere, Category = "Speaker")
    TArray<FWandererSpeakerOption> Options;
    UPROPERTY(Config, EditAnywhere, Category = "Speaker")
    TSoftObjectPtr<UStaticMesh> RadioMesh;
    UPROPERTY(Config, EditAnywhere, Category = "Speaker")
    TSoftObjectPtr<UStaticMesh> MountedMesh;
    UPROPERTY(Config, EditAnywhere, Category = "Speaker", meta = (ClampMin = "1", ClampMax = "4"))
    int32 MaxSources = 4;
    UPROPERTY(Config, EditAnywhere, Category = "Speaker", meta = (ClampMin = "0", ClampMax = "2"))
    int32 RadioInventory = 2;
    UPROPERTY(Config, EditAnywhere, Category = "Speaker", meta = (ClampMin = "0", ClampMax = "2"))
    int32 MountedInventory = 2;
    UPROPERTY(Config, EditAnywhere, Category = "Speaker", meta = (ClampMin = "50"))
    float MaxHorizontalDistance = 600;
    UPROPERTY(Config, EditAnywhere, Category = "Speaker", meta = (ClampMin = "50"))
    float MaxVerticalDistance = 350;
    UPROPERTY(Config, EditAnywhere, Category = "Speaker", meta = (ClampMin = "50"))
    float RetrieveDistance = 180;
    UPROPERTY(Config, EditAnywhere, Category = "Speaker", meta = (ClampMin = "5"))
    float RadioSize = 40;
    UPROPERTY(Config, EditAnywhere, Category = "Speaker", meta = (ClampMin = "5"))
    float MountedSize = 20;
};
