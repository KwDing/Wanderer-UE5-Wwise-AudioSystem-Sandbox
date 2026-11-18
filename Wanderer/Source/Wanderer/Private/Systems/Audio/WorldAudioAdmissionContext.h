#pragma once

#include "CoreMinimal.h"

#include "Types/Audio/AudioSchedulingTypes.h"

/** Per-pass counters; the world subsystem owns the referenced diagnostic readback. */
struct FWorldAudioAdmissionContext
{
    FAudioBudgetLimits Limits;
    TMap<FName, int32>& Groups;
    int32& Admitted;
    int32& Overflow;
    TMap<FString, int32> Profiles;
    TSet<const UObject*> ClaimedObjects;
    int32 Ambiences = 0;
    int32 Details = 0;
    int32 RegularAdmitted = 0;
    int32 EmergencyAdmitted = 0;
    int32 DesiredRegular = 0;
    int32 DesiredEmergency = 0;
    int32 StartsAvailable = 0;

    FWorldAudioAdmissionContext(const FAudioBudgetLimits& InLimits,
        TMap<FName, int32>& InGroups, int32& InAdmitted, int32& InOverflow)
        : Limits(InLimits), Groups(InGroups), Admitted(InAdmitted), Overflow(InOverflow)
    {
        Limits.TotalEvents = FMath::Max(1, Limits.TotalEvents);
        Limits.AmbienceEvents = FMath::Max(1, Limits.AmbienceEvents);
        Limits.DetailEvents = FMath::Max(1, Limits.DetailEvents);
        Limits.RegularSlots = FMath::Clamp(Limits.RegularSlots, 1, 64);
        Limits.TransitionSlots = FMath::Clamp(Limits.TransitionSlots, 0, 16);
        Limits.EmergencySlots = FMath::Clamp(Limits.EmergencySlots, 0, 8);
    }

    bool IsFull(const FAudioSchedulingInfo& InInfo, bool bInGroupFull) const
    {
        return bInGroupFull || RegularAdmitted >= Limits.TotalEvents
            || (InInfo.bAmbience && Ambiences >= Limits.AmbienceEvents)
            || (InInfo.bDetail && Details >= Limits.DetailEvents)
            || Profiles.FindRef(InInfo.ProfileKey) >= InInfo.MaxInstances;
    }

    void RecordAdmission(const FAudioSchedulingInfo& InInfo,
        bool bInFull, const UObject* InPlaybackIdentity)
    {
        Overflow += bInFull;
        ++Admitted;
        ++RegularAdmitted;
        ++Groups.FindOrAdd(InInfo.ResourceGroup);
        Ambiences += InInfo.bAmbience;
        Details += InInfo.bDetail;
        ++Profiles.FindOrAdd(InInfo.ProfileKey);
        if (InPlaybackIdentity)
        {
            ClaimedObjects.Add(InPlaybackIdentity);
        }
    }
};
