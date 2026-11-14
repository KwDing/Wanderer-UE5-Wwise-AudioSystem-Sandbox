#include "Features/AudioManager/View/AudioManagerPanel.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"

#include "Features/AudioManager/Controller/AudioManagerController.h"

TSharedRef<SWidget> SAudioManagerPanel::BuildColumnActions(int32 InPage)
{
    const TWeakPtr<SAudioManagerPanel> ManagerWeakPtr = SharedThis(this);
    return SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
            [
                SNew( SButton)
                    .Text(FText::FromString(TEXT("常用列")))
                    .OnClicked_Lambda(
                    [ManagerWeakPtr, InPage]
                    {
                        if (const TSharedPtr<SAudioManagerPanel> Manager = ManagerWeakPtr.Pin())
                        {
                            Manager->Controller->ResetColumnVisibility( InPage);
                            Manager->Controller->SaveColumnVisibility();
                        }
                        return FReply::Handled();
                    })
            ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding( 6, 0)
            [
                SNew(SButton)
                    .Text(FText::FromString(TEXT("全部显示")))
                    .OnClicked_Lambda(
                    [ManagerWeakPtr, InPage]
                    {
                        if (const TSharedPtr<SAudioManagerPanel> Manager = ManagerWeakPtr.Pin())
                        {
                            Manager->Controller->ShowAllColumns( InPage);
                            Manager->Controller->SaveColumnVisibility();
                        }
                        return FReply::Handled();
                    })
            ];
}

TSharedRef<SWidget> SAudioManagerPanel::BuildColumnChecks(int32 InPage)
{
    const TWeakPtr<SAudioManagerPanel> ManagerWeakPtr = SharedThis(this);
    TSharedRef<SVerticalBox> Checks = SNew(SVerticalBox);
    for (const auto& Column : FAudioManagerController::GetColumnDefinitions(InPage))
    {
        const FName Key = Column.Key;
        Checks->AddSlot()
            .AutoHeight()
            .Padding(4)
        [
            SNew(SCheckBox)
                .IsEnabled_Lambda(
                [ManagerWeakPtr, Key]
                {
                    return Key != "Name" && ManagerWeakPtr.IsValid();
                })
                .IsChecked_Lambda(
                [ManagerWeakPtr, InPage, Key]
                {
                    const TSharedPtr<SAudioManagerPanel> Manager = ManagerWeakPtr.Pin();
                    return Manager && Manager->Controller->IsColumnVisible(InPage, Key)
                    ? ECheckBoxState::Checked
                    : ECheckBoxState::Unchecked;
                })
                .OnCheckStateChanged_Lambda(
                [ManagerWeakPtr, InPage, Key](ECheckBoxState State)
                {
                    if (const TSharedPtr<SAudioManagerPanel> Manager = ManagerWeakPtr.Pin())
                    {
                        Manager->Controller->SetColumnVisibility(
                        InPage, Key, State == ECheckBoxState::Checked);
                        Manager->Controller->SaveColumnVisibility();
                    }
                })
            [
                SNew(STextBlock)
                    .Text(FText::FromString(
                    Column.Label + (Key == "Name" ? TEXT("（固定）") : TEXT(""))))
            ]
        ];
    }
    return Checks;
}

TSharedRef<SWidget> SAudioManagerPanel::BuildColumnSettings()
{
    const int32 Page = Controller->GetState().ActivePage;
    TSharedRef<SWidget> Checks = BuildColumnChecks(Page);
    return SNew(SBox)
        .WidthOverride(270)
        .MaxDesiredHeight( 500)
    [
        SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding( 6)
        [
            SNew(STextBlock)
                .Text(FText::FromString(TEXT("显示列 · 本机偏好")))
        ]
            + SVerticalBox::Slot()
            .FillHeight(1)
        [
            SNew(SScrollBox)
                + SScrollBox::Slot()
            [
                Checks
            ]
        ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(4)
        [
        BuildColumnActions(Page)
    ]
    ];
}
