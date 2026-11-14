#pragma once

#include "CoreMinimal.h"
#include "SceneAuthoringTypes.generated.h"

UENUM(BlueprintType)
enum class EWandererStepCollisionMode : uint8
{
    ModelSurface,
    RegeneratedConvex
};

struct FSceneTerrainGrid
{
    FVector Origin;
    FIntPoint Size;
    float XYScale;
    float ZScale;
};
