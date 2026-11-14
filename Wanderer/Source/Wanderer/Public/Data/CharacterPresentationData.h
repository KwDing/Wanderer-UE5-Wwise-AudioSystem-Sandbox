#pragma once

#include "CoreMinimal.h"

struct FCharacterCameraConfig
{
    float MinimumDistance = 200.0f;
    float MaximumDistance = 800.0f;
    float ZoomStep = 40.0f;
    float InterpolationSpeed = 12.0f;
};

namespace CharacterPresentationSettings
{
    constexpr float InitialCameraDistance = 400.0f;
    constexpr float MinimumCameraDistance = 50.0f;
    constexpr float MinimumZoomStep = 1.0f;
    constexpr float MinimumZoomSpeed = 1.0f;
    constexpr float SceneLightRefreshSeconds = 5.0f;
    constexpr float LightingSampleSeconds = 0.35f;
    constexpr float FillInterpolationSpeed = 3.0f;
    constexpr float FillVisibilityThreshold = 0.01f;
    constexpr float SecondaryFillRatio = 0.5f;
    constexpr float SunTraceDistance = 20000.0f;
    constexpr float SkyTraceDistance = 10000.0f;
    constexpr float SunIntensityScale = 3.0f;
    constexpr float SkyIntensityScale = 0.8f;
    constexpr float RadiusFadeExponent = 4.0f;
    constexpr float MinimumSquaredLightDistanceM = 0.25f;
    constexpr float LocalLightIntensityScale = 8.0f;
    constexpr float MetersPerCentimeter = 0.01f;
    constexpr float ViewTraceDistance = 1000.0f;
    constexpr float ViewSurfaceOffset = 15.0f;
    inline const FVector KeyLightOffset(150.0f, 80.0f, 60.0f);
    inline const FVector FillLightOffset(100.0f, -140.0f, 20.0f);
    inline const FVector BodySampleOffset(0.0f, 0.0f, 30.0f);
} // namespace CharacterPresentationSettings
