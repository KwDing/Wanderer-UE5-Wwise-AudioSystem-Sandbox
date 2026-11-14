#pragma once
#include "GameFramework/PlayerController.h"
#include "Interfaces/CharacterPreviewProvider.h"
#include "Interfaces/SceneEntryProvider.h"
#include "CharacterFlowTestActors.generated.h"
class UCharacterSelectionModel;
class UTextureRenderTarget2D;

UCLASS(Transient, NotBlueprintable)
class ACharacterFlowTestHost : public APlayerController, public ISceneEntryProvider
{
    GENERATED_BODY()
  public:
    UPROPERTY()
    TObjectPtr<UCharacterSelectionModel> Model;
    bool bLoading = false;
    bool bRejectClose = false;
    bool bStopOnClose = false;
    UPROPERTY()
    TObjectPtr<APlayerController> TransferTo;
    UPROPERTY()
    TObjectPtr<APawn> TransferPawn;
    bool bRejectCandidate = false;
    bool bMapScanEnabled = true;
    virtual bool IsEntryLoading() const override;
    virtual bool AreEntryCharactersReady() const override;
    virtual void SetEntryMapScanEnabled(bool bInEnabled) override;
    virtual void StartEntryMapScan() override;
    virtual bool IsEntryMapReady() const override;
    virtual bool SetEntryLoadingState(bool bInLoading) override;
    virtual bool SetCharacterSelectionState(bool bInOpen) override;

  protected:
    virtual void OnPossess(APawn* InPawn) override;
};
UCLASS(Transient, NotBlueprintable)
class ACharacterPreviewTestActor : public AActor, public ICharacterPreviewProvider
{
    GENERATED_BODY()
  public:
    UPROPERTY()
    TObjectPtr<UTextureRenderTarget2D> Portrait;
    TWeakObjectPtr<UCharacterSelectionModel> ModelWeakPtr;
    bool bStopDuringSpawn = false;
    bool bStopDuringPreview = false;
    bool bActive = false;
    virtual UTextureRenderTarget2D* ShowPreview(
        UClass* InClass, UAnimationAsset* InAnimation) override;
    virtual void SetPreviewActive(bool bInActive) override;
    virtual APawn* SpawnCandidate(UClass* InClass, const FTransform& InTransform) override;
    virtual bool IsClassReady(UClass* InClass) const override;
    virtual bool IsPawnReady(const APawn* InPawn) const override;
};
