#pragma once

#include "CoreMinimal.h"

#include "Types/RadioControlTypes.h"

/** Borrowed values valid only for one synchronous mix update. */
class FRadioChannelLayout
{
public:
    static constexpr int32 SlotCount = 4;
    FRadioChannelLayout(
        const TArray<FRadioEmitterSnapshot>& InSources, FName InContentID, bool bInStereo);
    int32 GetChannelMask(int32 InSlot) const;
    float GetGain() const;
    const FRadioEmitterSnapshot* GetMember(int32 InSlot) const;
    bool HasPlaybackRequest() const
    {
        return bAnyPlaying;
    }

private:
    const FRadioEmitterSnapshot* Members[SlotCount] = {};
    int32 MemberCount = 0;
    bool bStereo = true;
    bool bAnyPlaying = false;
};
