#include "Features/AudioManager/Authoring/RoomAudioEditOperation.h"

#include "Editor.h"
#include "ScopedTransaction.h"

#include "AkAudioEvent.h"
#include "AkRoomComponent.h"
#include "AkSpatialAudioVolume.h"

#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Features/AudioManager/Authoring/WorldAudioProfileRepository.h"

FAudioSourceResult FRoomAudioEditOperation::Execute(const FRoomAudioEditRequest& InRequest)
{
    FAudioSourceResult Result;
    static bool bExecuting = false;
    if (!IsInGameThread() || !GEditor || GIsTransacting || GEditor->IsTransactionActive()
        || bExecuting)
    {
        Result.Errors.Add(TEXT("未修改：已有事务、重入或非编辑主线程。"));
        return Result;
    }
    TGuardValue<bool> Guard(bExecuting, true);
    FRoomAudioEditOperation Operation(InRequest);
    if (!Operation.Build(Result))
    {
        Result.Errors.Add(TEXT("未修改：没有有效目标，或 Room/Profile/Event/编辑上下文预检失败。"));
        return Result;
    }
    FScopedTransaction Transaction(NSLOCTEXT("WorldAudio", "RoomEdit", "Edit Room ambience"));
    bool bSuccess = true;
    for (FItem& Item : Operation.Items)
    {
        if (!Operation.Commit(Item))
        {
            bSuccess = false;
            break;
        }
    }
    bSuccess = bSuccess && Operation.CanContinue();
    for (const FItem& Item : Operation.Items)
    {
        bSuccess = bSuccess && Operation.Matches(Item);
    }
    if (!bSuccess)
    {
        const bool bRestored = Operation.Rollback();
        Result.Errors.Add(
            bRestored ? TEXT("提交中止，本批修改已补偿；此前 Undo 保留。")
                      : TEXT("提交中止，外部冲突使补偿不完整；保留本批 Undo，请检查场景。"));
        if (bRestored)
        {
            Transaction.Cancel();
        }
        return Result;
    }
    Result.Applied = Operation.Items.Num();
    return Result;
}

FRoomAudioEditOperation::FRoomAudioEditOperation(const FRoomAudioEditRequest& InRequest)
    : Request(InRequest), ProfileTable(const_cast<UDataTable*>(InRequest.Profile.DataTable.Get()))
{
}

bool FRoomAudioEditOperation::IsUnchanged(const FItem& InItem) const
{
    return InItem.bCreate
           || (InItem.SourceWeakPtr.IsValid()
               && InItem.SourceWeakPtr->Profile == InItem.PreviousProfile
               && InItem.SourceWeakPtr->AcousticRoom == InItem.PreviousRoomSoftPtr
               && InItem.SourceWeakPtr->ResourceGroup == InItem.PreviousGroup);
}

bool FRoomAudioEditOperation::Matches(const FItem& InItem) const
{
    if (!IsLive(InItem) || !InItem.bWritten || !MatchesRoomGroup(InItem))
    {
        return false;
    }
    const UWandererAudioSourceComponent* Source = InItem.SourceWeakPtr.Get();
    if (Source->Profile != InItem.WrittenProfile
        || !FWorldAudioProfileRepository::Matches(*InItem.Table.Get(),
                                                  InItem.WrittenProfile.RowName, InItem.Row))
    {
        return false;
    }
    if ((InItem.bCreate || Request.Action == ERoomAudioEdit::Associate)
        && (Source->AcousticRoom.Get() != InItem.RoomWeakPtr.Get()
            || Source->ResourceGroup != InItem.Group))
    {
        return false;
    }
    return !InItem.bRoomWritten || InItem.RoomComponentWeakPtr->AkAudioEvent == InItem.Event.Get();
}

