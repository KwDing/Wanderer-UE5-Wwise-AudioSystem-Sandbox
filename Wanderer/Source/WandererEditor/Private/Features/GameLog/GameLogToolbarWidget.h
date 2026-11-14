#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FGameLogModel;

class SGameLogToolbarWidget : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SGameLogToolbarWidget)
    {
    }
    SLATE_ARGUMENT(TSharedPtr<FGameLogModel>, Model)
    SLATE_END_ARGS()
    void Construct(const FArguments& InArguments);

  private:
    TSharedRef<SWidget> MakePresetButton(const TCHAR* InLabel, int32 InPreset);
    TSharedRef<SWidget> MakeTools();
    TSharedPtr<FGameLogModel> Model;
};
