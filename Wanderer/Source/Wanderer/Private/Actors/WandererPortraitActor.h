#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WandererPortraitActor.generated.h"

class USkeletalMeshComponent;
class UAnimationAsset;
class USceneCaptureComponent2D;
class UPointLightComponent;
class UTextureRenderTarget2D;
struct FWandererCharacterEntry;
struct FWandererAppearancePart;

UCLASS(NotBlueprintable, Transient)
class WANDERER_API AWandererPortraitActor : public AActor
{
    GENERATED_BODY()

  public:
    AWandererPortraitActor();
    bool ShowCharacter(const FWandererCharacterEntry& Entry);
    virtual void SetPreviewActive(bool bActive);
    virtual UTextureRenderTarget2D* ShowPreview(UClass* InClass, UAnimationAsset* InAnimation);
    UTextureRenderTarget2D* GetPortrait() const
    {
        return Portrait;
    }

  protected:
    virtual void EndPlay(const EEndPlayReason::Type InReason) override;

  private:
    void SetAppearanceParts(const TArray<FWandererAppearancePart>& Parts);

    UPROPERTY()
    TObjectPtr<USkeletalMeshComponent> PreviewMesh;

    UPROPERTY(Transient)
    TArray<TObjectPtr<USkeletalMeshComponent>> AppearanceComponents;

    UPROPERTY()
    TObjectPtr<USceneCaptureComponent2D> Capture;

    UPROPERTY()
    TObjectPtr<UPointLightComponent> KeyLight;

    UPROPERTY()
    TObjectPtr<UPointLightComponent> FillLight;

    UPROPERTY(Transient)
    TObjectPtr<UTextureRenderTarget2D> Portrait;
};
