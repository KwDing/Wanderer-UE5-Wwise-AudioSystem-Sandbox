#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/** Leaf inventory card. Attributes and click intent are owned by its parent view. */
class SRadioItemWidget : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SRadioItemWidget)
    {
    }
        SLATE_ATTRIBUTE(FText, Label)
        SLATE_ATTRIBUTE(bool, Available)
        SLATE_ARGUMENT(const FSlateBrush*, Image)
        SLATE_EVENT(FOnClicked, OnDeploy)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
};
