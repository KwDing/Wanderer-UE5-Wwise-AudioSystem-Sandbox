#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Types/RadioControlTypes.h"

#include "WandererSpeakerPreviewActor.generated.h"

class UStaticMeshComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UStaticMesh;

/** Inventory-owned transient model studio; never a gameplay item or collision target. */
UCLASS()
class AWandererSpeakerPreviewActor : public AActor
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure, Category = "Speaker")
    UTextureRenderTarget2D* GetPreviewTexture(EWandererSpeakerItem Item) const;

    AWandererSpeakerPreviewActor();
    bool ShowItems(UStaticMesh* InRadioMesh, UStaticMesh* InMountedMesh);

private:
    UPROPERTY(VisibleAnywhere)
    TArray<TObjectPtr<UStaticMeshComponent>> Meshes;
    UPROPERTY(VisibleAnywhere)
    TArray<TObjectPtr<USceneCaptureComponent2D>> Captures;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextureRenderTarget2D>> Textures;
};
