#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CharacterSelectionModel.generated.h"
class APlayerController;
class APawn;
class UWandererCharacterCatalog;
class UTextureRenderTarget2D;
struct FWandererCharacterEntry;

/** Controller-lifetime selection state. It never owns or includes a concrete pawn/view. */
UCLASS()
class WANDERER_API UCharacterSelectionModel : public UObject
{
    GENERATED_BODY()
  public:
    DECLARE_MULTICAST_DELEGATE(FChanged);
    FChanged OnChanged;
    void Initialize(APlayerController* InPlayer, UWandererCharacterCatalog* InCatalog);
    void Stop();
    bool RegisterProvider(UObject* InProvider);
    void UnregisterProvider(UObject* InProvider);
    bool Open();
    bool Close();
    void SetOpen(bool bInOpen);
    bool Preview(int32 InIndex);
    bool Choose(int32 InIndex);
    bool AreResourcesReady() const;
    int32 GetCurrentIndex() const;
    int32 GetOptionCount() const;
    FText GetOptionName(int32 InIndex) const;
    bool IsOptionEnabled(int32 InIndex) const;
    bool IsOpen() const;
    bool CanChoose() const;
    int32 GetSelectedIndex() const;
    FText GetStatus() const;
    UTextureRenderTarget2D* GetPortrait() const;

  private:
    UPROPERTY(Transient)
    TObjectPtr<UWandererCharacterCatalog> Catalog;
    UPROPERTY(Transient)
    TWeakObjectPtr<APlayerController> PlayerWeakPtr;
    UPROPERTY(Transient)
    TWeakObjectPtr<UObject> ProviderWeakPtr;
    UPROPERTY(Transient)
    TObjectPtr<UTextureRenderTarget2D> Portrait;
    bool bStopped = true;
    bool bOpen = false;
    bool bChoosing = false;
    int32 SelectedIndex = INDEX_NONE;
    FText Status;
    const FWandererCharacterEntry* FindOption(int32 InIndex) const;
    bool CanContinue() const;
    bool SwitchPawn(UClass* InClass);
    void RollbackPawn(const TWeakObjectPtr<APlayerController>& InPlayer,
        const TWeakObjectPtr<UWorld>& InWorld,
        const TWeakObjectPtr<APawn>& InPrevious,
        const TWeakObjectPtr<APawn>& InCandidate,
        const FRotator& InView);
};
