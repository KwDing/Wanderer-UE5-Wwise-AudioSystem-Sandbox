#include "Data/Audio/WandererObjectAudioProfile.h"

bool UWandererObjectAudioProfile::Validate() const
{
    if (LoopLimit < 0 || ShortEventLimit < 0 || !FMath::IsFinite(Priority)
        || Priority < 0 || Priority > 100 || !FMath::IsFinite(ActivationDistanceM)
        || ActivationDistanceM <= 0 || !FMath::IsFinite(ReleaseDistanceM)
        || ReleaseDistanceM < ActivationDistanceM || !FMath::IsFinite(FadeInSec)
        || FadeInSec < 0 || !FMath::IsFinite(FadeOutSec) || FadeOutSec < 0)
    {
        return false;
    }
    TSet<FName> EventIDs;
    for (const FObjectAudioEvent& Event : Events)
    {
        if (Event.EventID.IsNone() || Event.EventSoftPtr.IsNull() || EventIDs.Contains(Event.EventID)
            || (Event.bLoop && Event.bEmergency) || !FMath::IsFinite(Event.StartDeadlineSec)
            || Event.StartDeadlineSec <= 0)
        {
            return false;
        }
        EventIDs.Add(Event.EventID);
    }
    return true;
}
