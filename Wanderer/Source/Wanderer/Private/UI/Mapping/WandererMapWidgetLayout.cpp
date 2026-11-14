#include "UI/Mapping/WandererMapWidget.h"

#include "Engine/World.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#include "Framework/WandererGameState.h"
#include "Framework/WandererPlayerController.h"
#include "UI/Mapping/MapArtworkUtils.h"
#include "UI/Mapping/MapCanvasWidget.h"
#include "UI/Mapping/MapDayNightWidget.h"

using MapArtworkUtils::DrawRegionInk;
using MapArtworkUtils::Ink;
using MapArtworkUtils::MapSurfaceColor;
using MapArtworkUtils::Paper;
using MapArtworkUtils::Road;

TSharedRef<SWidget> MakeMapLegend()
{
    TSharedRef<SWrapBox> Legend = SNew(SWrapBox)
        .UseAllottedSize(true)
        .InnerSlotPadding(FVector2D(10, 3));
    const TPair<FName, const TCHAR*> Entries[] = {{TEXT("grass"), TEXT("Grass")},
        {TEXT("snow"), TEXT("Snow")},
        {TEXT("sand"), TEXT("Sand")},
        {TEXT("soil"), TEXT("Soil")},
        {TEXT("water"), TEXT("Water")},
        {TEXT("building"), TEXT("Building")},
        {TEXT("tunnel"), TEXT("Tunnel")},
        {TEXT("ground"), TEXT("Paving")}};
    for (const TPair<FName, const TCHAR*>& Entry : Entries)
    {
        Legend->AddSlot()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(0, 0, 4, 0)
            [
                SNew(SBox)
                    .WidthOverride(10)
                    .HeightOverride( 10)
                [
                    SNew(SBorder)
                        .Padding(0)
                        .BorderImage(FCoreStyle::Get() .GetBrush("WhiteBrush"))
                        .BorderBackgroundColor(MapSurfaceColor(Entry.Key))
                ]
            ]
            + SHorizontalBox::Slot()
                .AutoWidth()
            [
                SNew(STextBlock)
                    .Text(FText::FromString(Entry.Value))
                    .ColorAndOpacity(Ink)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
            ]
        ];
    }
    return Legend;
}

TSharedRef<SWidget> UWandererMapWidget::BuildPausePanel()
{
    return SNew(SBorder)
        .Visibility_UObject(this, &UWandererMapWidget::GetPauseVisibility)
        .BorderImage(FCoreStyle::Get() .GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(0.02f, 0.025f, 0.03f, 0.55f))
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Center)
    [
        SNew(SBox)
            .WidthOverride( 220)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
                .AutoHeight()
            [
                SNew(SButton)
                    .HAlign(HAlign_Center)
                    .ContentPadding(FMargin(20, 12))
                    .ButtonColorAndOpacity(FLinearColor(0.08f, 0.30f, 0.65f))
                    .OnClicked(FOnClicked::CreateUObject(
                        this, &UWandererMapWidget::OnResumeClicked))
                [
                    SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Resume")))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
                ]
            ]
            + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0, 8, 0, 0)
            [
                SNew(SButton)
                    .HAlign(HAlign_Center)
                    .ContentPadding(FMargin(20, 12))
                    .ButtonColorAndOpacity(FLinearColor(0.65f, 0.09f, 0.07f))
                    .OnClicked(FOnClicked::CreateUObject( this, &UWandererMapWidget::OnExitClicked))
                [
                    SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Exit")))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
                ]
            ]
        ]
    ];
}

EVisibility UWandererMapWidget::GetPauseVisibility() const
{
    const AWandererGameState* State =
        GetWorld() ? GetWorld()->GetGameState<AWandererGameState>() : nullptr;
    return State && State->GetPlayState() == EWandererPlayState::PauseMenu ? EVisibility::Visible
                                                                           : EVisibility::Collapsed;
}

FReply UWandererMapWidget::OnPauseClicked()
{
    AWandererGameState* State =
        GetWorld() ? GetWorld()->GetGameState<AWandererGameState>() : nullptr;
    if (State && State->GetPlayState() == EWandererPlayState::Normal)
    {
        State->SetPlayState(EWandererPlayState::PauseMenu);
    }
    return FReply::Handled();
}

FReply UWandererMapWidget::OnResumeClicked()
{
    AWandererGameState* State =
        GetWorld() ? GetWorld()->GetGameState<AWandererGameState>() : nullptr;
    if (State && State->GetPlayState() == EWandererPlayState::PauseMenu)
    {
        State->SetPlayState(EWandererPlayState::Normal);
    }
    return FReply::Handled();
}

FReply UWandererMapWidget::OnExitClicked()
{
    UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
    return FReply::Handled();
}

TSharedRef<SWidget> UWandererMapWidget::BuildDoorActions()
{
    return SNew(SBorder)
        .Padding(12)
        .Visibility_UObject(this, &UWandererMapWidget::GetDoorActionVisibility)
        .BorderImage(FCoreStyle::Get() .GetBrush("WhiteBrush"))
        .BorderBackgroundColor( Road)
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
            .AutoHeight()
        [
            SNew(STextBlock)
                .Text_UObject(this, &UWandererMapWidget::GetDoorLabel)
                .ColorAndOpacity(Ink)
                .MinDesiredWidth(180)
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
        ]
        + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 8)
        [
            SNew(SButton)
                .ButtonStyle(&TeleportStyle)
                .IsFocusable(false)
                .ContentPadding(FMargin(12, 8))
                .OnClicked(FOnClicked::CreateUObject( this,
                &UWandererMapWidget::OnTeleportClicked))
            [
                SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Teleport")))
                    .ColorAndOpacity(FLinearColor::White)
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
            ]
        ]
        + SVerticalBox::Slot()
            .AutoHeight()
        [
            SNew(STextBlock)
                .Text_UObject(this, &UWandererMapWidget::GetTeleportMessage)
                .ColorAndOpacity(Ink)
                .AutoWrapText(true)
        ]
        + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 4)
        [
            SNew(SButton)
                .ButtonStyle(&CancelStyle)
                .ContentPadding(FMargin(12, 8))
                .IsFocusable(false)
                .OnClicked(FOnClicked::CreateUObject(
                    this, &UWandererMapWidget::OnCancelDoorClicked))
            [
                SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Cancel")))
                    .ColorAndOpacity(FLinearColor::White)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
            ]
        ]
    ];
}

