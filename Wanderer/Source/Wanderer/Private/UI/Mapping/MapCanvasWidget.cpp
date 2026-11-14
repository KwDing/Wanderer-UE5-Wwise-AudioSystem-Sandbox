#include "UI/Mapping/MapCanvasWidget.h"

#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

#include "Data/WandererMapMarkerSettings.h"
#include "Framework/WandererPlayerController.h"
#include "UI/Mapping/MapArtworkUtils.h"

using MapArtworkUtils::DrawRegionInk;
using MapArtworkUtils::Ink;
using MapArtworkUtils::MapSurfaceColor;
using MapArtworkUtils::Paper;
using MapArtworkUtils::Road;

namespace
{
    struct FMapPaintContext
    {
        const FGeometry& Geometry;
        FSlateWindowElementList& Elements;
        const FMapProjection& Projection;
        int32 Layer;
    };
    void DrawMapRegions(const FMapPaintContext& InPaint, const FMapScanSnapshot& InMap)
    {
        const FSlateBrush* Brush = FCoreStyle::Get().GetBrush("WhiteBrush");
        for (const FWandererMapRegion& Region : InMap.Regions)
        {
            const FVector2D Point = InPaint.Projection.Project(FVector(Region.Center, 0));
            const FVector2D RegionSize(
                Region.Size.Y * InPaint.Projection.Scale, Region.Size.X * InPaint.Projection.Scale);
            FSlateDrawElement::MakeBox(InPaint.Elements,
                InPaint.Layer,
                InPaint.Geometry.ToPaintGeometry(
                    RegionSize + FVector2D(0.4), FSlateLayoutTransform(Point - RegionSize * 0.5)),
                Brush,
                ESlateDrawEffect::None,
                Region.Surface == TEXT("soil") ? Paper : MapSurfaceColor(Region.Surface));
            DrawRegionInk(InPaint.Geometry,
                InPaint.Elements,
                InPaint.Layer + 1,
                FSlateRect(Point.X - RegionSize.X * 0.5,
                    Point.Y - RegionSize.Y * 0.5,
                    Point.X + RegionSize.X * 0.5,
                    Point.Y + RegionSize.Y * 0.5),
                Region.Surface);
        }
    }
    void DrawMapConnectivity(const FMapPaintContext& InPaint, const FMapScanSnapshot& InMap)
    {
        const FSlateBrush* Brush = FCoreStyle::Get().GetBrush("WhiteBrush");
        const double CellPixels = InMap.CellSize * InPaint.Projection.Scale;
        for (const FVector& Cell : InMap.Cells)
        {
            const FVector2D Point = InPaint.Projection.Project(Cell);
            FSlateDrawElement::MakeBox(InPaint.Elements,
                InPaint.Layer,
                InPaint.Geometry.ToPaintGeometry(FVector2D(CellPixels + 0.5),
                    FSlateLayoutTransform(Point - FVector2D(CellPixels / 2))),
                Brush,
                ESlateDrawEffect::None,
                Road);
            const FIntPoint Key(FMath::RoundToInt((Cell.X - InMap.Seed.X) / InMap.CellSize),
                FMath::RoundToInt((Cell.Y - InMap.Seed.Y) / InMap.CellSize));
            const FIntPoint Neighbors[] = {
                FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1)};
            for (const FIntPoint& Neighbor : Neighbors)
            {
                if (InMap.CellLookup.Contains(Key + Neighbor))
                {
                    continue;
                }
                const FVector2D Normal(Neighbor.Y, -Neighbor.X);
                const FVector2D Tangent(-Normal.Y, Normal.X);
                const FVector2D Edge = Point + Normal * CellPixels * 0.5;
                FSlateDrawElement::MakeLines(InPaint.Elements,
                    InPaint.Layer + 1,
                    InPaint.Geometry.ToPaintGeometry(),
                    TArray<FVector2D>{
                        Edge - Tangent * CellPixels * 0.5, Edge + Tangent * CellPixels * 0.5},
                    ESlateDrawEffect::None,
                    Ink,
                    true,
                    1.0f);
            }
        }
    }
    void DrawMapEntrances(const FMapPaintContext& InPaint, const FMapScanSnapshot& InMap)
    {
        for (const FVector& Entrance : InMap.Entrances)
        {
            const FVector2D Point = InPaint.Projection.Project(Entrance);
            FSlateDrawElement::MakeLines(InPaint.Elements,
                InPaint.Layer + 2,
                InPaint.Geometry.ToPaintGeometry(),
                TArray<FVector2D>{Point + FVector2D(-4, 4),
                    Point + FVector2D(-4, -2),
                    Point + FVector2D(0, -5),
                    Point + FVector2D(4, -2),
                    Point + FVector2D(4, 4)},
                ESlateDrawEffect::None,
                FLinearColor(0.15f, 0.3f, 0.25f),
                true,
                2.0f);
        }
    }
    void DrawMapPlayer(const FMapPaintContext& InPaint, const FMapPlayerSnapshot& InPlayer)
    {
        if (InPlayer.bVisible)
        {
            const FVector2D Point = InPaint.Projection.Project(InPlayer.Location);
            const FVector Forward = InPlayer.Forward;
            const FVector2D Direction(Forward.Y, -Forward.X);
            const FVector2D Right(-Direction.Y, Direction.X);
            const TArray<FVector2D> Arrow{Point + Direction * 13,
                Point - Direction * 8 + Right * 7,
                Point - Direction * 3,
                Point - Direction * 8 - Right * 7,
                Point + Direction * 13};
            FSlateDrawElement::MakeLines(InPaint.Elements,
                InPaint.Layer + 3,
                InPaint.Geometry.ToPaintGeometry(),
                Arrow,
                ESlateDrawEffect::None,
                FLinearColor(0.40f, 0.075f, 0.055f),
                true,
                3.0f);
        }
    }
} // namespace

