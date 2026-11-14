#pragma once

#include "CoreMinimal.h"

class UAkAudioEvent;
class UWandererBuildingAmbienceComponent;

/** Owns one synchronous building edit; shared ambience never rewrites individual Room events. */
class FBuildingAudioEditOperation
{
  public:
    /** Independent editor transaction. Callers handle failure and explicitly save the map. */
    static UWandererBuildingAmbienceComponent* Create(const TArray<AActor*>& InRooms, FName InName,
                                                      UAkAudioEvent* InEvent, FString& OutError,
                                                      TFunction<bool()> InCanContinue = {});

    static bool ApplyResourceGroup(const TArray<UWandererBuildingAmbienceComponent*>& InBuildings,
                                   FName InGroup, UWorld* InWorld, FString& OutError,
                                   TFunction<bool()> InCanContinue = {});
};
