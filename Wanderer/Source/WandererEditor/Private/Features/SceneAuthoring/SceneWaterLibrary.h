#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SceneWaterLibrary.generated.h"

class APhysicsVolume;
class UBlueprint;
class UMaterial;
class UMaterialExpression;
class UMaterialInterface;
class UNiagaraSystem;

/** Scene-specific water asset authoring; modifies only explicitly allowed assets.
 * Game-thread only. Successful edits may dirty packages; the caller controls saving.
 */
UCLASS()
class USceneWaterLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

  public:
    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static bool ConfigureIWSWaterBlueprint(UBlueprint* Blueprint);

    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static bool ConfigureSwimmingContactSpray(UNiagaraSystem* System);

    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static bool ConfigureTrialDropletRenderer(
        UNiagaraSystem* System, UMaterialInterface* DropletMaterial, float MeshScale = 0.12f);

    UFUNCTION(
        BlueprintCallable, Category = "Wanderer|Editor", meta = (WorldContext = "WorldContext"))
    static APhysicsVolume* CreateWaterVolume(UObject* WorldContext,
        const TArray<FVector2D>& Outline,
        const FVector& SurfaceLocation,
        float Depth);

    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static bool SetTrialBodyWaterlineOffset(UMaterial* Material, UMaterialExpression* Offset);
};
