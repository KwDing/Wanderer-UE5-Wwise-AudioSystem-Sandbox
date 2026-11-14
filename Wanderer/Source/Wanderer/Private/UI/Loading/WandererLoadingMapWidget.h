#pragma once

#include "Widgets/SLeafWidget.h"

/** Shared decorative compass, with no scene or UObject access; safe on MoviePlayer's thread. */
class SWandererLoadingMapWidget : public SLeafWidget
{
  public:
    SLATE_BEGIN_ARGS(SWandererLoadingMapWidget)
    {
    }
    SLATE_END_ARGS()
    void Construct(const FArguments& InArgs);
    virtual FVector2D ComputeDesiredSize(float InScale) const override;
    virtual int32 OnPaint(const FPaintArgs& Args,
        const FGeometry& Geometry,
        const FSlateRect& Culling,
        FSlateWindowElementList& Elements,
        int32 Layer,
        const FWidgetStyle& Style,
        bool bParentEnabled) const override;
};
