#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SwimmingAnimLibrary.generated.h"

class UAnimSequence;
class UBlendSpace;
class UBlueprint;

UCLASS()
class USwimmingAnimLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static bool ConfigureSwimmingAnimations(
        UBlueprint* Blueprint, UBlendSpace* BlendSpace, UAnimSequence* Idle, UAnimSequence* Stroke);
};
