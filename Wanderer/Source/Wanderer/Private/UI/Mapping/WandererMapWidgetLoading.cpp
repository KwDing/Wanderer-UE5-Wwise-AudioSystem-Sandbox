#include "UI/Mapping/WandererMapWidget.h"

#include "Engine/World.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#include "Data/WandererMapMarkerSettings.h"
#include "Framework/WandererLoadingComponent.h"
#include "Framework/WandererPlayerController.h"
#include "UI/Loading/WandererLoadingMapWidget.h"
#include "UI/Mapping/MapArtworkUtils.h"

using MapArtworkUtils::DrawRegionInk;
using MapArtworkUtils::Ink;
using MapArtworkUtils::MapSurfaceColor;
using MapArtworkUtils::Paper;
using MapArtworkUtils::Road;

TSharedRef<SWidget> UWandererMapWidget::BuildLoadingPanel()
{
    return SNew(SBorder)
        .Visibility_UObject(this, &UWandererMapWidget::GetLoadingVisibility)
        .BorderImage(FCoreStyle::Get() .GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(0.76f, 0.65f, 0.44f, 1))
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Center)
        .Padding(40)
    [
        SNew(SBox)
            .WidthOverride(1000)
            .HeightOverride( 680)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0, 12)
            [
                SNew(STextBlock)
                    .Text_UObject(this, &UWandererMapWidget::GetSceneTitle)
                    .ColorAndOpacity(Ink)
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 28))
            ]
            + SVerticalBox::Slot()
                .FillHeight( 1)
            [
                SNew(SOverlay)
                + SOverlay::Slot()
                [
                    SNew(SWandererLoadingMapWidget)
                ]
                + SOverlay::Slot()
                [
                    SNew(SBackgroundBlur)
                        .BlurStrength(2.5f)
                ]
            ]
            + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0, 16)
            [
                SNew(STextBlock)
                    .Text_UObject(this, &UWandererMapWidget::GetLoadingStatus)
                    .ColorAndOpacity(Ink)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 18))
            ]
            + SVerticalBox::Slot()
                .AutoHeight()
            [
                SNew(SBox)
                    .HeightOverride(18)
                [
                    SNew(SProgressBar)
                        .Style(&LoadingProgressStyle)
                        .Percent_UObject(this, &UWandererMapWidget::GetLoadingProgress)
                        .FillColorAndOpacity(FLinearColor(0.32f, 0.065f, 0.055f))
                        .BorderPadding(FVector2D::ZeroVector)
                ]
            ]
        ]
    ];
}

EVisibility UWandererMapWidget::GetLoadingVisibility() const
{
    const AWandererPlayerController* Player = GetOwningPlayer<AWandererPlayerController>();
    return Player && Player->GetLoadingComponent()->IsLoading() ? EVisibility::Visible
                                                                : EVisibility::Collapsed;
}

TOptional<float> UWandererMapWidget::GetLoadingProgress() const
{
    const AWandererPlayerController* Player = GetOwningPlayer<AWandererPlayerController>();
    return Player ? Player->GetLoadingComponent()->GetProgress() : 0.0f;
}

FText UWandererMapWidget::GetLoadingStatus() const
{
    const AWandererPlayerController* Player = GetOwningPlayer<AWandererPlayerController>();
    return Player ? Player->GetLoadingComponent()->GetStatus() : FText::GetEmpty();
}

FText UWandererMapWidget::GetSceneTitle() const
{
    const UWorld* World = GetWorld();
    return GetDefault<UWandererMapMarkerSettings>()->GetSceneTitle(
        World ? UWandererMapMarkerSettings::GetScenePackage(*World) : FString());
}
