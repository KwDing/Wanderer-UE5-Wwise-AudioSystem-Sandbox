#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AcousticQueryLibrary.generated.h"

UCLASS()
class WANDERER_API UAcousticQueryLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintPure, Category = "Audio")
    static FString GetAcousticPortalRooms(AActor* PortalActor);
};
