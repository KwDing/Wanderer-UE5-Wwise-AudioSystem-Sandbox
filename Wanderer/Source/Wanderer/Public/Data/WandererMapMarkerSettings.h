#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "Types/MapTypes.h"

#include "WandererMapMarkerSettings.generated.h"

class UWorld;

/** Per-scene minimap annotations; teleport logic does not depend on this configuration. */
UCLASS(Config = Game, DefaultConfig)
class WANDERER_API UWandererMapMarkerSettings : public UObject
{
    GENERATED_BODY()

  public:
    const TArray<FWandererMapDoorMarker>& GetDoorMarkers() const
    {
        return DoorMarkers;
    }
    const FWandererMapDoorMarker* FindDoorMarker(const UWorld& InWorld, FName InID) const;
    static FString GetScenePackage(const UWorld& InWorld);
    FText GetSceneTitle(const FString& InScenePackage) const;

  private:
    UPROPERTY(Config, EditAnywhere, Category = "Map")
    TArray<FWandererMapDoorMarker> DoorMarkers;
    UPROPERTY(Config, EditAnywhere, Category = "Map")
    TMap<FString, FString> SceneTitles;
};
