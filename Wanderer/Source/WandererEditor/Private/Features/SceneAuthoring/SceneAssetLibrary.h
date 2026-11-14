#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Features/SceneAuthoring/SceneAuthoringTypes.h"
#include "SceneAssetLibrary.generated.h"

class UStaticMesh;

/** Scene asset authoring and constrained reference replacement.
 * Game-thread only. Successful edits may dirty packages; the caller controls saving.
 */
UCLASS()
class USceneAssetLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

  public:
    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static bool ConfigureTrialStepCollision(UStaticMesh* Mesh,
        EWandererStepCollisionMode Mode = EWandererStepCollisionMode::ModelSurface);

    UFUNCTION(
        BlueprintCallable, Category = "Wanderer|Editor", meta = (WorldContext = "WorldContext"))
    static AActor* CreatePCGVolume(UObject* WorldContext,
        const TArray<FVector2D>& Outline,
        const FVector& TopLocation,
        float Depth);

    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static bool ReplaceSceneAssetReferences(
        const TArray<UObject*>& SearchObjects, const TMap<UObject*, UObject*>& Replacements);
};
