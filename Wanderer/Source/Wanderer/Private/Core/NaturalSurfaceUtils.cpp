#include "Core/NaturalSurfaceUtils.h"

namespace UE::Wanderer::Contours
{
    double LakeOutline(double Angle)
    {
        const double BayA = FMath::UnwindRadians(Angle - DemoSurface::LakeFirstBayAngle)
                            / DemoSurface::LakeFirstBayWidth;
        const double BayB = FMath::UnwindRadians(Angle + DemoSurface::LakeSecondBayAngle)
                            / DemoSurface::LakeSecondBayWidth;
        return DemoSurface::LakeBaseRadius
               - DemoSurface::LakeFirstBayDepth * FMath::Exp(-BayA * BayA)
               - DemoSurface::LakeSecondBayDepth * FMath::Exp(-BayB * BayB)
               + DemoSurface::LakeShoreWarp * FMath::Cos(Angle - DemoSurface::LakeShorePhase);
    }

    double LakeRadius(const FVector& Point, double RadiusX, double RadiusY)
    {
        const FVector2D Local((Point.X - DemoSurface::LakeCenterX) / RadiusX,
            (Point.Y - DemoSurface::LakeCenterY) / RadiusY);
        return Local.Size() / LakeOutline(FMath::Atan2(Local.Y, Local.X));
    }

    double LakeGroundMoisture(const FVector& Ground)
    {
        const double Shore =
            1.0
            - FMath::SmoothStep(DemoSurface::ShoreFadeStart,
                DemoSurface::ShoreFadeEnd,
                LakeRadius(Ground, DemoSurface::LakeRadiusX, DemoSurface::LakeRadiusY));
        const double Height =
            1.0
            - FMath::SmoothStep(
                DemoSurface::MoistureHeightStart, DemoSurface::MoistureHeightEnd, Ground.Z);
        return Shore * Height;
    }

    double SnowOutline(double Angle)
    {
        return DemoSurface::SnowBaseRadius
               + DemoSurface::SnowFirstWarp * FMath::Sin(2.0 * Angle + DemoSurface::SnowFirstPhase)
               + DemoSurface::SnowSecondWarp
                     * FMath::Cos(3.0 * Angle - DemoSurface::SnowSecondPhase);
    }

    double SnowRadius(const FVector& Point)
    {
        const FVector2D Local((Point.X - DemoSurface::SnowCenterX) / DemoSurface::SnowRadiusX,
            (Point.Y - DemoSurface::SnowCenterY) / DemoSurface::SnowRadiusY);
        return Local.Size() / SnowOutline(FMath::Atan2(Local.Y, Local.X));
    }

    double SandCoverage(
        const FVector& Point, double CX, double CY, double RX, double RY, double BlendStart)
    {
        const double X = (Point.X - CX) / RX;
        const double Y = (Point.Y - CY) / RY;
        const double WarpedX = X
                               + DemoSurface::SandHorizontalWarp
                                     * FMath::Sin(Y * DemoSurface::SandHorizontalFrequency
                                                  + DemoSurface::SandHorizontalPhase);
        const double WarpedY = Y
                               + DemoSurface::SandVerticalWarp
                                     * FMath::Sin(X * DemoSurface::SandVerticalFrequency
                                                  - DemoSurface::SandVerticalPhase);
        return 1.0 - FMath::SmoothStep(BlendStart, 1.0, FVector2D(WarpedX, WarpedY).Size());
    }
} // namespace UE::Wanderer::Contours
