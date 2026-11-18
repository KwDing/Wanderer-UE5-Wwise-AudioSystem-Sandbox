#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Interfaces/RadioControlInterface.h"

#include "RadioInventoryActor.generated.h"

class AWandererPlayerController;
class AWandererSpeakerActor;
class AWandererSpeakerPreviewActor;
class UItemPlacementComponent;
class URadioControlModel;
class UStaticMesh;
class UTextureRenderTarget2D;
struct FStreamableHandle;
/** Local-player inventory. Owns device entities and delegates geometry/audio to lower layers. */
UCLASS()
class ARadioInventoryActor : public AActor, public IRadioControlInterface
{
    GENERATED_BODY()

public:
    ARadioInventoryActor();

    virtual bool ExecuteRadioRequest(const FRadioControlRequest& InRequest) override;
    virtual UTextureRenderTarget2D* GetRadioPreview(EWandererSpeakerItem InItem) const override;
    virtual FRadioControlSnapshot ReadRadioState() const override;
    virtual void SetRadioPreviewEnabled(bool bInEnabled) override;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type InEndPlayReason) override;
    virtual void Tick(float InDeltaTime) override;

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UItemPlacementComponent> Placement;

    UPROPERTY(Transient)
    TObjectPtr<AWandererSpeakerPreviewActor> PreviewStudio;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMesh> MountedMesh;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMesh> RadioMesh;

    TWeakObjectPtr<URadioControlModel> ModelWeakPtr;
    TWeakObjectPtr<AWandererSpeakerActor> DismissedSourceWeakPtr;
    TWeakObjectPtr<AWandererSpeakerActor> PreviewActorWeakPtr;
    TWeakObjectPtr<AWandererSpeakerActor> SelectedSourceWeakPtr;
    TArray<TWeakObjectPtr<AWandererSpeakerActor>> SourcesWeakPtrs;
    TMap<EWandererSpeakerItem, TArray<FGuid>> ReturnedDeviceIDs;
    TSharedPtr<FStreamableHandle> MeshRequest;
    FName PendingOption;
    FString Status;
    FVector LastPoint = FVector::ZeroVector;
    FVector LastNormal = FVector::UpVector;
    EWandererSpeakerItem PlacementItem = EWandererSpeakerItem::Automatic;
    float PlacementYaw = 0;
    bool bCursorOverPanel = false;
    bool bInteractionOpen = false;
    bool bPreviewFailed = false;
    bool bPreviewValid = false;
    bool bPublishingIntent = false;
    bool bShuttingDown = false;
    bool bPreviewRequested = false;
    bool bPlacementRequested = false;

    // Request validation and inventory ownership.
    bool ExecutePlacementRequest(const FRadioControlRequest& InRequest);
    bool ExecuteSelectionRequest(const FRadioControlRequest& InRequest);
    int32 GetAvailableCount(EWandererSpeakerItem InItem) const;
    AWandererPlayerController* GetController() const;
    TArray<AWandererSpeakerActor*> GetSources() const;
    bool IsGameplayReady() const;
    bool IsOwnedSource(const AWandererSpeakerActor* InSource) const;
    bool RetrieveAllSources();
    bool RetrieveSource(AWandererSpeakerActor* InSource);
    void ReturnDeviceToInventory(AWandererSpeakerActor& InSource);
    void SetCursorOverPanel(bool bInOverPanel)
    {
        bCursorOverPanel = bInOverPanel;
    }
    void SetInteractionOpen(bool bInOpen);
    bool IsInteractionOpen() const
    {
        return bInteractionOpen;
    }

    // Entity placement. Geometric queries live in ItemPlacementComponent.
    bool ApplySurface(const FHitResult& InHit);
    bool BeginItemPlacement(EWandererSpeakerItem InItem);
    bool BeginPlacement();
    bool PrepareSpeakerMeshes();
    void UpdateSpeakerMeshes();
    void CancelPlacement();
    bool ConfirmPlacement();
    bool ConfirmPlacementFromPointer();
    bool FindSurface(FVector InStart, FVector InEnd, FHitResult& OutHit) const;
    bool IsPlacing() const
    {
        return PreviewActorWeakPtr.IsValid();
    }
    bool PreviewSurface(FVector InSurfacePoint, FVector InSurfaceNormal);
    void RotatePreview();
    void UpdatePlacementPreview();
    bool UpdatePlacementTransform(const FHitResult& InHit, EWandererSpeakerItem InSurfaceItem);

    // Device selection and playback intent; no Wwise resource ownership here.
    void CancelSelection();
    bool CanRetrieveSource(const AWandererSpeakerActor* InSource) const;
    TArray<FRadioEmitterSnapshot> CollectEmitterSnapshots() const;
    int32 GetDeviceChannelMask(const AWandererSpeakerActor* InSource) const;
    int32 GetDevicePlaybackPositionMs(const AWandererSpeakerActor* InSource) const;
    FName GetRecorderTransportState() const;
    FName GetSelectedOption() const;
    bool IsSourceNearby(const AWandererSpeakerActor* InSource) const;
    void LogRecorderAction(
        FName InAction, FName InResult, FName InPreviousState, const FString& InDetail);
    bool PausePairPlayback();
    bool SelectOption(FName InID);
    bool SelectSource(AWandererSpeakerActor* InSource);
    bool SelectSourceAlongRay(FVector InStart, FVector InDirection);
    bool SelectSourceAtCursor();
    void SetSourceIntent(AWandererSpeakerActor& InOutSource, FName InContentID, bool bInPlaying);
    bool StartPairPlayback();
    void StopPairPlayback();
    bool TogglePlaybackRequest();
    void UpdateNearbySelection();
    bool UpdatePairPlayback(float InDeltaTime);
};
