#include "Features/GameLog/GameLogToolbarWidget.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#include "Features/GameLog/GameLogModel.h"

void SGameLogToolbarWidget::Construct(const FArguments& InArguments)
{
    Model = InArguments._Model;
    ChildSlot
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
            .AutoHeight()
        [
            SNew(SWrapBox)
                .UseAllottedSize(true)
            + SWrapBox::Slot()
            [
                MakePresetButton(TEXT("Problems"), 0)
            ]
            + SWrapBox::Slot()
            [
                MakePresetButton(TEXT("Playback"), 1)
            ]
            + SWrapBox::Slot()
            [
                MakePresetButton(TEXT("Spatial + playback"), 2)
            ]
            + SWrapBox::Slot()
            [
                MakePresetButton(TEXT("Event bus"), 3)
            ]
            + SWrapBox::Slot()
            [
                MakePresetButton(TEXT("All (timed)"), 4)
            ]
        ]
        + SVerticalBox::Slot()
            .AutoHeight()
        [
            MakeTools()
        ]
    ];
}

TSharedRef<SWidget> SGameLogToolbarWidget::MakePresetButton(const TCHAR* InLabel, int32 InPreset)
{
    return SNew(SButton)
        .Text(FText::FromString(InLabel))
        .OnClicked_Lambda([Model = Model, InPreset]
            {
                Model->SetPreset(InPreset);
                return FReply::Handled();
            });
}

TSharedRef<SWidget> SGameLogToolbarWidget::MakeTools()
{
    return SNew(SWrapBox)
        .UseAllottedSize(true)
        + SWrapBox::Slot()
        [
            SNew(SCheckBox)
                .IsChecked_Lambda([Model = Model]
                    {
                        return Model->IsFootstepsEnabled()
                            ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                    })
                .OnCheckStateChanged_Lambda([Model = Model](ECheckBoxState InState)
                    {
                        Model->SetFootstepsEnabled(
                            InState == ECheckBoxState::Checked);
                    })
            [
                SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Footsteps (sampled)")))
            ]
        ]
        + SWrapBox::Slot()
        [
            SNew(SEditableTextBox)
                .MinDesiredWidth(240)
                .HintText(FText::FromString(TEXT("Capture source path filter (Enter)")))
                .Text(FText::FromString(Model->GetFocus()))
                .OnTextCommitted_Lambda([Model = Model](const FText& InText, ETextCommit::Type)
                    {
                        Model->SetFocus(InText.ToString());
                    })
        ]
        + SWrapBox::Slot()
        [
            SNew(SButton)
                .Text(FText::FromString(TEXT("Spatial snapshot")))
                .OnClicked_Lambda([Model = Model]
                    {
                        Model->CaptureSpace();
                        return FReply::Handled();
                    })
        ]
        + SWrapBox::Slot()
        [
            SNew(SButton)
                .Text(FText::FromString(TEXT("Clear")))
                .OnClicked_Lambda([Model = Model]
                    {
                        Model->Clear();
                        return FReply::Handled();
                    })
        ];
}
