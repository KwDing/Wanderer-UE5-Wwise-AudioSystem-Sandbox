#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FGameLogModel;
class SMultiLineEditableTextBox;
class STextBlock;

class SGameLogPanel : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SGameLogPanel)
    {
    }
    SLATE_ARGUMENT(TSharedPtr<FGameLogModel>, Model)
    SLATE_END_ARGS()
    void Construct(const FArguments& InArguments);
    virtual void Tick(const FGeometry& InGeometry, double InTime, float InDelta) override;

  private:
    TSharedPtr<FGameLogModel> Model;
    TSharedPtr<SMultiLineEditableTextBox> LogText;
    TSharedPtr<STextBlock> StatusText;
    FString DisplayedText;
    double NextRefresh = 0;
};
