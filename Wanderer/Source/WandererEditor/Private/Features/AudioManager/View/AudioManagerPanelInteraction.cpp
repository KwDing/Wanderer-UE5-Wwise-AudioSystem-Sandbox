#include "Features/AudioManager/View/AudioManagerPanel.h"

#include "IDetailsView.h"
#include "Misc/MessageDialog.h"
#include "Widgets/Input/SComboBox.h"

void SAudioManagerPanel::RefreshTable(const TArray<FEntryPtr>& InSelection)
{
    if (!List)
    {
        return;
    }
    List->ClearSelection();
    List->RequestListRefresh();
    for (const FEntryPtr& Entry : InSelection)
    {
        List->SetItemSelection(Entry, true);
    }
}

void SAudioManagerPanel::SetDetailsObjects(const TArray<UObject*>& InObjects)
{
    if (Details)
    {
        Details->SetObjects(InObjects);
    }
}

bool SAudioManagerPanel::IsGroupPickerOpen() const
{
    return GroupPicker && GroupPicker->IsOpen();
}

void SAudioManagerPanel::RefreshGroupOptions()
{
    if (GroupPicker)
    {
        GroupPicker->RefreshOptions();
    }
}

void SAudioManagerPanel::RefreshRoomOptions()
{
    if (RoomProfilePicker)
    {
        RoomProfilePicker->RefreshOptions();
    }
}

bool SAudioManagerPanel::ConfirmRoomDeletion(int32 InCount)
{
    return FMessageDialog::Open(EAppMsgType::YesNo,
        FText::FromString(FString::Printf(TEXT("删除所选关联的 %d 个底声配置？Room 本体、Portal、DT 和音频资产会保留。"
        "可撤销，不会自动保存地图。"),
        InCount)))
    == EAppReturnType::Yes;
}
