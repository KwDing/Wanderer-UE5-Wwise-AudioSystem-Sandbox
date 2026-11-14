#include "UI/Characters/WandererCharacterSelectionWidget.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#include "Framework/CharacterSelectionModel.h"

TSharedRef<SWidget> UWandererCharacterSelectionWidget::RebuildWidget()
{
    SetIsFocusable(true);
    PortraitBrush.ImageSize = FVector2D(384.0f, 512.0f);
    PortraitBrush.DrawAs = ESlateBrushDrawType::Image;
    return SNew(SOverlay)
        + SOverlay::Slot()
        .HAlign(HAlign_Right)
        .VAlign(VAlign_Top)
        .Padding(24.0f)
        [
            BuildOpenButton()
        ]
        + SOverlay::Slot()
        [
            SNew(SBorder)
                .Visibility_UObject(this, &ThisClass::GetPanelVisibility)
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(FLinearColor(0.005f, 0.01f, 0.02f, 0.86f))
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)
                [
                    SNew(SScaleBox)
                        .Stretch(EStretch::ScaleToFit)
                        .StretchDirection(EStretchDirection::DownOnly)
                        [
                            BuildSelectionPanel()
                        ]
                ]
        ];
}

TSharedRef<SWidget> UWandererCharacterSelectionWidget::BuildOpenButton()
{
    return SNew(SButton)
        .Visibility_UObject(this, &ThisClass::GetButtonVisibility)
        .ContentPadding(FMargin(22.0f, 12.0f))
        .OnClicked(FOnClicked::CreateUObject(this, &ThisClass::OnOpenSelection))
        [
            SNew(STextBlock)
                .Text(FText::FromString(TEXT("Switch character")))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
        ];
}

TSharedRef<SWidget> UWandererCharacterSelectionWidget::BuildCharacterList()
{
    TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
    const UCharacterSelectionModel* Model = GetModel();
    const int32 Count = Model ? Model->GetOptionCount() : 0;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        List->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 10.0f)
            [
                BuildCharacterRow(Index)
            ];
    }
    return SNew(SScrollBox)
        + SScrollBox::Slot()
        [
            List
        ];
}

TSharedRef<SWidget> UWandererCharacterSelectionWidget::BuildCharacterRow(int32 InIndex)
{
    const UCharacterSelectionModel* Model = GetModel();
    return SNew(SButton)
        .ContentPadding(FMargin(18.0f, 16.0f))
        .IsEnabled(Model && Model->IsOptionEnabled(InIndex))
        .OnClicked(FOnClicked::CreateUObject(this, &ThisClass::OnSelectPreview, InIndex))
        [
            SNew(STextBlock)
                .Text_UObject(this, &ThisClass::GetRowText, InIndex)
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 18))
        ];
}

TSharedRef<SWidget> UWandererCharacterSelectionWidget::BuildSelectionPanel()
{
    return SNew(SBox)
        .WidthOverride(940.0f)
        .HeightOverride(660.0f)
        [
            SNew(SBorder)
                .Padding(28.0f)
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(FLinearColor(0.025f, 0.035f, 0.05f))
                [
                    SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                        [
                            SNew(STextBlock)
                                .Text(FText::FromString(TEXT("Choose your character")))
                                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 28))
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 0.0f, 0.0f, 24.0f)
                        [
                            SNew(STextBlock)
                                .Text(FText::FromString(TEXT("Select a name to preview. Your game is paused.")))
                                .ColorAndOpacity(FLinearColor(0.65f, 0.72f, 0.8f))
                        ]
                        + SVerticalBox::Slot()
                        .FillHeight(1.0f)
                        [
                            BuildSelectionContent()
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 16.0f, 0.0f, 0.0f)
                        [
                            BuildSelectionFooter()
                        ]
                ]
        ];
}

TSharedRef<SWidget> UWandererCharacterSelectionWidget::BuildPreviewPanel()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .FillHeight(1.0f)
        .HAlign(HAlign_Center)
        [
            SNew(SBox)
                .WidthOverride(330.0f)
                .HeightOverride(440.0f)
                [
                    SNew(SImage)
                        .Image(&PortraitBrush)
                ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .HAlign(HAlign_Center)
        .Padding(0.0f, 8.0f)
        [
            SNew(STextBlock)
                .Text_UObject(this, &ThisClass::GetSelectedName)
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 20))
        ];
}

TSharedRef<SWidget> UWandererCharacterSelectionWidget::BuildSelectionContent()
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .AutoWidth()
        .Padding(0.0f, 0.0f, 24.0f, 0.0f)
        [
            SNew(SBox)
                .WidthOverride(240.0f)
                [
                    BuildCharacterList()
                ]
        ]
        + SHorizontalBox::Slot()
        .FillWidth(1.0f)
        .HAlign(HAlign_Center)
        [
            BuildPreviewPanel()
        ];
}

TSharedRef<SWidget> UWandererCharacterSelectionWidget::BuildSelectionFooter()
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .FillWidth(1.0f)
        .VAlign(VAlign_Center)
        [
            SNew(STextBlock)
                .Text_UObject(this, &ThisClass::GetStatusText)
                .AutoWrapText(true)
        ]
        + SHorizontalBox::Slot()
        .AutoWidth()
        .Padding(12.0f, 0.0f)
        [
            SNew(SButton)
                .ContentPadding(FMargin(20.0f, 12.0f))
                .OnClicked(FOnClicked::CreateUObject(this, &ThisClass::OnCancelSelection))
                [
                    SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Cancel")))
                ]
        ]
        + SHorizontalBox::Slot()
        .AutoWidth()
        [
            SNew(SButton)
                .ContentPadding(FMargin(32.0f, 12.0f))
                .ButtonColorAndOpacity(FLinearColor(0.25f, 0.7f, 0.58f))
                .IsEnabled_UObject(this, &ThisClass::CanChoose)
                .OnClicked(FOnClicked::CreateUObject(this, &ThisClass::OnConfirmSelection))
                [
                    SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Choose")))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
                ]
        ];
}
