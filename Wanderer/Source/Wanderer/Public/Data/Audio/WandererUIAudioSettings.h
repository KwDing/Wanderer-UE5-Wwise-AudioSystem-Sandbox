#pragma once

#include "Engine/DeveloperSettings.h"

#include "WandererUIAudioSettings.generated.h"

class UWandererUIAudioProfile;

/** Routing only; all Wwise rules live in profiles/tables under /Game/Config/Audio/UI. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Wanderer UI Audio"))
class WANDERER_API UWandererUIAudioSettings : public UDeveloperSettings
{
    GENERATED_BODY()

  public:
    virtual FName GetCategoryName() const override
    {
        return TEXT("Game");
    }
    TSoftObjectPtr<UWandererUIAudioProfile> GetProfileReference(const UClass* WidgetClass) const;

    UPROPERTY(Config, EditAnywhere, Category = "Configuration")
    TSoftObjectPtr<UWandererUIAudioProfile> DefaultProfile;

    UPROPERTY(Config,
        EditAnywhere,
        Category = "Configuration",
        meta = (ToolTip =
                    "Widget class name, e.g. WandererMapWidget. Blueprint classes include _C. Profiles inherit defaults through ParentProfile."))
    TMap<FName, TSoftObjectPtr<UWandererUIAudioProfile>> ScreenProfiles;
};
