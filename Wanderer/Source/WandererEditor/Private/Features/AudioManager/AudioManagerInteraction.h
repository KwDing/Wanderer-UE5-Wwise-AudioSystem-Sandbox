#pragma once

#include "CoreMinimal.h"

#include "Features/AudioManager/AudioManagerTypes.h"

/** Synchronous editor-thread presentation boundary; no concrete Slate dependency. */
class IAudioManagerInteraction
{
public:
    virtual ~IAudioManagerInteraction() = default;
    virtual void RefreshTable(const TArray<FAudioManagerEntryPtr>& InSelection) = 0;
    virtual void SetDetailsObjects(const TArray<UObject*>& InObjects) = 0;
    virtual void RebuildColumns() = 0;
    virtual bool IsGroupPickerOpen() const = 0;
    virtual void RefreshGroupOptions() = 0;
    virtual void RefreshRoomOptions() = 0;
    virtual void ShowSourceDialog(TSharedRef<FAudioSourceDialogDraft> InDraft) = 0;
    virtual void ShowBuildingDialog(TSharedRef<FAudioBuildingDraft> InDraft) = 0;
    virtual bool ConfirmRoomDeletion(int32 InCount) = 0;
};
