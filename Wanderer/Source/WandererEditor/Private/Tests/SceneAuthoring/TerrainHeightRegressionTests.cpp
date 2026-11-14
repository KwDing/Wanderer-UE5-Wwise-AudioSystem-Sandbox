#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Features/SceneAuthoring/SceneTerrainUtils.h"

// Frozen pre-refactor oracle: keep arithmetic independent of the production parameter table.
namespace
{
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
                const float EdgeFade = FMath::SmoothStep(0.0f, 350.0f, EdgeDistance);
                const float BroadNoise =
                    FMath::PerlinNoise2D(FVector2D(WorldX / 620.0f, WorldY / 620.0f));
                const float DetailNoise = FMath::PerlinNoise2D(
                    FVector2D(WorldX / 210.0f + 13.0f, WorldY / 210.0f - 7.0f));
                const float FineNoise =
                    FMath::PerlinNoise2D(FVector2D(WorldX / 90.0f - 19.0f, WorldY / 90.0f + 23.0f));
                const float LakeDistance =
                    EllipseDistance(WorldX, WorldY, -50.0f, -8950.0f, 1500.0f, 1000.0f);
                const float DryGround = FMath::SmoothStep(0.75f, 1.05f, LakeDistance);
                float HeightCm = EdgeFade * DryGround
                                 * (45.0f * BroadNoise + 28.0f * DetailNoise + 9.0f * FineNoise);
                const float FountainFlat = SmoothMask(
                    EllipseDistance(WorldX, WorldY, -350.0f, -4480.0f, 850.0f, 650.0f), 0.7f, 1.0f);
                HeightCm *= 1.0f - FountainFlat;
                const float Grass =
                    SmoothMask(EllipseDistance(WorldX, WorldY, -500.0f, -5150.0f, 1180.0f, 760.0f),
                        0.72f,
                        1.0f);
                const float Snow =
                    SmoothMask(EllipseDistance(WorldX, WorldY, -1250.0f, -6300.0f, 720.0f, 650.0f),
                        0.72f,
                        1.0f);
                const float Sand = SmoothMask(
                    EllipseDistance(WorldX, WorldY, 450.0f, -7850.0f, 900.0f, 620.0f), 0.72f, 1.0f);
                const float Soil = FMath::Clamp(1.0f - Grass - Snow - Sand, 0.0f, 1.0f);
                const float DuneNoise = FMath::PerlinNoise2D(
                    FVector2D(WorldX / 350.0f + 41.0f, WorldY / 480.0f - 17.0f));
                const float SnowDriftNoise = FMath::PerlinNoise2D(
                    FVector2D(WorldX / 300.0f - 31.0f, WorldY / 420.0f + 9.0f));
                HeightCm +=
                    EdgeFade * DryGround
                    * (Grass * (18.0f * DetailNoise + 8.0f * FineNoise)
                        + Snow * (26.0f * SnowDriftNoise + 11.0f * DetailNoise)
                        + Sand * (30.0f * DuneNoise + 8.0f * FineNoise)
                        + Soil * (16.0f * (1.0f - FMath::Abs(DetailNoise)) + 9.0f * FineNoise));
                // A broad, walkable lake bed is independent of dry-ground relief.
                const float Lake = SmoothMask(LakeDistance, 0.25f, 1.05f);
                HeightCm -= EdgeFade * 240.0f * Lake;
                const int32 Encoded = FMath::RoundToInt(32768.0f + HeightCm * 128.0f / ZScale);
                Heights[Y * SizeX + X] = static_cast<uint16>(FMath::Clamp(Encoded, 0, 65535));
            }
        }
        return Heights;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSceneTerrainRegressionTest,
    "Wanderer.SceneAuthoring.TerrainHeightEquivalence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSceneTerrainRegressionTest::RunTest(const FString& InParameters)
{
    const FVector Origins[] = {FVector(-1800, -10500, 0),
        FVector(-1500, -6400, 0),
        FVector(-900, -5000, 0),
        FVector::ZeroVector};
    const float Scales[] = {25.0f, 100.0f, 200.0f};
    constexpr int32 GridSize = 129;
    for (const FVector& Origin : Origins)
    {
        for (float Scale : Scales)
        {
            const TArray<uint16> Expected =
                BuildTerrainHeights(Origin, GridSize, GridSize, Scale, Scale);
            const TArray<uint16> Actual = SceneTerrainUtils::BuildHeights(
                {Origin, FIntPoint(GridSize, GridSize), Scale, Scale});
            if (!TestTrue(
                    TEXT("Every encoded height equals the original algorithm"), Actual == Expected))
            {
                for (int32 Index = 0; Index < Expected.Num(); ++Index)
                {
                    if (Actual[Index] != Expected[Index])
                    {
                        AddError(FString::Printf(
                            TEXT("Origin=%s scale=%f index=%d expected=%u actual=%u"),
                            *Origin.ToString(),
                            Scale,
                            Index,
                            Expected[Index],
                            Actual[Index]));
                        break;
                    }
                }
                return false;
            }
        }
    }
    return true;
}
#endif
