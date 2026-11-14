#pragma once

#include "CoreMinimal.h"

/** Central compile-time tuning. Consumers must not duplicate values or UI descriptions. */
namespace AudioVisualizationSettings
{
    constexpr float LineLifetimeSec = 0.25f;
    constexpr float TextLifetimeSec = 0.12f;
    constexpr float TextScale = 0.85f;
    constexpr float LineThickness = 2.0f;
    constexpr float EmitterPointSize = 18.0f;
    constexpr float PawnPointSize = 12.0f;
    constexpr float SplinePointSize = 6.0f;
    constexpr double CentimetersPerMeter = 100.0;
    constexpr double ReferenceViewportWidth = 1920.0;
    constexpr double LabelPerspectiveScale = 0.012;
    constexpr double LabelSizeMin = 6.0;
    constexpr double LabelSizeMax = 240.0;
    constexpr double LabelRightOffset = 2.0;
    constexpr double LabelUpOffset = 1.7;
    constexpr float LabelSpacing = 14.0f;
    constexpr float TextHeight = 25.0f;
    constexpr float NearZeroDistanceM = 0.01f;
    constexpr int32 HueStride = 83;
    constexpr uint8 ColorSaturation = 210;
    constexpr uint8 ColorValue = 255;
    constexpr int32 SplinePointCountMax = 256;
    constexpr int32 CurveSteps = 8;

    constexpr uint32 DefaultDrawBatchID = 0x57415344;
    constexpr uint32 FirstOwnedDrawBatchID = 0x57415400;
    constexpr int32 RelationCountMax = 16;
    constexpr bool bPrioritizePostedSources = true;
    constexpr float TickIntervalSec = 0.1f;
    constexpr double DiscoveryIntervalSec = 1.0;
    constexpr float DefaultRangeM = 20.0f;
    constexpr float RangeMinM = 1.0f;
    constexpr float RangeMaxM = 1000.0f;
    constexpr float SplineBlendDistanceM = 1.0f;
    constexpr int32 SplinePointCountMin = 2;
    constexpr int32 LinearSplineSteps = 1;
    constexpr double DigitSpacing = 1.4;
    inline const FLinearColor PawnColor = FLinearColor::Yellow;
    inline const FLinearColor EditorPlaybackColor = FLinearColor::Green;
    inline const FLinearColor SplinePointColor = FLinearColor::White;
    inline const FLinearColor SplineLineColor = FLinearColor(0, 1, 1);

    // Seven-segment glyph geometry is fixed data, not a tuning policy.
    inline const FVector2D DigitPoints[] = {{0, 1.5}, {1, 1.5}, {1, .75}, {1, 0}, {0, 0}, {0, .75}};
    constexpr int32 SegmentStarts[] = {0, 1, 2, 3, 4, 5, 5};
    constexpr int32 SegmentEnds[] = {1, 2, 3, 4, 5, 0, 2};
    constexpr uint8 DigitMasks[] = {63, 6, 91, 79, 102, 109, 125, 7, 127, 111};
} // namespace AudioVisualizationSettings

namespace AudioManagerRefreshSettings
{
    constexpr float TickIntervalSec = 0.1f;
    constexpr double StateRefreshIntervalSec = 1.0;
} // namespace AudioManagerRefreshSettings
