#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AssetMaintenanceLibrary.generated.h"

class UBlueprint;

/**
 * Editor asset utilities. Redirector fixup may save/delete assets; callers must explicitly
 * authorize it.
 */
UCLASS()
class UAssetMaintenanceLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static bool ExportBlueprintGraphs(UBlueprint* Blueprint, const FString& Filename);

    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static bool FixRedirectors(const FString& Directory);

    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static void WaitForAssetCompilation();
};
