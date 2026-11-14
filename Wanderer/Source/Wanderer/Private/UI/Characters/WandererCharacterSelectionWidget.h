#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "WandererCharacterSelectionWidget.generated.h"
class SWidget;
class UCharacterSelectionModel;

UCLASS()
class UWandererCharacterSelectionWidget : public UUserWidget
{
    GENERATED_BODY()
  public:
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

  protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;

  private:
    UPROPERTY(Transient)
    FSlateBrush PortraitBrush;
    TWeakObjectPtr<UCharacterSelectionModel> ModelWeakPtr;
    FDelegateHandle ChangedHandle;
    bool bWasOpen = false;
    UCharacterSelectionModel* GetModel() const;
    void OnModelChanged();
    void UnbindModel();
    TSharedRef<SWidget> BuildOpenButton();
    TSharedRef<SWidget> BuildCharacterList();
    TSharedRef<SWidget> BuildCharacterRow(int32 InIndex);
    TSharedRef<SWidget> BuildSelectionPanel();
    TSharedRef<SWidget> BuildPreviewPanel();
    TSharedRef<SWidget> BuildSelectionContent();
    TSharedRef<SWidget> BuildSelectionFooter();
    FReply OnOpenSelection();
    FReply OnCancelSelection();
    FReply OnSelectPreview(int32 InIndex);
    FReply OnConfirmSelection();
    EVisibility GetPanelVisibility() const;
    EVisibility GetButtonVisibility() const;
    FText GetSelectedName() const;
    FText GetStatusText() const;
    FText GetRowText(int32 InIndex) const;
    bool CanChoose() const;
};
