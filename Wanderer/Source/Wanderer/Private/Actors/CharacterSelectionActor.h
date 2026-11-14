#pragma once
#include "GameFramework/Actor.h"
#include "Interfaces/CharacterPreviewProvider.h"
#include "CharacterSelectionActor.generated.h"
class AWandererPortraitActor;
class UCharacterSelectionModel;

/** L5 selection adapter owns its preview renderer and creates configured candidates. */
UCLASS(Transient, NotBlueprintable)
class ACharacterSelectionActor : public AActor, public ICharacterPreviewProvider
{
    GENERATED_BODY()
  public:
    virtual UTextureRenderTarget2D* ShowPreview(
        UClass* InClass, UAnimationAsset* InAnimation) override;
    virtual void SetPreviewActive(bool bInActive) override;
    virtual APawn* SpawnCandidate(UClass* InClass, const FTransform& InTransform) override;
    virtual bool IsClassReady(UClass* InClass) const override;
    virtual bool IsPawnReady(const APawn* InPawn) const override;
    bool BindSelectionModel(UCharacterSelectionModel* InModel);

  protected:
    virtual void EndPlay(const EEndPlayReason::Type InReason) override;

  private:
    UPROPERTY(Transient)
    TObjectPtr<AWandererPortraitActor> PortraitActor;
    TWeakObjectPtr<UCharacterSelectionModel> ModelWeakPtr;
};
