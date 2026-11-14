#pragma once
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

/** Contact-only values; no controller, component or emitter lifetime escapes. */
struct FCharacterSurfaceSnapshot
{
    FName Surface = TEXT("ground");
    TEnumAsByte<EPhysicalSurface> PhysicalSurface = SurfaceType_Default;
    FName Wetness = TEXT("dry");
};
