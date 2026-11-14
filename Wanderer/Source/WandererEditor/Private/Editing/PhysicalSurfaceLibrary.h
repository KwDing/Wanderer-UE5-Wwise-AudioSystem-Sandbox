#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PhysicalSurfaceLibrary.generated.h"

class UMaterial;
class UPhysicalMaterial;
class UPhysicalMaterialMask;
class UStaticMesh;
class UTexture2D;

/**
 * Editor physical-surface authoring; callers own transactions and saving after successful
 * edits.
 */
UCLASS()
class UPhysicalSurfaceLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Surface|Editor")
    static int32 AddPhysicalMaskUV(
        UStaticMesh* Mesh, const FTransform& LocalToWorld, FVector2D Minimum, FVector2D Size);

    UFUNCTION(BlueprintCallable, Category = "Surface|Editor")
    static bool ConfigureLandscapePhysicalOutput(UMaterial* Material,
        UTexture2D* Texture,
        const TArray<UPhysicalMaterial*>& Materials,
        FVector2D Minimum,
        FVector2D Size);

    UFUNCTION(BlueprintCallable, Category = "Surface|Editor")
    static bool ConfigurePhysicalMask(UMaterial* Material,
        UPhysicalMaterialMask* Mask,
        UTexture2D* Texture,
        const TArray<UPhysicalMaterial*>& Materials,
        int32 UVChannel);

    UFUNCTION(BlueprintCallable, Category = "Surface|Editor")
    static void RefreshNativeSurfaceCollision(AActor* Actor);

    UFUNCTION(BlueprintCallable, Category = "Surface|Editor")
    static bool SetNativeSurfaceType(UPhysicalMaterial* Material, int32 SurfaceType);

    UFUNCTION(BlueprintCallable, Category = "Surface|Editor")
    static bool UpdateLandscapePhysicalSurfaces(AActor* LandscapeActor);
};
