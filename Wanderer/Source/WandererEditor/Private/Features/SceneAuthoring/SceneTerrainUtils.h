#pragma once

#include "CoreMinimal.h"

#include "Features/SceneAuthoring/SceneAuthoringTypes.h"

namespace SceneTerrainUtils
{
    /** Caller supplies positive finite scales and dimensions bounded by the authoring policy. */
    TArray<uint16> BuildHeights(const FSceneTerrainGrid& InGrid);
} // namespace SceneTerrainUtils
