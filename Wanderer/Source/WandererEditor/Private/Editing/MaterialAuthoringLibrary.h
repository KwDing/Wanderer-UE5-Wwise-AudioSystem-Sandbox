#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MaterialAuthoringLibrary.generated.h"

class UMaterialExpression;
class UMaterialFunctionInterface;

/** Game-thread material graph edits; callers own transactions, recompilation and saving. */
UCLASS()
class UMaterialAuthoringLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static bool ConnectMaterialAttributes(UMaterialExpression* Expression, FName OutputName);

    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static TArray<FString> GetMaterialFunctionOutputs(UMaterialFunctionInterface* Function);
};
