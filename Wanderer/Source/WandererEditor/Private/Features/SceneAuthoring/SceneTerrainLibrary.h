#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SceneTerrainLibrary.generated.h"

class UMaterialInterface;

/** Scene-specific terrain authoring; validates allowed samples before editing height data.
 * Game-thread only. Successful edits may dirty packages; the caller controls saving.
 */
UCLASS()
class USceneTerrainLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

  public:
    UFUNCTION(
        BlueprintCallable, Category = "Wanderer|Editor", meta = (WorldContext = "WorldContext"))
    static AActor* CreateProceduralLandscape(UObject* WorldContext,
        UMaterialInterface* Material,
        const FVector& Origin,
        int32 ComponentCountX,
        int32 ComponentCountY,
        int32 QuadsPerSection,
        float XYScale,
        float ZScale);

    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static bool LowerTrialFountainTerrain(AActor* Terrain, const TArray<FVector>& WorldSamples);

    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static bool RefreshTrialTerrainMaterial(AActor* Terrain, UMaterialInterface* Material);

    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static bool RegradeTrialLakeTerrain(AActor* Terrain, const TArray<FVector>& WorldSamples);
};