FVector2D FMapProjection::Project(const FVector& Position) const
{
    return Size * 0.5 + FVector2D(Position.Y - Center.Y, Center.X - Position.X) * Scale;
}

void SMapCanvasWidget::Construct(const FArguments& InArgs)
{
    ControllerWeakPtr = InArgs._ControllerWeakPtr;
    OnSelectDoor = InArgs._OnSelectDoor;
    SetVisibility(EVisibility::Visible);
}

FVector2D SMapCanvasWidget::ComputeDesiredSize(float InScale) const
{
    return FVector2D(600, 350);
}

int32 SMapCanvasWidget::OnPaint(const FPaintArgs& InArgs,
    const FGeometry& InGeometry,
    const FSlateRect& InCulling,
    FSlateWindowElementList& InOutElements,
    int32 InLayer,
    const FWidgetStyle& InStyle,
    bool bInParentEnabled) const
{
    const AWandererPlayerController* Player = ControllerWeakPtr.Get();
    const FMapScanSnapshot* Map = Player ? Player->GetMapScanSnapshot() : nullptr;
    if (!Map || !Map->MapBounds.bIsValid)
    {
        return InLayer;
    }
    FMapProjection Projection;
    if (!GetProjection(InGeometry, Projection))
    {
        return InLayer;
    }
    const FMapPaintContext Paint{InGeometry, InOutElements, Projection, InLayer};
    DrawMapRegions(Paint, *Map);
    // Connectivity remains a fallback so it cannot hide surface colours.
    if (Map->Regions.IsEmpty())
    {
        DrawMapConnectivity(Paint, *Map);
    }
    DrawMapEntrances(Paint, *Map);
    DrawDoors(InGeometry, Projection, InOutElements, InLayer + 2);
    DrawMapPlayer(Paint, Player->GetMapPlayerSnapshot());
    const FVector2D Size = Projection.Size;
    FSlateDrawElement::MakeText(InOutElements,
        InLayer + 3,
        InGeometry.ToPaintGeometry(
            FVector2D(20, 20), FSlateLayoutTransform(FVector2D(Size.X - 28, 4))),
        FText::FromString(TEXT("N")),
        FCoreStyle::GetDefaultFontStyle("Bold", 14),
        ESlateDrawEffect::None,
        Ink);
    return InLayer + 4;
}

