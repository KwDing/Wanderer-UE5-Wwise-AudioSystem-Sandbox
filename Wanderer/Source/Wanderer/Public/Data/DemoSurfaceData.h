#pragma once

#include "CoreMinimal.h"

/** Authored Demo coordinates, not a general terrain classification policy. */
namespace UE::Wanderer::DemoSurface
{
    inline constexpr TCHAR MapSuffix[] = TEXT("L_AudioSystemDemo");
    constexpr double LakeCenterX = 950.0;
    constexpr double LakeCenterY = -8950.0;
    constexpr double OriginalLakeRadiusX = 1550.0;
    constexpr double OriginalLakeRadiusY = 1000.0;
    constexpr double LakeRadiusX = 1705.0;
    constexpr double LakeRadiusY = 1080.0;
    constexpr double LakeContainmentInset = 0.99;
    constexpr double ShoreFadeStart = 1.0;
    constexpr double ShoreFadeEnd = 1.09;
    constexpr double MoistureHeightStart = -20.0;
    constexpr double MoistureHeightEnd = -8.0;
    constexpr double SnowCenterX = -1250.0;
    constexpr double SnowCenterY = -6300.0;
    constexpr double SnowRadiusX = 680.0;
    constexpr double SnowRadiusY = 630.0;
    constexpr double PoolContainmentInset = 0.98;
    constexpr double RearPoolWidthRatio = 505.0 / 620.0;
    constexpr double RearPoolEdgeInset = 2.0;
    constexpr double LakeFirstBayAngle = 0.55;
    constexpr double LakeFirstBayWidth = 0.4;
    constexpr double LakeSecondBayAngle = 2.3;
    constexpr double LakeBaseRadius = 0.94;
    constexpr double LakeFirstBayDepth = 0.25;
    constexpr double LakeSecondBayDepth = 0.20;
    constexpr double LakeShoreWarp = 0.05;
    constexpr double LakeShorePhase = 1.1;
    constexpr double SnowBaseRadius = 0.87;
    constexpr double SnowFirstWarp = 0.09;
    constexpr double SnowFirstPhase = 0.5;
    constexpr double SnowSecondWarp = 0.06;
    constexpr double SnowSecondPhase = 0.8;
    constexpr double SandHorizontalWarp = 0.18;
    constexpr double SandHorizontalFrequency = 3.1;
    constexpr double SandHorizontalPhase = 0.7;
    constexpr double SandVerticalWarp = 0.14;
    constexpr double SandVerticalFrequency = 3.8;
    constexpr double LakeSecondBayWidth = 0.55;
    constexpr double SandVerticalPhase = 0.4;
} // namespace UE::Wanderer::DemoSurface
