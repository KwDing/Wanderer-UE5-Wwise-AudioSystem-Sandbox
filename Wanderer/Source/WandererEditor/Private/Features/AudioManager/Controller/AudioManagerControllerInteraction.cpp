#include "Features/AudioManager/Controller/AudioManagerController.h"

#include "Features/AudioManager/AudioManagerInteraction.h"

bool FAudioManagerController::ConfirmRoomDeletion(int32 InCount)
{
    if (const TSharedPtr<IAudioManagerInteraction> View = InteractionWeakPtr.Pin())
    {
        return View->ConfirmRoomDeletion(InCount);
    }
    return false;
}

bool FAudioManagerController::IsGroupPickerOpen() const
{
    if (const TSharedPtr<IAudioManagerInteraction> View = InteractionWeakPtr.Pin())
    {
        return View->IsGroupPickerOpen();
    }
    return false;
}

void FAudioManagerController::RebuildColumns()
{
    if (const TSharedPtr<IAudioManagerInteraction> View = InteractionWeakPtr.Pin())
    {
        View->RebuildColumns();
    }
}

void FAudioManagerController::RefreshGroupOptions()
{
    if (const TSharedPtr<IAudioManagerInteraction> View = InteractionWeakPtr.Pin())
    {
        View->RefreshGroupOptions();
    }
}

void FAudioManagerController::RefreshRoomOptions()
{
    if (const TSharedPtr<IAudioManagerInteraction> View = InteractionWeakPtr.Pin())
    {
        View->RefreshRoomOptions();
    }
}

void FAudioManagerController::RefreshTable()
{
    if (bStopped)
    {
        return;
    }
    TGuardValue<bool> Guard(bRefreshing, true);
    const TArray<FEntryPtr> Selection = SelectedEntries;
    if (const TSharedPtr<IAudioManagerInteraction> View = InteractionWeakPtr.Pin())
    {
        View->RefreshTable(Selection);
    }
}

void FAudioManagerController::SetDetailsObjects(const TArray<UObject*>& InObjects)
{
    if (const TSharedPtr<IAudioManagerInteraction> View = InteractionWeakPtr.Pin())
    {
        View->SetDetailsObjects(InObjects);
    }
}
