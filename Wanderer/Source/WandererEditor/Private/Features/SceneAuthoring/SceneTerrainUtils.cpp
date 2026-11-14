#include "Features/SceneAuthoring/SceneTerrainUtils.h"

#include "Features/SceneAuthoring/SceneTerrainData.h"

namespace
{
    using namespace SceneTerrainParameters;
    // Keep this numerical kernel together: extracting float intermediates changed encoded
    // heights at rounding boundaries. The frozen pre-refactor oracle checks exact results.
    TArray<uint16> BuildTerrainHeights(
        const FVector& Origin, int32 SizeX, int32 SizeY, float XYScale, float ZScale)
    {
        TArray<uint16> Heights;
        Heights.SetNumUninitialized(SizeX * SizeY);
        auto SmoothMask = [](float Distance, float Inner, float Outer)
        {
            return 1.0f - FMath::SmoothStep(Inner, Outer, Distance);
        };
        auto EllipseDistance = [](float X, float Y, float CX, float CY, float RX, float RY)
        {
            return FMath::Sqrt(FMath::Square((X - CX) / RX) + FMath::Square((Y - CY) / RY));
        };
        for (int32 Y = 0; Y < SizeY; ++Y)
        {
            for (int32 X = 0; X < SizeX; ++X)
            {
                const float WorldX = Origin.X + X * XYScale;
                const float WorldY = Origin.Y + Y * XYScale;
                const float EdgeDistance = FMath::Min(FMath::Min(X * XYScale, Y * XYScale),
                    FMath::Min((SizeX - 1 - X) * XYScale, (SizeY - 1 - Y) * XYScale));
                const float EdgeFade = FMath::SmoothStep(0.0f, EdgeFadeDistance, EdgeDistance);
                const float BroadNoise = FMath::PerlinNoise2D(
                    FVector2D(WorldX / BroadNoiseScale, WorldY / BroadNoiseScale));
                const float DetailNoise =
                    FMath::PerlinNoise2D(FVector2D(WorldX / DetailNoiseScale + DetailNoiseOffsetX,
                        WorldY / DetailNoiseScale - DetailNoiseOffsetY));
                const float FineNoise =
                    FMath::PerlinNoise2D(FVector2D(WorldX / FineNoiseScale - FineNoiseOffsetX,
                        WorldY / FineNoiseScale + FineNoiseOffsetY));
                const float LakeDistance = EllipseDistance(
                    WorldX, WorldY, LakeCenterX, LakeCenterY, LakeRadiusX, LakeRadiusY);
                const float DryGround = FMath::SmoothStep(DryGroundInner, LakeOuter, LakeDistance);
                float HeightCm = EdgeFade * DryGround
                                 * (BroadAmplitude * BroadNoise + DetailAmplitude * DetailNoise
                                     + FineAmplitude * FineNoise);
                const float FountainFlat = SmoothMask(EllipseDistance(WorldX,
                                                          WorldY,
                                                          FountainCenterX,
                                                          FountainCenterY,
                                                          FountainRadiusX,
                                                          FountainRadiusY),
                    FountainInner,
                    1.0f);
                HeightCm *= 1.0f - FountainFlat;
                const float Grass = SmoothMask(
                    EllipseDistance(
                        WorldX, WorldY, GrassCenterX, GrassCenterY, GrassRadiusX, GrassRadiusY),
                    SurfaceMaskInner,
                    1.0f);
                const float Snow = SmoothMask(
                    EllipseDistance(
                        WorldX, WorldY, SnowCenterX, SnowCenterY, SnowRadiusX, SnowRadiusY),
                    SurfaceMaskInner,
                    1.0f);
                const float Sand = SmoothMask(
                    EllipseDistance(
                        WorldX, WorldY, SandCenterX, SandCenterY, SandRadiusX, SandRadiusY),
                    SurfaceMaskInner,
                    1.0f);
                const float Soil = FMath::Clamp(1.0f - Grass - Snow - Sand, 0.0f, 1.0f);
                const float DuneNoise = FMath::PerlinNoise2D(FVector2D(
                    WorldX / DuneScaleX + DuneOffsetX, WorldY / DuneScaleY - DuneOffsetY));
                const float SnowDriftNoise =
                    FMath::PerlinNoise2D(FVector2D(WorldX / SnowNoiseScaleX - SnowNoiseOffsetX,
                        WorldY / SnowNoiseScaleY + SnowNoiseOffsetY));
                HeightCm +=
                    EdgeFade * DryGround
                    * (Grass
                            * (GrassDetailAmplitude * DetailNoise
                                + SurfaceFineAmplitude * FineNoise)
                        + Snow
                              * (SnowDriftAmplitude * SnowDriftNoise
                                  + SnowDetailAmplitude * DetailNoise)
                        + Sand * (SandDuneAmplitude * DuneNoise + SurfaceFineAmplitude * FineNoise)
                        + Soil
                              * (SoilDetailAmplitude * (1.0f - FMath::Abs(DetailNoise))
                                  + FineAmplitude * FineNoise));
                // A broad, walkable lake bed is independent of dry-ground relief.
                const float Lake = SmoothMask(LakeDistance, LakeInner, LakeOuter);
                HeightCm -= EdgeFade * LakeDepth * Lake;
                const int32 Encoded = FMath::RoundToInt(
                    HeightEncodingOffset + HeightCm * HeightEncodingScale / ZScale);
                Heights[Y * SizeX + X] =
                    static_cast<uint16>(FMath::Clamp(Encoded, 0, MaxEncodedHeight));
            }
        }
        return Heights;
    }
} // namespace

TArray<uint16> SceneTerrainUtils::BuildHeights(const FSceneTerrainGrid& InGrid)
{
    return BuildTerrainHeights(
        InGrid.Origin, InGrid.Size.X, InGrid.Size.Y, InGrid.XYScale, InGrid.ZScale);
}
