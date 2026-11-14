#pragma once

#include "CoreMinimal.h"
#include "Layout/Geometry.h"
#include "Rendering/DrawElements.h"

namespace MapArtworkUtils
{
    extern const FLinearColor Ink;
    extern const FLinearColor Paper;
    extern const FLinearColor Road;
    FLinearColor MapSurfaceColor(FName InSurface);
    void DrawRegionInk(const FGeometry& InGeometry,
        FSlateWindowElementList& InOutElements,
        int32 InLayer,
        const FSlateRect& InBounds,
        FName InSurface);
} // namespace MapArtworkUtils
