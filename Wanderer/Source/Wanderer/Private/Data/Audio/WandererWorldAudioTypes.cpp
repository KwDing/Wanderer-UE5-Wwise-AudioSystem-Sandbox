#include "Data/Audio/WandererWorldAudioTypes.h"

FString FWandererWorldAudioProfileRow::Validate() const
{
    if (Mode == EWandererWorldAudioMode::Spline
        && (!FMath::IsFinite(SplineBlendDistanceM) || SplineBlendDistanceM <= 0
            || !FMath::IsFinite(SplineBlendSeconds) || SplineBlendSeconds < .05f
            || SplineBlendSeconds > 2))
    {
        return TEXT("Spline blend distance must be positive; transition must be 0.05–2 seconds");
    }
    if (PlayEvent.IsNull())
    {
        return TEXT("缺少 Play Event");
    }
    if (!FMath::IsFinite(AudibleDistanceM) || !FMath::IsFinite(ActivationDistanceM)
        || !FMath::IsFinite(ReleaseDistanceM) || AudibleDistanceM < 0
        || ActivationDistanceM < AudibleDistanceM || ReleaseDistanceM <= ActivationDistanceM)
    {
        return TEXT("距离必须满足 0 ≤ 可听尾部 ≤ 启动距离 < 释放距离");
    }
    if (!FMath::IsFinite(Priority) || Priority < 0 || Priority > 100
        || !FMath::IsFinite(DistancePriorityPenalty) || DistancePriorityPenalty < 0
        || DistancePriorityPenalty > 100 || !FMath::IsFinite(ProtectNearM) || ProtectNearM < 0)
    {
        return TEXT("优先级/距离惩罚须为0～100，近处保护距离须非负");
    }
    if (MaxInstances < 1 || !FMath::IsFinite(FadeOutSec) || FadeOutSec < 0
        || FadeOutSec > 2 || !FMath::IsFinite(FadeInSec) || FadeInSec < 0 || FadeInSec > 2)
    {
        return TEXT("实例上限至少1，淡出时间须为0～2秒");
    }
    if (!FMath::IsFinite(ReverbTailHoldSec) || ReverbTailHoldSec < 0)
    {
        return TEXT("Reverb tail hold must be finite and nonnegative");
    }
    return FString();
}
