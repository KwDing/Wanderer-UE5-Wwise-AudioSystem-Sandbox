#include "Systems/Audio/RadioChannelLayout.h"

FRadioChannelLayout::FRadioChannelLayout(
    const TArray<FRadioEmitterSnapshot>& Sources, FName ContentID, bool bInStereo)
    : bStereo(bInStereo)
{
    for (const FRadioEmitterSnapshot& WeakSource : Sources)
    {
        const FRadioEmitterSnapshot* Source = &WeakSource;
        if (!Source || Source->ContentID != ContentID)
        {
            continue;
        }
        const int32 Slot = Source->Number - 1;
        if (Slot < 0 || Slot >= SlotCount || Members[Slot])
        {
            continue;
        }
        Members[Slot] = Source;
        ++MemberCount;
        bAnyPlaying |= Source->bPlaying;
    }
}

ERadioChannel FRadioChannelLayout::GetChannel(int32 InSlot) const
{
    if (!GetMember(InSlot))
    {
        return ERadioChannel::None;
    }
    if (!bStereo)
    {
        return ERadioChannel::Center;
    }

    // Pair by device number within this content, including paused devices.
    int32 Rank = 0;
    for (int32 Earlier = 0; Earlier < InSlot; ++Earlier)
    {
        if (Members[Earlier])
        {
            ++Rank;
        }
    }
    if (MemberCount == 1 || (MemberCount == 3 && Rank == 2))
    {
        return ERadioChannel::Stereo;
    }
    return Rank % 2 == 0 ? ERadioChannel::Left : ERadioChannel::Right;
}

float FRadioChannelLayout::GetGain() const
{
    // Equal-power compensation per complete stereo copy, or per mono source.
    const int32 Copies = bStereo ? (MemberCount >= 3 ? 2 : 1) : FMath::Max(1, MemberCount);
    return FMath::InvSqrt(static_cast<float>(Copies));
}

const FRadioEmitterSnapshot* FRadioChannelLayout::GetMember(int32 InSlot) const
{
    return InSlot >= 0 && InSlot < SlotCount ? Members[InSlot] : nullptr;
}
