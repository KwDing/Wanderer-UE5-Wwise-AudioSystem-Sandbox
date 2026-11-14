#include "UI/Loading/WandererLoadingMapWidget.h"

#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

void SWandererLoadingMapWidget::Construct(const FArguments& InArgs)
{
    SetVisibility(EVisibility::HitTestInvisible);
}

FVector2D SWandererLoadingMapWidget::ComputeDesiredSize(float InScale) const
{
    return FVector2D(900, 480);
}

int32 SWandererLoadingMapWidget::OnPaint(const FPaintArgs& Args,
    const FGeometry& Geometry,
    const FSlateRect& Culling,
    FSlateWindowElementList& Elements,
    int32 Layer,
    const FWidgetStyle& Style,
    bool bParentEnabled) const
{
    const FVector2D Size = Geometry.GetLocalSize();
    const FVector2D Center = Size * 0.5;
    const float Radius = FMath::Max(0.0f, float(FMath::Min(Size.X, Size.Y)) * 0.32f);
    const FLinearColor Ink(0.25f, 0.19f, 0.12f, 0.45f);
    const FLinearColor Red(0.32f, 0.065f, 0.055f, 0.8f);
    TArray<FVector2D> Ring;
    Ring.Reserve(65);
    for (int32 Index = 0; Index <= 64; ++Index)
    {
        const float Angle = Index * UE_TWO_PI / 64.0f;
        Ring.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
    }
    FSlateDrawElement::MakeLines(
        Elements, Layer, Geometry.ToPaintGeometry(), Ring, ESlateDrawEffect::None, Ink, true, 2.0f);
    for (int32 Index = 0; Index < 8; ++Index)
    {
        const float Angle = Index * UE_TWO_PI / 8.0f;
        const FVector2D Direction(FMath::Sin(Angle), -FMath::Cos(Angle));
        const FVector2D Right(-Direction.Y, Direction.X);
        const float Length = Radius * (Index % 2 == 0 ? 0.9f : 0.65f);
        const TArray<FVector2D> Needle{Center,
            Center + Right * Radius * 0.12f,
            Center + Direction * Length,
            Center - Right * Radius * 0.12f,
            Center};
        FSlateDrawElement::MakeLines(Elements,
            Layer + 1,
            Geometry.ToPaintGeometry(),
            Needle,
            ESlateDrawEffect::None,
            Index == 0 ? Red : Ink,
            true,
            2.5f);
    }
    FSlateDrawElement::MakeText(Elements,
        Layer + 2,
        Geometry.ToPaintGeometry(
            FVector2D(28, 28), FSlateLayoutTransform(Center + FVector2D(-9, -Radius - 35))),
        FText::FromString(TEXT("N")),
        FCoreStyle::GetDefaultFontStyle("Bold", 24),
        ESlateDrawEffect::None,
        Red);
    return Layer + 3;
}
