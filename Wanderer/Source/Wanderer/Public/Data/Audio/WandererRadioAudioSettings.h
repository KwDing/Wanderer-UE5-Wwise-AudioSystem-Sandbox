#pragma once

#include "Engine/DeveloperSettings.h"

#include "WandererRadioAudioSettings.generated.h"

class UAkAudioEvent;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Wanderer Radio Audio"))
class WANDERER_API UWandererRadioAudioSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UPROPERTY(Config, EditAnywhere, Category = "World Budget", meta = (ClampMin = "0", ClampMax = "100"))
    float Priority = 80;

    UPROPERTY(Config, EditAnywhere, Category = "World Budget", meta = (ClampMin = "0.01", Units = "m"))
    float ActivationDistanceM = 35;

    UPROPERTY(Config, EditAnywhere, Category = "World Budget", meta = (ClampMin = "0.01", Units = "m"))
    float ReleaseDistanceM = 42;

    UPROPERTY(Config, EditAnywhere, Category = "World Budget", meta = (ClampMin = "0", Units = "s"))
    float FadeOutSec = 0.35f;

    UPROPERTY(Config, EditAnywhere, Category = "Radio")
    TSoftObjectPtr<UAkAudioEvent> PlayEvent{
        FSoftObjectPath(TEXT("/Game/WwiseAudio/Events/Objects/Play_Radio.Play_Radio"))};

    UPROPERTY(Config, EditAnywhere, Category = "Radio")
    TSoftObjectPtr<UAkAudioEvent> StopEvent{
        FSoftObjectPath(TEXT("/Game/WwiseAudio/Events/Objects/Stop_Radio.Stop_Radio"))};
};
