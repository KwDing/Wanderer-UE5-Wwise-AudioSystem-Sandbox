#include "Features/AudioManager/View/AudioManagerPanel.h"

#include "Engine/DataTable.h"
#include "IDetailsView.h"
#include "Misc/MessageDialog.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#include "AkAudioEvent.h"

#include "Features/AudioManager/AudioSourceAuthoringOperation.h"
#include "Features/AudioManager/Controller/AudioManagerController.h"

TSharedRef<SWidget> SAudioManagerPanel::BuildRoomEventControls()
{
    TSharedRef<SWrapBox> Main = SNew(SWrapBox)
        .UseAllottedSize(true);
    Main->AddSlot()
        .Padding( 2)
    [
        SNew(STextBlock)
            .Text(FText::FromString(TEXT("Room Play Event（空=无附加声）")))
    ];
    Main->AddSlot()
        .Padding(2)
    [
        SNew(SObjectPropertyEntryBox)
            .AllowedClass(UAkAudioEvent::StaticClass())
            .ToolTipText_Lambda(
            [this]
            {
                return Controller->GetState().SelectedRoomEvent;
            })
            .ObjectPath_Lambda(
            [this]
            {
                return Controller->GetState().RoomEventPath.ToString();
            })
            .OnObjectChanged_Lambda(
            [this](const FAssetData& Asset)
            {
                Controller->SetRoomEvent(Asset.GetSoftObjectPath());
            })
    ];
    Main->AddSlot()
        .Padding( 2)
    [
        SNew(STextBlock)
            .Text(FText::FromString(TEXT("Night Event（空=沿用 Play）")))
    ];
    Main->AddSlot()
        .Padding(2)
    [
        SNew(SObjectPropertyEntryBox)
            .AllowedClass(UAkAudioEvent::StaticClass())
            .ObjectPath_Lambda(
            [this]
            {
                return Controller->GetState().RoomNightEventPath.ToString();
            })
            .OnObjectChanged_Lambda(
            [this](const FAssetData& Asset)
            {
                Controller->SetRoomNightEvent(Asset.GetSoftObjectPath());
            })
    ];
    Main->AddSlot()
        .Padding(2)
    [
        SNew(SButton)
            .Text(FText::FromString(TEXT("Room + Event / 应用")))
            .ToolTipText(FText::FromString(TEXT(
            "明确配置时与建筑层叠加；Play Event 清空后应用只移除 Room 自身声源，不修改建筑底噪。")))
            .OnClicked(this, &SAudioManagerPanel::OnApplyRoomSelection)
    ];
    Main->AddSlot()
        .Padding(2)
    [
        SNew(SButton)
            .Text(FText::FromString(TEXT("所选 Rooms 共用建筑底噪")))
            .OnClicked(this, &SAudioManagerPanel::OnCreateBuildingDialog)
    ];
    return Main;
}

TSharedRef<SWidget> SAudioManagerPanel::BuildRoomProfileControls()
{
    TSharedRef<SWrapBox> Bar = SNew(SWrapBox)
        .UseAllottedSize(true)
        .IsEnabled( this, &SAudioManagerPanel::OnCanEditRoomSources);
    Bar->AddSlot()
        .Padding(2)
    [
        SNew(SButton)
            .Text(FText::FromString(TEXT("全选筛选结果中的 Room")))
            .OnClicked(this, &SAudioManagerPanel::OnSelectVisibleRooms)
    ];
    Bar->AddSlot()
        .Padding(2)
    [
        SNew(SObjectPropertyEntryBox)
            .AllowedClass(UDataTable::StaticClass())
            .ObjectPath_Lambda(
            [this]
            {
                return Controller->GetState().RoomProfileTablePath.ToString();
            })
            .OnObjectChanged_Lambda(
            [this](const FAssetData& Asset)
            {
                Controller->SetRoomProfileTable(Asset.GetSoftObjectPath());
            })
    ];
    Bar->AddSlot()
        .Padding(2)
    [
        BuildRoomProfilePicker()
    ];
    Bar->AddSlot()
        .Padding(2)
    [
        SNew(SButton)
            .Text(FText::FromString(TEXT("补建缺失底声")))
            .OnClicked(this, &SAudioManagerPanel::OnCreateRoomSources)
    ];
    Bar->AddSlot()
        .Padding(2)
    [
        SNew(SButton)
            .Text(FText::FromString(TEXT("应用配置到所选底声")))
            .OnClicked(this, &SAudioManagerPanel::OnApplyRoomProfile)
    ];
    Bar->AddSlot()
        .Padding(2)
    [
        SNew(SButton)
            .Text(FText::FromString(TEXT("批量编辑关联底声")))
            .OnClicked(this, &SAudioManagerPanel::OnEditRoomSources)
    ];
    Bar->AddSlot()
        .Padding(2)
    [
        SNew(SButton)
            .Text(FText::FromString(TEXT("自动关联未关联底声")))
            .OnClicked(this, &SAudioManagerPanel::OnAssociateRoomSources)
    ];
    Bar->AddSlot()
        .Padding(2)
    [
        SNew(SButton)
            .Text(FText::FromString(TEXT("删除所选关联底声")))
            .OnClicked(this, &SAudioManagerPanel::OnDeleteRoomSources, true)
    ];
    return Bar;
}

TSharedRef<SWidget> SAudioManagerPanel::BuildRoomProfilePicker()
{
    return SAssignNew(RoomProfilePicker, SComboBox<TSharedPtr<FString>>)
            .OptionsSource(&Controller->GetState().RoomProfileOptions)
            .OnComboBoxOpening(this, &SAudioManagerPanel::OnRefreshRoomProfiles)
            .OnGenerateWidget_Lambda(
            [](TSharedPtr<FString> Item)
            {
                return SNew(STextBlock)
                .Text(FText::FromString(*Item));
            })
            .OnSelectionChanged_Lambda(
            [this](TSharedPtr<FString> Item, ESelectInfo::Type)
            {
                if (Item)
                {
                    Controller->SetRoomProfileRow(FName(**Item));
                }
            })
        [
            SNew(STextBlock)
                .Text_Lambda(
                [this]
                {
                    return FText::FromString( Controller->GetState().RoomProfileRow.IsNone()
                    ? TEXT("请选择有效 Room 配置行")
                    : Controller->GetState().RoomProfileRow.ToString());
                })
        ];
}

TSharedRef<SWidget> SAudioManagerPanel::BuildRoomToolbar()
{
    Controller->RefreshRoomProfiles();
    return SNew(SVerticalBox)
        .Visibility_Lambda(
        [this]
        {
            return Controller->GetState().ActivePage == 0
            && Controller->GetState().bShowRoomActions
            && !Controller->GetSelectedRooms()
            .IsEmpty()
            ? EVisibility::Visible
            : EVisibility::Collapsed;
        })
        .IsEnabled(this, &SAudioManagerPanel::OnCanEditRoomSources)
        + SVerticalBox::Slot()
        .AutoHeight()
    [
        BuildRoomEventControls()
    ]
        + SVerticalBox::Slot()
        .AutoHeight()
    [
        SNew(SExpandableArea)
            .InitiallyCollapsed(true)
            .HeaderContent()
        [
            SNew(STextBlock)
                .Text(FText::FromString(TEXT("高级：配置、批量编辑、关联与删除")))
        ]
            .BodyContent()
        [
            BuildRoomProfileControls()
        ]
    ];
}
