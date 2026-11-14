#include "UI/Widgets/RadioItemWidget.h"

#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SRadioItemWidget::Construct(const FArguments& InArgs)
{
    ChildSlot
    [
        SNew(SBox)
        .WidthOverride(145)
        [
            SNew(SButton)
            .IsFocusable(false)
            .ContentPadding(8)
            .ButtonColorAndOpacity(FLinearColor(0.12f, 0.16f, 0.22f, 0.28f))
            .IsEnabled(InArgs._Available)
            .OnClicked(InArgs._OnDeploy)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                .HAlign(HAlign_Center)
                [
                    SNew(SBox)
                    .WidthOverride(120)
                    .HeightOverride(120)
                    [
                        SNew(SImage)
                        .Image(InArgs._Image)
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0, 5)
                [
                    SNew(STextBlock)
                    .Text(InArgs._Label)
                    .Justification(ETextJustify::Center)
                ]
            ]
        ]
    ];
}
