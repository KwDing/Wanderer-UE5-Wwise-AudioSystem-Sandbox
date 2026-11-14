#pragma once

#include "Widgets/SLeafWidget.h"

/** Vector glyphs avoid depending on an installed emoji/symbol font. */
class SMapDayNightWidget : public SLeafWidget
{
  public:
    SLATE_BEGIN_ARGS(SMapDayNightWidget)
    {
    }
    SLATE_ATTRIBUTE(bool, IsNight)
    SLATE_END_ARGS()
    void Construct(const FArguments& InArgs);
    virtual FVector2D ComputeDesiredSize(float InScale) const override;
    virtual int32 OnPaint(const FPaintArgs& InArgs,
        const FGeometry& InGeometry,
        const FSlateRect& InCulling,
        FSlateWindowElementList& InOutElements,
        int32 InLayer,
        const FWidgetStyle& InStyle,
        bool bInEnabled) const override;

  private:
    TAttribute<bool> Night;
};
