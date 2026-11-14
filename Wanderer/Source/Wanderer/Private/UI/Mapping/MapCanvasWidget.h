#pragma once

#include "Widgets/SLeafWidget.h"

class AWandererPlayerController;

DECLARE_DELEGATE_RetVal_OneParam(bool, FSelectMapDoor, FName);

struct FMapProjection
{
    FVector2D Center;
    FVector2D Size;
    double Scale = 1;
    FVector2D Project(const FVector& Position) const;
};

class SMapCanvasWidget : public SLeafWidget
{
  public:
    SLATE_BEGIN_ARGS(SMapCanvasWidget)
    {
    }
    SLATE_ARGUMENT(TWeakObjectPtr<AWandererPlayerController>, ControllerWeakPtr)
    SLATE_EVENT(FSelectMapDoor, OnSelectDoor)
    SLATE_END_ARGS()
    void Construct(const FArguments& InArgs);
    virtual FVector2D ComputeDesiredSize(float InScale) const override;
    virtual int32 OnPaint(const FPaintArgs& InArgs,
        const FGeometry& InGeometry,
        const FSlateRect& InCulling,
        FSlateWindowElementList& InOutElements,
        int32 InLayer,
        const FWidgetStyle& InStyle,
        bool bInParentEnabled) const override;
    virtual FReply OnMouseButtonDown(
        const FGeometry& InGeometry, const FPointerEvent& InEvent) override;

  private:
    bool GetProjection(const FGeometry& InGeometry, FMapProjection& OutProjection) const;
    void DrawDoors(const FGeometry& InGeometry,
        const FMapProjection& InProjection,
        FSlateWindowElementList& InOutElements,
        int32 InLayer) const;
    TWeakObjectPtr<AWandererPlayerController> ControllerWeakPtr;
    FSelectMapDoor OnSelectDoor;
};
