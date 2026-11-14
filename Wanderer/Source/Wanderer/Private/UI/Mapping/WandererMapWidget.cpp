#include "UI/Mapping/WandererMapWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#include "Framework/WandererLoadingComponent.h"
#include "Framework/WandererPlayerController.h"
#include "Interfaces/CharacterSurfaceProvider.h"
#include "UI/Mapping/MapArtworkUtils.h"

using MapArtworkUtils::DrawRegionInk;
using MapArtworkUtils::Ink;
using MapArtworkUtils::MapSurfaceColor;
using MapArtworkUtils::Paper;
using MapArtworkUtils::Road;

TSharedRef<SWidget> UWandererMapWidget::RebuildWidget()
{
    SetIsFocusable(false);
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    InitializeStyles();
    return SNew(SOverlay)
    + SOverlay::Slot()
    [
        SNew(SOverlay)
            .Visibility_UObject(this, &UWandererMapWidget::GetHUDVisibility)
        + SOverlay::Slot()
            .HAlign(HAlign_Left)
            .VAlign(VAlign_Top)
            .Padding(24)
        [
            BuildHUDControls()
        ]
        + SOverlay::Slot()
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
        [
            BuildMapPanel()
        ]
    ]
    + SOverlay::Slot()
    [
        BuildPausePanel()
    ]
    + SOverlay::Slot()
    [
        BuildLoadingPanel()
    ];
}

void UWandererMapWidget::InitializeStyles()
{
    CloseStyle.SetNormal(FSlateRoundedBoxBrush(FLinearColor(0.32f, 0.065f, 0.055f), 12.0f));
    CloseStyle.SetHovered(FSlateRoundedBoxBrush(FLinearColor(0.46f, 0.12f, 0.085f), 12.0f));
    CloseStyle.SetPressed(FSlateRoundedBoxBrush(FLinearColor(0.23f, 0.045f, 0.035f), 12.0f));
    DayNightStyle.SetNormal(FSlateRoundedBoxBrush(FLinearColor::White, 6.0f));
    DayNightStyle.SetHovered(FSlateRoundedBoxBrush(FLinearColor(1.2f, 1.2f, 1.2f), 6.0f));
    DayNightStyle.SetPressed(FSlateRoundedBoxBrush(FLinearColor(.75f, .75f, .75f), 6.0f));
    TeleportStyle.SetNormal(FSlateRoundedBoxBrush(FLinearColor(0.19f, 0.22f, 0.15f), 1.0f));
    TeleportStyle.SetHovered(FSlateRoundedBoxBrush(FLinearColor(0.29f, 0.32f, 0.22f), 1.0f));
    TeleportStyle.SetPressed(FSlateRoundedBoxBrush(FLinearColor(0.12f, 0.15f, 0.10f), 1.0f));
    CancelStyle.SetNormal(FSlateRoundedBoxBrush(FLinearColor(0.26f, 0.14f, 0.10f), 1.0f));
    CancelStyle.SetHovered(FSlateRoundedBoxBrush(FLinearColor(0.36f, 0.21f, 0.15f), 1.0f));
    CancelStyle.SetPressed(FSlateRoundedBoxBrush(FLinearColor(0.17f, 0.085f, 0.06f), 1.0f));
    LoadingProgressStyle.SetBackgroundImage(
        FSlateRoundedBoxBrush(FLinearColor(0.95f, 0.88f, 0.70f), 3.0f));
    LoadingProgressStyle.SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White, 3.0f));
}

