#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "GameStartupSettings.generated.h"

class APawn;

/** Native GameMode fallback; map/GameMode Blueprint overrides remain authoritative. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Wanderer Game Startup"))
class WANDERER_API UGameStartupSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UPROPERTY(Config, EditAnywhere, Category = "Spawn")
    TSoftClassPtr<APawn> DefaultPawnClassSoftPtr;

    UGameStartupSettings();
};
