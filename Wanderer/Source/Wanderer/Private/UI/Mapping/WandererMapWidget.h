#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"

#include "Types/Audio/UIAudioTypes.h"

#include "WandererMapWidget.generated.h"

class AWandererPlayerController;

/** Presentation only; selection and scan state survive view reconstruction in the Controller. */
UCLASS()
class WANDERER_API UWandererMapWidget : public UUserWidget
{
    GENERATED_BODY()

  public:
    void SetMapOpen(
        bool bInOpen, EWandererUIAudioAction InCloseAction = EWandererUIAudioAction::Cancel);
    bool IsMapOpen() const;
    bool OnDoorSelected(FName InID);
    FName GetSelectedDoor() const;
    UFUNCTION(BlueprintCallable, Category = "Wanderer|Audio")
    bool ToggleDayNight();
    UFUNCTION(BlueprintPure, Category = "Wanderer|Audio")
    bool IsAudioNight() const;

  protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual TSharedRef<SWidget> RebuildWidget() override;

  private:
    void InitializeStyles();
    TSharedRef<SWidget> BuildHUDControls();
    void OnMapAudio(EWandererUIAudioAction InAction);
    TSharedRef<SWidget> BuildPausePanel();
    EVisibility GetPauseVisibility() const;
    FReply OnPauseClicked();
    FReply OnResumeClicked();
    FReply OnExitClicked();
    TSharedRef<SWidget> BuildDoorActions();
    TSharedRef<SWidget> BuildMapHeading();
    TSharedRef<SWidget> BuildMapCanvas();
    TSharedRef<SWidget> BuildMapFooter();
    TSharedRef<SWidget> BuildMapPanel();
    TSharedRef<SWidget> BuildDayNightButton();
    FReply OnDayNightClicked();
    FSlateColor GetDayNightColor() const;
    FText GetDayNightLabel() const;
    TSharedRef<SWidget> BuildLoadingPanel();
    EVisibility GetLoadingVisibility() const;
    TOptional<float> GetLoadingProgress() const;
    FText GetLoadingStatus() const;
    FText GetSceneTitle() const;
    FReply OnMapClicked();
    FReply OnCloseClicked();
    FReply OnRescanClicked();
    FReply OnTeleportClicked();
    FReply OnCancelDoorClicked();
    EVisibility GetDoorActionVisibility() const;
    FText GetDoorLabel() const;
    FText GetTeleportMessage() const;
    EVisibility GetMapVisibility() const;
    EVisibility GetHUDVisibility() const;
    FText GetSurfaceText() const;
    FText GetScanText() const;
    FOptionalSize GetPanelWidth() const;
    FOptionalSize GetPanelHeight() const;

    FButtonStyle CloseStyle;
    FButtonStyle DayNightStyle;
    FButtonStyle TeleportStyle;
    FButtonStyle CancelStyle;
    FProgressBarStyle LoadingProgressStyle;
    FDelegateHandle AudioHandle;
    TWeakObjectPtr<AWandererPlayerController> PlayerWeakPtr;
};