FReply SMapCanvasWidget::OnMouseButtonDown(
    const FGeometry& InGeometry, const FPointerEvent& InEvent)
{
    if (InEvent.GetEffectingButton() != EKeys::LeftMouseButton || !OnSelectDoor.IsBound())
    {
        return FReply::Unhandled();
    }
    const AWandererPlayerController* Player = ControllerWeakPtr.Get();
    FMapProjection Projection;
    if (!Player || !Player->GetWorld() || !GetProjection(InGeometry, Projection))
    {
        return FReply::Unhandled();
    }
    const FString ScenePackage = UWandererMapMarkerSettings::GetScenePackage(*Player->GetWorld());
    const FVector2D Cursor = InGeometry.AbsoluteToLocal(InEvent.GetScreenSpacePosition());
    for (const FWandererMapDoorMarker& Marker :
        GetDefault<UWandererMapMarkerSettings>()->GetDoorMarkers())
    {
        if (Marker.ScenePackage == ScenePackage
            && FVector2D::Distance(Cursor, Projection.Project(Marker.DoorLocation)) <= 18
            && OnSelectDoor.Execute(Marker.ID))
        {
            return FReply::Handled();
        }
    }
    return FReply::Unhandled();
}

bool SMapCanvasWidget::GetProjection(
    const FGeometry& InGeometry, FMapProjection& OutProjection) const
{
    const AWandererPlayerController* Player = ControllerWeakPtr.Get();
    const FMapScanSnapshot* Map = Player ? Player->GetMapScanSnapshot() : nullptr;
    if (!Map || !Map->MapBounds.bIsValid || !Player->GetWorld())
    {
        return false;
    }
    FBox2D Bounds = Map->MapBounds;
    const FString ScenePackage = UWandererMapMarkerSettings::GetScenePackage(*Player->GetWorld());
    for (const FWandererMapDoorMarker& Marker :
        GetDefault<UWandererMapMarkerSettings>()->GetDoorMarkers())
    {
        if (Marker.ScenePackage == ScenePackage)
        {
            Bounds += FVector2D(Marker.DoorLocation);
        }
    }
    Bounds = Bounds.ExpandBy(Map->CellSize * 2);
    OutProjection.Center = Bounds.GetCenter();
    OutProjection.Size = InGeometry.GetLocalSize();
    OutProjection.Scale = FMath::Min((OutProjection.Size.X - 40) / Bounds.GetSize().Y,
        (OutProjection.Size.Y - 40) / Bounds.GetSize().X);
    return OutProjection.Scale > 0;
}

void SMapCanvasWidget::DrawDoors(const FGeometry& InGeometry,
    const FMapProjection& InProjection,
    FSlateWindowElementList& InOutElements,
    int32 InLayer) const
{
    const AWandererPlayerController* Player = ControllerWeakPtr.Get();
    if (!Player || !Player->GetWorld())
    {
        return;
    }
    const FString ScenePackage = UWandererMapMarkerSettings::GetScenePackage(*Player->GetWorld());
    for (const FWandererMapDoorMarker& Marker :
        GetDefault<UWandererMapMarkerSettings>()->GetDoorMarkers())
    {
        if (Marker.ScenePackage != ScenePackage)
        {
            continue;
        }
        const FVector2D Point = InProjection.Project(Marker.DoorLocation);
        const FLinearColor DoorInk =
            Marker.ID == Player->GetSelectedMapDoor() ? FLinearColor(0.40f, 0.075f, 0.055f) : Ink;
        // Ink arch, open leaf and small handle: no additional texture asset.
        FSlateDrawElement::MakeLines(InOutElements,
            InLayer,
            InGeometry.ToPaintGeometry(),
            TArray<FVector2D>{Point + FVector2D(-7, 8),
                Point + FVector2D(-7, -3),
                Point + FVector2D(-3, -8),
                Point + FVector2D(3, -8),
                Point + FVector2D(7, -3),
                Point + FVector2D(7, 8)},
            ESlateDrawEffect::None,
            DoorInk,
            true,
            2.0f);
        FSlateDrawElement::MakeLines(InOutElements,
            InLayer + 1,
            InGeometry.ToPaintGeometry(),
            TArray<FVector2D>{Point + FVector2D(-5, 7),
                Point + FVector2D(-5, -4),
                Point + FVector2D(3, -1),
                Point + FVector2D(3, 10),
                Point + FVector2D(-5, 7)},
            ESlateDrawEffect::None,
            DoorInk,
            true,
            1.5f);
        FSlateDrawElement::MakeLines(InOutElements,
            InLayer + 1,
            InGeometry.ToPaintGeometry(),
            TArray<FVector2D>{Point + FVector2D(0, 3), Point + FVector2D(1, 3)},
            ESlateDrawEffect::None,
            DoorInk,
            true,
            2.0f);
    }
}
