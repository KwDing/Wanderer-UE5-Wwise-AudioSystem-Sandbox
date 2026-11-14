#include "Features/AudioManager/Controller/AudioManagerController.h"

#include "AkSpatialAudioVolume.h"

#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Features/AudioManager/Authoring/RoomAudioEditOperation.h"

void FAudioManagerController::EditRoomSources()
{
    if (!CanEditRoomSources())
    {
        return;
    }
    TArray<UObject*> Objects;
    for (UWandererAudioSourceComponent* Source : GetSelectedRoomSources())
    {
        Objects.Add(Source);
    }
    SetDetailsObjects(Objects);
    State.LastRoomAction = FString::Printf(
        TEXT("正在批量编辑 %d 个关联底声：Profile、启用、分类、资源组；不同值显示为多个值。"),
        Objects.Num());
    UpdateSummary();
    return;
}

void FAudioManagerController::SelectVisibleRooms()
{
    if (!CanEditRoomSources())
    {
        return;
    }
    SelectedEntries.Reset();
    TArray<FEntryPtr> Rooms;
    for (const FEntryPtr& Entry : State.Entries)
    {
        if (Entry->ActorWeakPtr.IsValid() && Entry->ActorWeakPtr->IsA<AAkSpatialAudioVolume>())
        {
            Rooms.Add(Entry);
        }
    }
    SelectedEntries = Rooms;
    RefreshTable();
    UpdateSelectionDetails();
    return FinishRoomAction(
        FString::Printf(TEXT("已选择当前筛选结果中的 %d 个 Room。"), Rooms.Num()));
}

void FAudioManagerController::CreateRoomSources()
{
    ApplyRoomEdit(ERoomAudioEdit::Create);
}

void FAudioManagerController::ApplyRoomProfile()
{
    ApplyRoomEdit(ERoomAudioEdit::Profile);
}

void FAudioManagerController::ApplyRoomEvent()
{
    ApplyRoomEdit(ERoomAudioEdit::Events);
}

void FAudioManagerController::ApplyRoomSelection()
{
    if (!CanEditRoomSources())
    {
        return;
    }
    if (State.RoomEventPath.IsNull())
    {
        DeleteRoomSources(false);
        return;
    }
    ApplyRoomEdit(ERoomAudioEdit::CreateOrApply);
}

void FAudioManagerController::AssociateRoomSources()
{
    ApplyRoomEdit(ERoomAudioEdit::Associate);
}

void FAudioManagerController::ApplyRoomEdit(ERoomAudioEdit InAction)
{
    if (!CanEditRoomSources())
    {
        return;
    }
    FRoomAudioEditRequest Request;
    Request.Action = InAction;
    Request.WorldWeakPtr = GetObservedWorld();
    Request.TableSoftRef = State.RoomProfileTablePath;
    Request.EventSoftRef = State.RoomEventPath;
    Request.NightEventSoftRef = State.RoomNightEventPath;
    const bool bHasProfile = GetRoomProfile(Request.Profile);
    if (!bHasProfile && (InAction == ERoomAudioEdit::Create || InAction == ERoomAudioEdit::Profile))
    {
        return FinishRoomAction(TEXT("请选择有效的 Room 模式配置行。"));
    }
    for (AAkSpatialAudioVolume* Room : GetSelectedRooms())
    {
        Request.RoomsWeakPtr.Add(Room);
    }
    for (UWandererAudioSourceComponent* Source : GetSelectedRoomSources())
    {
        Request.SourcesWeakPtr.Add(Source);
    }
    const TWeakPtr<FAudioManagerController> ControllerWeakPtr = AsShared();
    const TWeakObjectPtr<UWorld> WorldWeakPtr = Request.WorldWeakPtr;
    Request.CanContinue = [ControllerWeakPtr, WorldWeakPtr]()
    {
        const TSharedPtr<FAudioManagerController> Controller = ControllerWeakPtr.Pin();
        return Controller && Controller->CanEditRoomSources()
               && Controller->GetObservedWorld() == WorldWeakPtr.Get();
    };
    const FAudioSourceResult Result = FRoomAudioEditOperation::Execute(Request);
    if (bStopped)
    {
        return;
    }
    if (Result.Applied > 0 && InAction == ERoomAudioEdit::CreateOrApply)
    {
        for (UWandererAudioSourceComponent* Source : GetSelectedRoomSources())
        {
            if (!Source->Profile.IsNull()
                && FSoftObjectPath(Source->Profile.DataTable.Get()) == State.RoomProfileTablePath)
            {
                State.RoomProfileRow = Source->Profile.RowName;
                break;
            }
        }
    }
    RefreshRoomProfiles();
    RefreshSelectedRoomEvent();
    FinishRoomAction(Result.Summary());
}

void FAudioManagerController::FinishRoomAction(const FString& InResult)
{
    if (bStopped)
    {
        return;
    }
    State.LastRoomAction = InResult;
    Refresh();
    return;
}
