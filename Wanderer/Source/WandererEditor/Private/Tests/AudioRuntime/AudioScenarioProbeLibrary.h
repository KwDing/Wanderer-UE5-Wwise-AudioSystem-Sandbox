#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "AudioScenarioProbeLibrary.generated.h"

/** Editor test observation only; never creates emitters, loads Events or changes scheduling. */
UCLASS()
class UAudioScenarioProbeLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

  public:
    /** Game thread, live PIE world. Empty output means the listener or audio system is unavailable.
     */
    UFUNCTION(BlueprintCallable, Category = "Tests|Audio")
    static FString CaptureState(UWorld* InWorld);
};
