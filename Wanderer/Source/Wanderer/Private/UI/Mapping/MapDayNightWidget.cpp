#include "UI/Mapping/MapDayNightWidget.h"

#include "Rendering/DrawElements.h"

void SMapDayNightWidget::Construct(const FArguments& InArgs)
{
    Night = InArgs._IsNight;
}

FVector2D SMapDayNightWidget::ComputeDesiredSize(float InScale) const
{
    return FVector2D(28, 28);
}

int32 SMapDayNightWidget::OnPaint(const FPaintArgs& InArgs,
    const FGeometry& InGeometry,
    const FSlateRect& InCulling,
    FSlateWindowElementList& InOutElements,
    int32 InLayer,
    const FWidgetStyle& InStyle,
    bool bInEnabled) const
{
    const FVector2D Center = InGeometry.GetLocalSize() * .5;
    const FLinearColor Color = FLinearColor::White * InStyle.GetColorAndOpacityTint();
    TArray<FVector2D> Outline;
    const bool bNight = Night.Get(false);
    for (int32 Index = 0; Index <= 32; ++Index)
    {
        const double Angle =
            FMath::DegreesToRadians(bNight ? 60.0 + Index * 240.0 / 32 : Index * 360.0 / 32);
        Outline.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * (bNight ? 9 : 5));
    }
    if (bNight)
    {
        for (int32 Index = 0; Index <= 24; ++Index)
        {
            const double Angle = FMath::DegreesToRadians(240.0 - Index * 120.0 / 24);
            Outline.Add(
                Center + FVector2D(9, 0) + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * 9);
        }
    }
    FSlateDrawElement::MakeLines(InOutElements,
        InLayer,
        InGeometry.ToPaintGeometry(),
        Outline,
        ESlateDrawEffect::None,
        Color,
        true,
        2);
    if (!bNight)
    {
        for (int32 Index = 0; Index < 8; ++Index)
        {
            const double Angle = Index * PI / 4;
            const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
            FSlateDrawElement::MakeLines(InOutElements,
                InLayer,
                InGeometry.ToPaintGeometry(),
                TArray<FVector2D>{Center + Direction * 8, Center + Direction * 11},
                ESlateDrawEffect::None,
                Color,
                true,
                2);
        }
    }
    return InLayer;
}
