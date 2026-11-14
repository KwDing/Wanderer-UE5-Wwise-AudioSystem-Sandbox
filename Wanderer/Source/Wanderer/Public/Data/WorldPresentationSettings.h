#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "WorldPresentationSettings.generated.h"

/**
 * Explicit compatibility rules for authored maps; reusable components do not assume Demo
 * content.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Wanderer World Presentation"))
class WANDERER_API UWorldPresentationSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UPROPERTY(Config, EditAnywhere, Category = "Surface")
    TArray<FName> LegacySurfaceMaps = {TEXT("L_AudioSystemDemo")};

    UPROPERTY(Config, EditAnywhere, Category = "Surface")
    FString PuddleNameToken = TEXT("Puddle");

    UPROPERTY(Config, EditAnywhere, Category = "Surface")
    FString PoolMaterialPrefix = TEXT("M_FountainWater_");

    UPROPERTY(Config, EditAnywhere, Category = "Surface")
    FName PuddleSurfaceTag = TEXT("Puddle");

    UPROPERTY(Config, EditAnywhere, Category = "Surface")
    FName WaterSurfaceTag = TEXT("WaterSurface");

    UPROPERTY(Config, EditAnywhere, Category = "Fill Lighting")
    TArray<FName> AutomaticFillReceiverMaps = {TEXT("L_AudioSystemDemo")};

    UPROPERTY(Config, EditAnywhere, Category = "Fill Lighting")
    FName FillReceiverTag = TEXT("CameraFillReceiver");

    static bool MatchesMap(const FString& InMapName, const TArray<FName>& InMaps);
};
