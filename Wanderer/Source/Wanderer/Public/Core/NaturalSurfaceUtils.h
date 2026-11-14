#pragma once
#include "CoreMinimal.h"

#include "Data/DemoSurfaceData.h"

namespace UE::Wanderer::Contours
{
    WANDERER_API double LakeOutline(double Angle);
    WANDERER_API double LakeRadius(const FVector& Point,
        double RadiusX = DemoSurface::OriginalLakeRadiusX,
        double RadiusY = DemoSurface::OriginalLakeRadiusY);
    WANDERER_API double LakeGroundMoisture(const FVector& Ground);
    WANDERER_API double SnowOutline(double Angle);
    WANDERER_API double SnowRadius(const FVector& Point);
    WANDERER_API double SandCoverage(
        const FVector& Point, double CX, double CY, double RX, double RY, double BlendStart);
} // namespace UE::Wanderer::Contours
