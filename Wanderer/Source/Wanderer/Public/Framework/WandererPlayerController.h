#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Types/GameStateTypes.h"
#include "Interfaces/CharacterSurfaceProvider.h"
#include "Interfaces/SceneEntryProvider.h"
#include "Types/MapTypes.h"
#include "Types/Audio/UIAudioTypes.h"
#include "WandererPlayerController.generated.h"

class UListenerAudioManager;
class AWandererGameState;
class UWandererCharacterCatalog;
class UCharacterSelectionModel;
class UTextureRenderTarget2D;
class UCameraComponent;
class UWandererSurfaceComponent;
class UWandererMapComponent;
class UWandererLoadingComponent;
class URadioControlModel;
class UAkAudioEvent;

/** Local session coordinator; owns stable models and never constructs concrete views. */
UCLASS()
class WANDERER_API AWandererPlayerController : public APlayerController,
                                               public ICharacterSurfaceProvider,
                                               public ISceneEntryProvider
{
    GENERATED_BODY()

  public:
    AWandererPlayerController();
    /** False after shutdown, including while a startup broadcast is still unwinding. */
    bool IsFrameworkActive() const;
    DECLARE_MULTICAST_DELEGATE_OneParam(FMapAudioEvent, EWandererUIAudioAction);
    FMapAudioEvent& GetMapAudioEvent()
    {
        return MapAudioEvent;
    }
    const FMapScanSnapshot* GetMapScanSnapshot() const;
    FMapPlayerSnapshot GetMapPlayerSnapshot() const;
    void CloseMap(EWandererUIAudioAction InAction);
    void RescanMap();
    void ClearMapDoor();
    bool TeleportSelectedMapDoor();
    const FText& GetMapTeleportMessage() const
    {
        return MapTeleportMessage;
    }
    bool IsMapInteractionReady() const;
    bool IsMapPresentationAvailable() const
    {
        return !bMapStopped;
    }
    UCharacterSelectionModel* GetCharacterSelectionModel() const;
    virtual void RefreshContactRegions() override;
    virtual FName ResolveSurfaceName(const FHitResult& InHit) const override;
    virtual FCharacterSurfaceSnapshot QueryContactSurface(
        const FHitResult* InHit, const FVector& InFeet) const override;
    virtual bool IsEntryLoading() const override;
    virtual bool AreEntryCharactersReady() const override;
    virtual void SetEntryMapScanEnabled(bool bInEnabled) override;
    virtual void StartEntryMapScan() override;
    virtual bool IsEntryMapReady() const override;
    virtual bool SetEntryLoadingState(bool bInLoading) override;
    virtual bool SetCharacterSelectionState(bool bInOpen) override;
    /** Character-anchored acoustics, with the current camera's listening orientation. */
    virtual void GetAudioListenerPosition(
        FVector& OutLocation, FVector& OutFrontDir, FVector& OutRightDir) const override;
    UFUNCTION(BlueprintPure, Category = "Wanderer|Audio")
    bool AreEarsUnderwater() const;
    UFUNCTION(BlueprintPure, Category = "Wanderer|Speaker")
    URadioControlModel* GetRadioControlModel() const
    {
        return RadioControlModel;
    }
    bool IsRadioInteractionReady() const;
    bool IsRadioHUDVisible() const;
    UFUNCTION(BlueprintCallable, Category = "Wanderer|Speaker")
    void SetSpeakerPanelOpen(bool bOpen);
    UFUNCTION(BlueprintCallable, Category = "Wanderer|Speaker")
    void ToggleItemInteraction();
    UFUNCTION(BlueprintPure, Category = "Wanderer|Loading")
    UWandererLoadingComponent* GetLoadingComponent() const
    {
        return LoadingComponent;
    }

    /** Legacy Blueprint access; native consumers use ICharacterSurfaceProvider. */
    UFUNCTION(BlueprintPure,
        Category = "Wanderer|Surface",
        meta = (DeprecatedFunction, DeprecationMessage = "Use the surface provider contract."))
    UWandererSurfaceComponent* GetSurfaceComponent() const
    {
        return SurfaceComponent;
    }
    UFUNCTION(BlueprintPure, Category = "Wanderer|Map")
    UWandererMapComponent* GetMapComponent() const
    {
        return MapComponent;
    }
    UFUNCTION(BlueprintCallable, Category = "Wanderer|Map")
    void SetMapOpen(bool bOpen);
    UFUNCTION(BlueprintCallable, Category = "Wanderer|Map")
    void ToggleMap();
    UFUNCTION(BlueprintPure, Category = "Wanderer|Map")
    bool IsMapOpen() const;

    /** Convenience gameplay gate; the stateless Teleport library also works without this
     * controller/map UI. */
    UFUNCTION(BlueprintCallable, Category = "Wanderer|Teleport")
    bool TeleportToLocation(FVector GroundLocation, FRotator Facing, FText& OutFailure);
    UFUNCTION(BlueprintCallable, Category = "Wanderer|Map")
    bool SelectMapDoor(FName ID);
    UFUNCTION(BlueprintPure, Category = "Wanderer|Map")
    FName GetSelectedMapDoor() const;
    UFUNCTION(BlueprintCallable, Category = "Wanderer|Map")
    bool TeleportToMapDoor(FName ID, FText& OutFailure);

    /** Non-owning camera lookup for migrated scene logic; resolves the current pawn each call. */
    UFUNCTION(BlueprintPure,
        Category = "Wanderer|Characters",
        meta = (WorldContext = "WorldContextObject"))
    static UCameraComponent* GetPlayerFollowCamera(const UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category = "Wanderer|Characters")
    bool OpenCharacterSelection();

    UFUNCTION(BlueprintCallable, Category = "Wanderer|Characters")
    bool CloseCharacterSelection();

    UFUNCTION(BlueprintCallable, Category = "Wanderer|Characters")
    bool ChooseCharacter(int32 Index);

    UFUNCTION(BlueprintCallable, Category = "Wanderer|Characters")
    UTextureRenderTarget2D* PreviewCharacter(int32 Index);
    const UWandererCharacterCatalog* GetCharacterCatalog() const
    {
        return CharacterCatalog;
    }
    int32 GetCurrentCharacterIndex() const;
    UFUNCTION(BlueprintPure, Category = "Wanderer|Characters")
    bool IsCharacterSelectionOpen() const;

  protected:
    virtual void PlayerTick(float DeltaSeconds) override;
    virtual void OnUnPossess() override;
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  private:
    UPROPERTY(EditDefaultsOnly, Category = "Wanderer|Audio", meta = (Units = "cm"))
    float EarHeightOffset = 8.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Wanderer|Audio", meta = (ClampMin = "1", Units = "cm"))
    float WaterlineHysteresis = 5.0f;

    UPROPERTY(
        EditDefaultsOnly, Category = "Wanderer|Audio", meta = (ClampMin = "0.05", Units = "s"))
    float WaterStateHoldTime = 0.25f;

    UPROPERTY(EditDefaultsOnly, Category = "Wanderer|Audio", meta = (ClampMin = "0.1", Units = "s"))
    float MinWaterStateInterval = 0.5f;

    UPROPERTY(VisibleAnywhere, Category = "Wanderer|Speaker")
    TObjectPtr<URadioControlModel> RadioControlModel;

    UPROPERTY(VisibleAnywhere, Category = "Wanderer|Loading")
    TObjectPtr<UWandererLoadingComponent> LoadingComponent;

    UPROPERTY(VisibleAnywhere, Category = "Wanderer|Surface")
    TObjectPtr<UWandererSurfaceComponent> SurfaceComponent;

    UPROPERTY(VisibleAnywhere, Category = "Wanderer|Map")
    TObjectPtr<UWandererMapComponent> MapComponent;

    UPROPERTY(EditDefaultsOnly, Category = "Wanderer|Characters")
    TObjectPtr<UWandererCharacterCatalog> CharacterCatalog;

    UPROPERTY(Transient)
    TObjectPtr<UCharacterSelectionModel> CharacterSelectionModel;

    UPROPERTY(Transient)
    TWeakObjectPtr<AWandererGameState> ObservedGameStateWeakPtr;

    UPROPERTY(Transient)
    TObjectPtr<UListenerAudioManager> ListenerAudioManager;

    bool bFrameworkStopped = false;
    bool bFeaturesStarted = false;
    FMapAudioEvent MapAudioEvent;
    float WaterSampleElapsedSec = 0.0f;
    bool bMapOpen = false;
    bool bMapStopped = false;
    FName SelectedMapDoor;
    FText MapTeleportMessage;

    bool CanApplyPlayState(EWandererPlayState InState) const;
    void BindPlayState();
    void StopFramework(EEndPlayReason::Type InReason);
    void NotifyFeaturesStopped();
    void PrepareInputTransition();
    void ApplyInputMode(EWandererPlayState InState);
    void PrepareLocalAudio();
    void UpdateUnderwaterAudio();
    void ResetUnderwaterAudio(bool bReleaseOwnership);
    void StartSpeakerPlacement();
    void ConfirmSpeakerPlacement();
    void ConfirmSpeakerPlacementWithMouse();
    void TogglePauseMenu();
    void SetMenuAudioDucking(EWandererPlayState PlayState);
    void RotateSpeakerPlacement();
    UFUNCTION()
    void ApplyPlayState(EWandererPlayState PreviousState, EWandererPlayState NewState);
};
