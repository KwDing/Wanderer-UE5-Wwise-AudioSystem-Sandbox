#pragma once
#include "UObject/Interface.h"
#include "CharacterPreviewProvider.generated.h"
class APawn;
class UAnimationAsset;
class UTextureRenderTarget2D;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UCharacterPreviewProvider : public UInterface
{
    GENERATED_BODY()
};
/** L5 executes entity/mesh work; callers only hold a weak provider identity. */
class WANDERER_API ICharacterPreviewProvider
{
    GENERATED_BODY()
  public:
    virtual UTextureRenderTarget2D* ShowPreview(UClass* InClass, UAnimationAsset* InAnimation) = 0;
    virtual void SetPreviewActive(bool bInActive) = 0;
    virtual APawn* SpawnCandidate(UClass* InClass, const FTransform& InTransform) = 0;
    virtual bool IsClassReady(UClass* InClass) const = 0;
    virtual bool IsPawnReady(const APawn* InPawn) const = 0;
};
