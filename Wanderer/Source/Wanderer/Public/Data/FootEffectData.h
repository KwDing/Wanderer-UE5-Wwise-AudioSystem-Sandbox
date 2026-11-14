#pragma once
#include "CoreMinimal.h"
namespace FootEffectSettings
{
    constexpr int32 MaximumEffects = 128;
    constexpr float PrintLifetime = 45.0f;
    constexpr float DustLifetime = 1.25f;
    constexpr float TickSeconds = 1.0f / 60.0f;
    constexpr float FootSideOffset = 12.0f;
    constexpr float TraceHeight = 30.0f;
    constexpr float TraceDepth = 90.0f;
    constexpr float MinimumNormalZ = 0.5f;
    constexpr float SandDustScale = 0.6f;
    constexpr float DefaultDustScale = 0.4f;
    constexpr float PrintHalfLength = 17.0f;
    constexpr float PrintHalfWidth = 10.0f;
    constexpr float PrintDepth = 6.0f;
    constexpr float FadeStartSeconds = 35.0f;
    constexpr float FadeSeconds = 10.0f;
    constexpr const TCHAR* DustRoot = TEXT("/Game/VFX/Particles/Ground/Footprints/");
    constexpr const TCHAR* PrintRoot = TEXT("/Game/Materials/Ground/Footprints/");
} // namespace FootEffectSettings
