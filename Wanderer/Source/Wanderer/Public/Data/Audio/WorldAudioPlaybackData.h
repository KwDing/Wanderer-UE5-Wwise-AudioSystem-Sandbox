#pragma once

namespace WorldAudioPlayback
{
    constexpr float CentimetersPerMeter = 100.0f;
    constexpr float MsPerSec = 1000.0f;
    constexpr double AssetRetryDelaySec = 5.0;
    constexpr double MediaTimeoutSec = 15.0;
    constexpr double TimeoutRetryDelaySec = 10.0;
    constexpr double PlaybackRetryDelaySec = 2.0;
    constexpr int PriorityInterpolationMs = 100;
    constexpr float FadeDurationMaxSec = 2.0f;
} // namespace WorldAudioPlayback
