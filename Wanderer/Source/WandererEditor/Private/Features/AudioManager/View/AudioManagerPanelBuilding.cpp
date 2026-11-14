#include "Features/AudioManager/View/AudioManagerPanel.h"

#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#include "AkAudioEvent.h"

#include "Features/AudioManager/Controller/AudioManagerController.h"

void SAudioManagerPanel::ShowBuildingDialog(TSharedRef<FAudioBuildingDraft> InDraft)
{
    TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(FText::FromString(TEXT("创建建筑共用底噪")))
        .ClientSize(FVector2D(560, 260))
        .SupportsMinimize(false)
        .SupportsMaximize(false);
    Window->SetContent(BuildBuildingDialogContents(InDraft, Window));
    FSlateApplication::Get()
        .AddModalWindow( Window, FSlateApplication::Get()
        .FindWidgetWindow(AsShared()));
    return;
}

TSharedRef<SWidget> SAudioManagerPanel::BuildBuildingDialogActions(
    TSharedRef<FAudioBuildingDraft> InDraft, TSharedRef<SWindow> InWindow)
{
    const TWeakPtr<SWindow> WindowWeakPtr = InWindow;
    const TWeakPtr<FAudioManagerController> ManagerWeakPtr = Controller;
    return SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("创建")))
                .OnClicked_Lambda(
                [InDraft, WindowWeakPtr, ManagerWeakPtr]
                {
                    const TSharedPtr<FAudioManagerController> Manager = ManagerWeakPtr.Pin();
                    if (!Manager || !Manager->ApplyBuildingDraft(InDraft))
                    {
                        return FReply::Handled();
                    }
                    if (TSharedPtr<SWindow> Dialog = WindowWeakPtr.Pin())
                    {
                        Dialog->RequestDestroyWindow();
                    }
                    return FReply::Handled();
                })
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(8, 0)
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("取消")))
                .OnClicked_Lambda(
                [WindowWeakPtr]
                {
                    if (TSharedPtr<SWindow> Dialog = WindowWeakPtr.Pin())
                    {
                        Dialog->RequestDestroyWindow();
                    }
                    return FReply::Handled();
                })
            ];
}

TSharedRef<SWidget> SAudioManagerPanel::BuildBuildingDialogContents(
    TSharedRef<FAudioBuildingDraft> InDraft, TSharedRef<SWindow> InWindow)
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8)
        [
            SNew(STextBlock)
            .AutoWrapText(true)
            .Text(FText::FromString(FString::Printf(TEXT(
            "%d 个成员 Room，共用一份底噪。默认不创建 Room 附加声，不复制建筑 Event；已有明确配置的 Room Event "
            "保留并叠加。重复建筑归属会拒绝创建。"),
            InDraft->RoomsWeakPtr.Num())))
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8)
        [
            SNew(SEditableTextBox)
            .HintText(FText::FromString(TEXT("建筑名称")))
            .Text(FText::FromString(InDraft->Name))
            .OnTextChanged_Lambda(
            [InDraft](const FText& Text)
            {
                InDraft->Name = Text.ToString()
                .TrimStartAndEnd();
            })
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8)
        [
            SNew(SObjectPropertyEntryBox)
            .AllowedClass(UAkAudioEvent::StaticClass())
            .ObjectPath_Lambda(
            [InDraft]
            {
                return InDraft->Event.ToString();
            })
            .OnObjectChanged_Lambda(
            [InDraft](const FAssetData& Asset)
            {
                InDraft->Event = Asset.GetSoftObjectPath();
            })
        ]
        + SVerticalBox::Slot()
        .FillHeight(1)
        .Padding(8)
        [
            SNew(STextBlock)
            .AutoWrapText(true)
            .Text_Lambda(
            [InDraft]
            {
                return InDraft->Error;
            })
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding( 8)
        [
        BuildBuildingDialogActions(InDraft, InWindow)
    ];
}

TSharedRef<SWidget> SAudioManagerPanel::BuildBuildingResourceMenu()
{
    const TArray<FName> Groups = Controller->GetBuildingResourceGroups();
    const TWeakPtr<SAudioManagerPanel> ManagerWeakPtr = SharedThis(this);
    const TWeakObjectPtr<UWorld> MenuWorldWeakPtr = Controller->GetObservedWorld();
    TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
    for (FName Group : Groups)
    {
        Menu->AddSlot()
            .AutoHeight()
            .Padding(3)
        [
            SNew(SButton)
                .Text(FText::FromString(
                Group.IsNone() ? TEXT("None（按成员 Room）") : Group.ToString()))
                .OnClicked_Lambda(
                [ManagerWeakPtr, MenuWorldWeakPtr, Group]
                {
                    if (const TSharedPtr<SAudioManagerPanel> Manager = ManagerWeakPtr.Pin();
                    Manager && MenuWorldWeakPtr.IsValid()
                    && Manager->Controller->GetObservedWorld() == MenuWorldWeakPtr.Get())
                    {
                        Manager->Controller->ApplyBuildingResourceGroup(Group);
                    }
                    return FReply::Handled();
                })
        ];
    }
    return SNew(SBox)
        .MinDesiredWidth(220)
        .MaxDesiredHeight( 400)
    [
        SNew(SScrollBox)
            + SScrollBox::Slot()
        [
            Menu
        ]
    ];
}

TSharedRef<SWidget> SAudioManagerPanel::BuildBuildingToolbar()
{
    return SNew(SHorizontalBox)
        .Visibility_Lambda(
        [this]
        {
            return Controller->GetState().ActivePage == 3 ? EVisibility::Visible
            : EVisibility::Collapsed;
        })
        .IsEnabled(this, &SAudioManagerPanel::OnCanEditRoomSources)
        + SHorizontalBox::Slot()
        .AutoWidth()
        .Padding(2)
    [
        SNew(SButton)
            .Text(FText::FromString(TEXT("从所选 Rooms 新建")))
            .OnClicked(this, &SAudioManagerPanel::OnCreateBuildingDialog)
    ]
        + SHorizontalBox::Slot()
        .AutoWidth()
        .Padding(2)
    [
        SNew(SButton)
            .Text(FText::FromString(TEXT("修改底噪 / 成员")))
            .OnClicked(this, &SAudioManagerPanel::OnEditBuildingSelection)
    ]
        + SHorizontalBox::Slot()
        .AutoWidth()
        .Padding(2)
    [
        SNew(SButton)
            .Text(FText::FromString(TEXT("删除所选建筑底噪")))
            .OnClicked(this, &SAudioManagerPanel::OnDeleteBuildingSelection)
    ]
        + SHorizontalBox::Slot()
        .AutoWidth()
        .Padding( 6, 2)
    [
        SNew(STextBlock)
            .Text(FText::FromString(TEXT("资源组")))
    ]
        + SHorizontalBox::Slot()
        .AutoWidth()
        .Padding(2)
    [
        SNew(SComboButton)
            .OnGetMenuContent(this, &SAudioManagerPanel::BuildBuildingResourceMenu)
            .ButtonContent()
        [
            SNew(STextBlock)
                .Text_Lambda(
                [this]
                {
                    return Controller->GetState().SelectedBuildingResourceGroup;
                })
        ]
    ]
        + SHorizontalBox::Slot()
        .FillWidth(1)
        .Padding(6)
    [
        SNew(STextBlock)
            .AutoWrapText(true)
            .Text(FText::FromString(TEXT(
            "一行一栋建筑。资源组控制准入，成员 Room 控制进入；None按成员触发。删除保留 Rooms / Portals。修改后保存地图。")))
    ];
}
