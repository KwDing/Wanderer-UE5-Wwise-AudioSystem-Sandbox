#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"

#include "Types/RadioControlTypes.h"

#include "WandererSpeakerWidget.generated.h"

class SVerticalBox;
class SBorder;
class URadioControlModel;
class UTextureRenderTarget2D;

UCLASS()
class UWandererSpeakerWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category = "Speaker")
    void SetPanelOpen(bool bOpen);

protected:
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& InGeometry, float InDeltaTime) override;
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool bInReleaseChildren) override;

private:
    using FRadioButtonCallback = FReply (UWandererSpeakerWidget::*)();
    UPROPERTY(Transient)
    TObjectPtr<UTextureRenderTarget2D> RadioTexture;
    UPROPERTY(Transient)
    TObjectPtr<UTextureRenderTarget2D> MountedTexture;

    TSharedPtr<SVerticalBox> DeviceList;
    TArray<FGuid> DisplayedSources;
    FRadioControlSnapshot State;
    TSharedPtr<SBorder> InventoryBorder;
    TSharedPtr<SBorder> ActionBorder;
    TSharedPtr<SBorder> PlacementBorder;
    FSlateBrush RadioBrush;
    FSlateBrush MountedBrush;

    void AddInventoryActions(TSharedRef<SVerticalBox> InOptions);
    TSharedRef<SWidget> BuildActionButton(
        TAttribute<FText> InText, FRadioButtonCallback InClicked, FLinearColor InColor);
    TSharedRef<SWidget> BuildInventory();
    TSharedRef<SWidget> BuildInventoryHeader();
    TSharedRef<SWidget> BuildInventoryOptions();
    TSharedRef<SWidget> BuildItemCard(EWandererSpeakerItem InItem);
    TSharedRef<SWidget> BuildPlacementActions();
    TSharedRef<SWidget> BuildSelectionActions();
    TSharedRef<SWidget> BuildSelectionButtons();
    bool CanDeployItem(EWandererSpeakerItem InItem) const;
    bool CanRetrieveSelected() const;
    const FRadioDeviceSnapshot* FindDevice(FGuid InID) const;
    FText GetDeviceText(FGuid InID) const;
    EVisibility GetHUDVisibility() const;
    ECheckBoxState GetHeadphoneCheckState() const;
    FText GetItemText(EWandererSpeakerItem InItem) const;
    URadioControlModel* GetModel() const;
    FText GetOptionText(FName InID, FString InLabel) const;
    EVisibility GetPanelVisibility() const;
    EVisibility GetPlacementVisibility() const;
    FText GetPlaybackText() const;
    FMargin GetSelectionPadding() const;
    FText GetSelectionText() const;
    EVisibility GetSelectionVisibility() const;
    FText GetStatusText() const;
    FReply OnCancelPlacement();
    FReply OnCancelSelection();
    FReply OnConfirmPlacement();
    FReply OnDeployItem(EWandererSpeakerItem InItem);
    FReply OnRetrieveAll();
    FReply OnRetrieveSelected();
    FReply OnRotatePlacement();
    FReply OnSelectDevice(FGuid InID);
    FReply OnSelectOption(FName InID);
    void OnSetHeadphoneCheckState(ECheckBoxState InState);
    FReply OnTogglePanel();
    FReply OnTogglePlayback();
    void DestroyPreview();
    bool Execute(ERadioControlAction InAction);
    void RefreshDeviceList();
    void RefreshPreview();
    void RefreshState();
};