TSharedRef<SWidget> UWandererMapWidget::BuildHUDControls()
{
    return SNew(SVerticalBox)
    + SVerticalBox::Slot()
        .AutoHeight()
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding( 0, 0, 6, 0)
        [
            SNew(SButton)
                .IsFocusable(false)
                .ContentPadding(FMargin(12, 10))
                .ButtonColorAndOpacity(Paper)
                .ToolTipText(FText::FromString(TEXT("Pause menu")))
                .OnClicked(FOnClicked::CreateUObject(this, &UWandererMapWidget::OnPauseClicked))
            [
                SNew(STextBlock)
                    .Text(FText::FromString(TEXT("\u2261")))
                    .ColorAndOpacity(Ink)
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
            ]
        ]
        + SHorizontalBox::Slot()
            .AutoWidth()
        [
            SNew(SButton)
                .IsFocusable(false)
                .ContentPadding(FMargin(20, 10))
                .ButtonColorAndOpacity(Paper)
                .OnClicked(FOnClicked::CreateUObject( this, &UWandererMapWidget::OnMapClicked))
            [
                SNew(STextBlock)
                    .Text(FText::FromString(TEXT("view map (M)")))
                    .ColorAndOpacity(Ink)
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
            ]
        ]
    ]
    + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0, 8)
    [
        SNew(STextBlock)
            .Text_UObject(this, &UWandererMapWidget::GetSurfaceText)
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
            .ShadowOffset(FVector2D(1, 1))
    ];
}

EVisibility UWandererMapWidget::GetMapVisibility() const
{
    return IsMapOpen() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
}

EVisibility UWandererMapWidget::GetHUDVisibility() const
{
    const AWandererPlayerController* Player = GetOwningPlayer<AWandererPlayerController>();
    return Player && !Player->GetLoadingComponent()->IsLoading()
                   && !Player->IsCharacterSelectionOpen()
               ? EVisibility::SelfHitTestInvisible
               : EVisibility::Collapsed;
}

FText UWandererMapWidget::GetSurfaceText() const
{
    const ICharacterSurfaceProvider* Provider = Cast<ICharacterSurfaceProvider>(GetOwningPlayer());
    const FCharacterSurfaceSnapshot Snapshot =
        Provider ? Provider->QueryContactSurface(nullptr, FVector::ZeroVector)
                 : FCharacterSurfaceSnapshot();
    const FName Surface = Provider ? Snapshot.Surface : NAME_None;
    const FName Wetness = Provider ? Snapshot.Wetness : NAME_None;
    const FString WetnessLabel = Wetness == TEXT("mild")  ? TEXT("Half-wet")
                                 : Wetness == TEXT("wet") ? TEXT("Wet")
                                 : Wetness == TEXT("dry") ? TEXT("Dry")
                                                          : TEXT("...");
    return FText::FromString(Surface.IsNone() ? TEXT("Surface: … | Wetness: …")
                                              : FString::Printf(TEXT("Surface: %s | Wetness: %s"),
                                                    *Surface.ToString(),
                                                    *WetnessLabel));
}

FText UWandererMapWidget::GetScanText() const
{
    const AWandererPlayerController* Player = GetOwningPlayer<AWandererPlayerController>();
    const FMapScanSnapshot* Map = Player ? Player->GetMapScanSnapshot() : nullptr;
    if (!Map || Map->Cells.Num() == 0)
    {
        return FText::FromString(TEXT("Waiting for walkable ground…"));
    }
    if ((Map->bScanning || Map->bSurveying))
    {
        return FText::FromString(TEXT("Drawing terrain and structures… You can keep walking."));
    }
    return FText::FromString(
        Map->bTruncated
            ? TEXT("Partial survey · Rescan from your location to explore further")
            : TEXT("WASD to walk · Hold right mouse to look · Rescan after doors change"));
}

FOptionalSize UWandererMapWidget::GetPanelWidth() const
{
    const FVector2D Size = UWidgetLayoutLibrary::GetViewportSize(GetOwningPlayer())
                           / FMath::Max(0.1f, UWidgetLayoutLibrary::GetViewportScale(this));
    return FMath::Min(780.0, Size.X * 0.72);
}

FOptionalSize UWandererMapWidget::GetPanelHeight() const
{
    const FVector2D Size = UWidgetLayoutLibrary::GetViewportSize(GetOwningPlayer())
                           / FMath::Max(0.1f, UWidgetLayoutLibrary::GetViewportScale(this));
    return FMath::Min(580.0, Size.Y * 0.72);
}
