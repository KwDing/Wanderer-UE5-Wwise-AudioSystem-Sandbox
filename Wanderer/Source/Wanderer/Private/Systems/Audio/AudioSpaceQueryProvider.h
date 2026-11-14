#pragma once

#include "CoreMinimal.h"

class APawn;
class UWorld;
struct FWandererSpaceDirection;

/** Read-only acoustic geometry snapshot; event publication belongs to the caller. */
class FAudioSpaceQueryProvider
{
public:
    static bool Capture(UWorld* InWorld, APawn* InPawn, float InQueryDistanceMaxM,
        TArray<FWandererSpaceDirection>& OutDirections);
};
