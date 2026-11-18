#pragma once

#include "CoreMinimal.h"

namespace AudioSchedulingDefaults
{
constexpr int32 TotalEvents = 48;
constexpr int32 AmbienceEvents = 24;
constexpr int32 DetailEvents = 16;
constexpr float UpdateIntervalSec = 0.2f;
constexpr float UpdateIntervalMinSec = 0.05f;
constexpr float UpdateIntervalMaxSec = 1.0f;
constexpr float PlayingPriorityBias = 2.0f;
constexpr int32 RegularSlots = 8;
constexpr int32 TransitionSlots = 2;
constexpr int32 EmergencySlots = 1;
constexpr int32 ConcurrentMediaLoads = 2;
constexpr float PreloadPriority = -100.0f;
constexpr float EmergencyPriority = 200.0f;
} // namespace AudioSchedulingDefaults

enum class EAudioEventResourceStatus : uint8
{
    Pending,
    Ready,
    Failed
};

/** Value-only admission facts. No profile assets or concrete source types cross this boundary. */
struct FAudioSchedulingInfo
{
    FString ProfileKey;
    FString OwnerPath;
    FName ResourceGroup;
    float Priority = 0.0f;
    int32 MaxInstances = 0;
    bool bProtected = false;
    bool bPlaying = false;
    bool bAmbience = false;
    bool bDetail = false;
};

/** Live admission limits; resource residency configuration has its own L2 contract. */
struct FAudioBudgetLimits
{
    int32 RegularSlots = AudioSchedulingDefaults::RegularSlots;
    int32 TransitionSlots = AudioSchedulingDefaults::TransitionSlots;
    int32 EmergencySlots = AudioSchedulingDefaults::EmergencySlots;
    int32 TotalEvents = AudioSchedulingDefaults::TotalEvents;
    int32 AmbienceEvents = AudioSchedulingDefaults::AmbienceEvents;
    int32 DetailEvents = AudioSchedulingDefaults::DetailEvents;
    float UpdateIntervalSec = AudioSchedulingDefaults::UpdateIntervalSec;
};
