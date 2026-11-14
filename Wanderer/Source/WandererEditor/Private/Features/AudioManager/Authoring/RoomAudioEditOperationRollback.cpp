#include "Features/AudioManager/Authoring/RoomAudioEditOperation.h"

#include "UObject/UObjectIterator.h"

#include "AkAudioEvent.h"
#include "AkRoomComponent.h"
#include "AkSpatialAudioVolume.h"

#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Features/AudioManager/Authoring/WorldAudioProfileRepository.h"

namespace
{
    bool IsReferenced(UDataTable* InTable, FName InRow)
    {
        for (TObjectIterator<UWandererAudioSourceComponent> It; It; ++It)
        {
            if (!IsValid(*It) || !IsValid(It->GetOwner())
                || It->GetOwner()->IsActorBeingDestroyed())
            {
                continue;
            }
            if (It->Profile.DataTable == InTable && It->Profile.RowName == InRow)
            {
                return true;
            }
        }
        return false;
    }

} // namespace

bool FRoomAudioEditOperation::Rollback()
{
    bool bRestored = true;
    for (int32 Index = Items.Num() - 1; Index >= 0; --Index)
    {
        FItem& Item = Items[Index];
        if (Item.bCreated)
        {
            if (Item.SourceWeakPtr.IsValid())
            {
                Item.SourceWeakPtr->DestroyComponent();
            }
        }
        else if (Item.bWritten)
        {
            if (IsLive(Item) && Item.SourceWeakPtr->Profile == Item.WrittenProfile
                && (Request.Action != ERoomAudioEdit::Associate
                    || (Item.SourceWeakPtr->AcousticRoom.Get() == Item.RoomWeakPtr.Get()
                        && Item.SourceWeakPtr->ResourceGroup == Item.Group)))
            {
                Item.SourceWeakPtr->Profile = Item.PreviousProfile;
                if (Request.Action == ERoomAudioEdit::Associate)
                {
                    Item.SourceWeakPtr->AcousticRoom = Item.PreviousRoomSoftPtr;
                    Item.SourceWeakPtr->ResourceGroup = Item.PreviousGroup;
                }
                Item.SourceWeakPtr->PostEditChange();
                bRestored = IsLive(Item) && Item.SourceWeakPtr->Profile == Item.PreviousProfile
                            && (Request.Action != ERoomAudioEdit::Associate
                                || (Item.SourceWeakPtr->AcousticRoom == Item.PreviousRoomSoftPtr
                                    && Item.SourceWeakPtr->ResourceGroup == Item.PreviousGroup))
                            && bRestored;
            }
            else
            {
                bRestored = false;
            }
        }
        if (Item.bRoomWritten)
        {
            if (Item.RoomComponentWeakPtr.IsValid() && Item.RoomWeakPtr.IsValid()
                && Item.RoomWeakPtr->Room == Item.RoomComponentWeakPtr.Get()
                && Item.RoomComponentWeakPtr->AkAudioEvent == Item.Event.Get())
            {
                Item.RoomComponentWeakPtr->AkAudioEvent = Item.PreviousRoomEvent.Get();
                Item.RoomComponentWeakPtr->PostEditChange();
                bRestored
                    = Item.RoomComponentWeakPtr.IsValid()
                      && Item.RoomComponentWeakPtr->AkAudioEvent == Item.PreviousRoomEvent.Get()
                      && bRestored;
            }
            else
            {
                bRestored = false;
            }
        }
    }
    for (FItem& Item : Items)
    {
        if (!Item.CreatedRow.IsNone())
        {
            const bool bRemoved = !IsReferenced(Item.Table.Get(), Item.CreatedRow)
                                  && FWorldAudioProfileRepository::RemoveCreated(
                                      *Item.Table.Get(), Item.CreatedRow, Item.Row);
            bRestored = bRemoved && bRestored;
        }
    }
    return bRestored;
}