TSharedRef<SWidget> UWandererMapWidget::BuildMapHeading()
{
    return SNew(SHorizontalBox)
    + SHorizontalBox::Slot()
        .FillWidth(1)
    [
        SNew(STextBlock)
            .Text_UObject(this, &UWandererMapWidget::GetSceneTitle)
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 20))
            .ColorAndOpacity(Ink)
    ]
    + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign( VAlign_Top)
    [
        SNew(SBox)
            .WidthOverride(24)
            .HeightOverride(24)
        [
            SNew(SButton)
                .ButtonStyle(&CloseStyle)
                .IsFocusable(false)
                .ContentPadding(0)
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)
                .OnClicked(FOnClicked::CreateUObject(this, &UWandererMapWidget::OnCloseClicked))
                .ToolTipText(FText::FromString(TEXT("Close map")))
            [
                SNew(STextBlock)
                    .Text(FText::FromString(TEXT("×")))
                    .ColorAndOpacity(Road)
            ]
        ]
    ];
}

TSharedRef<SWidget> UWandererMapWidget::BuildMapCanvas()
{
    return SNew(SOverlay)
    + SOverlay::Slot()
    [
        SNew(SMapCanvasWidget)
            .ControllerWeakPtr(GetOwningPlayer<AWandererPlayerController>())
            .OnSelectDoor( FSelectMapDoor::CreateUObject(this, &UWandererMapWidget::OnDoorSelected))
    ]
    + SOverlay::Slot()
        .HAlign(HAlign_Right)
        .VAlign(VAlign_Bottom)
    [
        BuildDoorActions()
    ];
}

TSharedRef<SWidget> UWandererMapWidget::BuildMapFooter()
{
    return SNew(SHorizontalBox)
    + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(0, 0, 12, 0)
    [
        BuildDayNightButton()
    ]
    + SHorizontalBox::Slot()
        .FillWidth(1)
    [
        SNew(STextBlock)
            .Text_UObject(this, &UWandererMapWidget::GetScanText)
            .ColorAndOpacity(Ink)
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
    ]
    + SHorizontalBox::Slot()
        .AutoWidth()
    [
        SNew(SButton)
            .IsFocusable(false)
            .OnClicked( FOnClicked::CreateUObject(this, &UWandererMapWidget::OnRescanClicked))
        [
            SNew(STextBlock)
                .Text(FText::FromString(TEXT("Rescan")))
                .ColorAndOpacity(Ink)
        ]
    ];
}

TSharedRef<SWidget> UWandererMapWidget::BuildMapPanel()
{
    return SNew(SBox)
        .WidthOverride_UObject(this, &UWandererMapWidget::GetPanelWidth)
        .HeightOverride_UObject(this, &UWandererMapWidget::GetPanelHeight)
        .Visibility_UObject(this, &UWandererMapWidget::GetMapVisibility)
    [
        SNew(SBorder)
            .Padding(6)
            .BorderImage(FCoreStyle::Get() .GetBrush("WhiteBrush"))
            .BorderBackgroundColor(Ink)
        [
            SNew(SBorder)
                .Padding(18)
                .BorderImage(FCoreStyle::Get() .GetBrush("WhiteBrush"))
                .BorderBackgroundColor(Paper)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                    .AutoHeight()
                [
                    BuildMapHeading()
                ]
                + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0, 4)
                [
                    SNew(STextBlock)
                        .Text(FText::FromString(
                            TEXT("Doors: click to teleport  ·  Arrow: you  ·  N: north")))
                        .ColorAndOpacity(Ink)
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                ]
                + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0, 4)
                [
                    MakeMapLegend()
                ]
                + SVerticalBox::Slot()
                    .FillHeight(1)
                    .Padding( 0, 10)
                [
                    BuildMapCanvas()
                ]
                + SVerticalBox::Slot()
                    .AutoHeight()
                [
                    BuildMapFooter()
                ]
            ]
        ]
    ];
}

TSharedRef<SWidget> UWandererMapWidget::BuildDayNightButton()
{
    return SNew(SButton)
        .ButtonStyle(&DayNightStyle)
        .IsFocusable(false)
        .ContentPadding(FMargin(8, 4))
        .ButtonColorAndOpacity_UObject(this, &UWandererMapWidget::GetDayNightColor)
        .OnClicked(FOnClicked::CreateUObject(this, &UWandererMapWidget::OnDayNightClicked))
        .ToolTipText(FText::FromString(TEXT("Toggle day / night ambience")))
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
            .AutoWidth()
        [
            SNew(SMapDayNightWidget)
                .IsNight_UObject(this, &UWandererMapWidget::IsAudioNight)
        ]
        + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(6, 0)
        [
            SNew(STextBlock)
                .Text_UObject(this, &UWandererMapWidget::GetDayNightLabel)
                .ColorAndOpacity(FLinearColor::White)
        ]
    ];
}
