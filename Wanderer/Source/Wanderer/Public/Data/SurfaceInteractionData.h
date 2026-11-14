#pragma once

#include "CoreMinimal.h"

/** Defaults matched to the existing authored water materials and local solver contract. */
namespace UE::Wanderer::SurfaceInteraction
{
    constexpr float GrassTickInterval = 0.05f;
    constexpr float GrassResponseRadius = 85.0f;
    constexpr int32 MaximumContactWaves = 6;
    constexpr float ContactWaveLifetime = 1.5f;
    constexpr float ContactWaveSpeed = 85.0f;
    constexpr int32 MaximumSprays = 12;
    constexpr float EntrySprayLifetime = 0.45f;
    constexpr float SwimmingSprayLifetime = 0.18f;
    constexpr float MotionReferenceSpeed = 375.0f;
    constexpr float EntryReferenceSpeed = 600.0f;
    constexpr float MinimumSpraySpeed = 150.0f;
    constexpr float MinimumEntryFallSpeed = 150.0f;
    constexpr float ImpactTravelDistance = 40.0f;
    constexpr float ContactInterval = 0.08f;
    constexpr float StrikeInterval = 0.22f;
    constexpr float NoContactTime = -10.0f;
    constexpr float MinimumSampleTime = 0.001f;
    constexpr float WaterContactBand = 28.0f;
    constexpr float SurfaceCrossingHeight = 8.0f;
    constexpr float FootPlantHeight = 16.0f;
    constexpr float FootDescendingSpeed = -15.0f;
    constexpr float FootSettledSpeed = -5.0f;
    constexpr float MinimumStrokeTravel = 1.0f;
    constexpr float StrokeDistance = 22.0f;
    constexpr float AnimatedReferenceSpeed = 240.0f;
    constexpr float MaximumAnimatedMotion = 0.35f;
    constexpr float SprayFollowBand = 20.0f;
    constexpr float MaximumSprayFollowDistance = 75.0f;
    constexpr int32 GrassProjectionBudget = 256;
    constexpr double GrassTraceDistance = 2000.0;
    constexpr double GrassGroundOffset = 0.4;
    constexpr double BodyFilmAllowance = 1.5;
    constexpr int32 BodyEdgeIterations = 12;
    constexpr double BodyEdgeTolerance = 0.2;
    constexpr double BodyEdgeMaximumStep = 12.0;
    constexpr double BodyEdgeMinimumGradient = 0.1;
    constexpr float ContactWaveMinStrength = 0.25f;
    constexpr float ContactWaveMaxStrength = 0.6f;
    constexpr float ContactWaveRadius = 7.0f;
    constexpr float ContactImpactMinStrength = 0.07f;
    constexpr float ContactImpactMaxStrength = 0.11f;
    constexpr float ContactImpactMinRadius = 4.0f;
    constexpr float ContactImpactMaxRadius = 6.0f;
    constexpr float ContactSprayMinPower = 0.35f;
    constexpr float ContactSprayMaxPower = 0.75f;
    constexpr float SprayHeightOffset = 1.0f;
    constexpr float SprayMinAmount = 0.6f;
    constexpr float SprayMaxAmount = 1.8f;
    constexpr float SprayMinPower = 0.12f;
    constexpr float SprayMaxPower = 0.48f;
    constexpr float SprayMinRadius = 1.5f;
    constexpr float SprayMaxRadius = 4.0f;
    constexpr float MinimumSplashDepth = 3.0f;
    constexpr float SplashDepthTraceDistance = 70.0f;
    constexpr float MinimumEntryEnergy = 0.2f;
    constexpr float MinimumEntryWaveRadius = 8.0f;
    constexpr float MaximumEntryWaveRadius = 30.0f;
    constexpr int32 EntrySprayDirections = 4;
    constexpr float MinimumWaveRadius = 4.0f;
    constexpr float MaximumWaveRadius = 40.0f;
    constexpr float WaveBoundsMargin = 18.0f;
    constexpr float EntryEnergyDecayRate = 4.0f;
    constexpr float BodyMotionResponseRate = 6.0f;
    constexpr float WaterlineBoundsMargin = 48.0f;
    constexpr float WaterlineHeightOffset = 1.2f;
    constexpr float WaterlineMeshSize = 100.0f;
    constexpr float WaterNormalPower = 3.8f;
    constexpr float WaterFootAllowance = 2.0f;
    constexpr float EntryImpactMinStrength = 0.075f;
    constexpr float EntryImpactMaxStrength = 0.18f;
    constexpr float EntryImpactMinRadius = 7.0f;
    constexpr float EntryImpactMaxRadius = 12.0f;
    constexpr float TravelImpactMinStrength = 0.06f;
    constexpr float TravelImpactMaxStrength = 0.095f;
    constexpr float TravelImpactMinRadius = 5.0f;
    constexpr float TravelImpactMaxRadius = 7.0f;
} // namespace UE::Wanderer::SurfaceInteraction
