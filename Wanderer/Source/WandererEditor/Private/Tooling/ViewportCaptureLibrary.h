#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ViewportCaptureLibrary.generated.h"

/** Queues an asynchronous viewport screenshot; return does not imply the file has been written. */
UCLASS()
class UViewportCaptureLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static void CaptureViewport(const FString& Filename);
};