bool FRoomAudioEditOperation::Commit(FItem& InOutItem)
{
    if (!CanContinue() || !IsLive(InOutItem) || !IsUnchanged(InOutItem)
        || !MatchesRoomGroup(InOutItem))
    {
        return false;
    }
    if (Event.IsValid())
    {
        InOutItem.WrittenProfile = FWorldAudioProfileRepository::FindOrAdd(
            InOutItem.Table.Get(), InOutItem.Row, Event.Get(), NAME_None, InOutItem.CreatedRow);
        if (InOutItem.WrittenProfile.IsNull() || !CanContinue() || !IsLive(InOutItem))
        {
            return false;
        }
    }
    InOutItem.OwnerWeakPtr->Modify();
    if (!CanContinue() || !IsLive(InOutItem) || !IsUnchanged(InOutItem)
        || !MatchesRoomGroup(InOutItem))
    {
        return false;
    }
    if (InOutItem.bCreate)
    {
        if (InOutItem.OwnerWeakPtr->FindComponentByClass<UWandererAudioSourceComponent>())
        {
            return false;
        }
        UWandererAudioSourceComponent* Source = NewObject<UWandererAudioSourceComponent>(
            InOutItem.OwnerWeakPtr.Get(), NAME_None, RF_Transactional);
        InOutItem.SourceWeakPtr = Source;
        InOutItem.bCreated = true;
        InOutItem.OwnerWeakPtr->AddInstanceComponent(Source);
        Source->Area = InOutItem.Group.IsNone() ? FName(TEXT("Interior")) : InOutItem.Group;
        Source->OnComponentCreated();
        if (!CanContinue() || !IsLive(InOutItem))
        {
            return false;
        }
        Source->RegisterComponent();
    }
    if (!CanContinue() || !IsLive(InOutItem))
    {
        return false;
    }
    InOutItem.SourceWeakPtr->SetFlags(RF_Transactional);
    InOutItem.SourceWeakPtr->Modify();
    if (!CanContinue() || !IsLive(InOutItem) || !IsUnchanged(InOutItem)
        || !MatchesRoomGroup(InOutItem))
    {
        return false;
    }
    InOutItem.bWritten = true;
    InOutItem.SourceWeakPtr->Profile = InOutItem.WrittenProfile;
    if (InOutItem.bCreate || Request.Action == ERoomAudioEdit::Associate)
    {
        InOutItem.SourceWeakPtr->AcousticRoom = InOutItem.RoomWeakPtr.Get();
        InOutItem.SourceWeakPtr->ResourceGroup = InOutItem.Group;
    }
    if (!PrepareRoom(InOutItem))
    {
        return false;
    }
    InOutItem.SourceWeakPtr->PostEditChange();
    return CanContinue() && Matches(InOutItem);
}

bool FRoomAudioEditOperation::PrepareRoom(FItem& InOutItem)
{
    if (InOutItem.RoomComponentWeakPtr->AkAudioEvent != InOutItem.PreviousRoomEvent.Get())
    {
        // Several selected layers may share a Room; accept metadata written by this batch only.
        return Items.ContainsByPredicate([&InOutItem](const FItem& InOther)
        {
            return InOther.bRoomWritten
                && InOther.RoomComponentWeakPtr == InOutItem.RoomComponentWeakPtr
                && InOutItem.RoomComponentWeakPtr->AkAudioEvent == InOther.Event.Get();
        });
    }
    if (!InOutItem.RoomComponentWeakPtr->AkAudioEvent)
    {
        InOutItem.RoomComponentWeakPtr->SetFlags(RF_Transactional);
        InOutItem.RoomComponentWeakPtr->Modify();
        if (!CanContinue() || !IsLive(InOutItem) || InOutItem.RoomComponentWeakPtr->AkAudioEvent)
        {
            return false;
        }
        // Wwise KeepRegistered needs Event metadata even while AutoPost remains disabled.
        InOutItem.bRoomWritten = true;
        InOutItem.RoomComponentWeakPtr->AkAudioEvent = InOutItem.Event.Get();
        InOutItem.RoomComponentWeakPtr->PostEditChange();
    }
    if (!CanContinue() || !IsLive(InOutItem))
    {
        return false;
    }
    return true;
}
