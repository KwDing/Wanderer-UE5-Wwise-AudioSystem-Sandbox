#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SurfaceQueryLibrary.generated.h"

class UPhysicalMaterial;

UCLASS()
class WANDERER_API USurfaceQueryLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Surface")
    static int32 GetNativeSurfaceType(UPhysicalMaterial* Material);
};
