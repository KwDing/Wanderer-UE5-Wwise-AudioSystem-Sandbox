#include "Features/GameLog/GameLogPanel.h"

#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#include "Features/GameLog/GameLogModel.h"
#include "Features/GameLog/GameLogToolbarWidget.h"

void SGameLogPanel::Construct(const FArguments& InArguments)
{
    Model = InArguments._Model;
    ChildSlot
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(6)
        [
            SNew(SGameLogToolbarWidget)
                .Model(Model)
        ]
        + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(6)
        [
            SAssignNew(StatusText, STextBlock)
                .AutoWrapText(true)
        ]
        + SVerticalBox::Slot()
            .FillHeight(1)
            .Padding(6)
        [
            SAssignNew(LogText, SMultiLineEditableTextBox)
                .IsReadOnly(true)
                .AutoWrapText(true)
        ]
    ];
}

void SGameLogPanel::Tick(const FGeometry& InGeometry, double InTime, float InDelta)
{
    constexpr double RefreshIntervalSec = 0.2;
    SCompoundWidget::Tick(InGeometry, InTime, InDelta);
    if (InTime < NextRefresh || !Model)
    {
        return;
    }
    NextRefresh = InTime + RefreshIntervalSec;
    Model->Refresh();
    StatusText->SetText(FText::FromString(Model->GetStatus()));
    const FString& Text = Model->GetText();
    if (Text != DisplayedText)
    {
        DisplayedText = Text;
        LogText->SetText(FText::FromString(Text));
        LogText->GoTo(ETextLocation::EndOfDocument);
    }
}
