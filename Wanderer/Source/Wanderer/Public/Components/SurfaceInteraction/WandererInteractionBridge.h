#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "WandererInteractionBridge.generated.h"

class ACharacter;
class UActorComponent;

/**
 * Stateless adapter for localized vendor water/snow Blueprint contracts.
 * Legacy reflected name and parameter names are retained for existing Blueprint callers.
 * Game-thread only. Calls may synchronously run Blueprint code and destroy the caller.
 */
UCLASS()
class WANDERER_API UWandererInteractionBridge : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

  public:
    UFUNCTION(BlueprintCallable, Category = "Wanderer|Interaction")
    static bool ApplyIWSWaterImpact(
        AActor* WaterActor, const FVector& Location, float Strength = 0.08f, float Radius = 8.0f);

    UFUNCTION(BlueprintCallable, Category = "Wanderer|Interaction")
    static bool CreateIWSWaterSplash(
        AActor* WaterActor, const FVector& Location, float Power = 0.15f);

    /** Game thread only. Borrows actors; repairs the vendor's reflected character cache. */
    UFUNCTION(BlueprintCallable, Category = "Wanderer|Interaction")
    static bool RefreshWaterCharacter(AActor* WaterActor, ACharacter* Character);

    /** Calls the localized WaterInteract Blueprint's existing character-mesh event. */
    UFUNCTION(BlueprintCallable, Category = "Wanderer|Interaction")
    static bool RegisterWaterCharacterMesh(AActor* WaterActor, ACharacter* Character);

    UFUNCTION(BlueprintCallable, Category = "Wanderer|Interaction")
    static bool UnregisterWaterCharacterMesh(AActor* WaterActor, ACharacter* Character);

    /** Character owns the registered component; caller destroys it at EndPlay. */
    UFUNCTION(BlueprintCallable, Category = "Wanderer|Interaction")
    static UActorComponent* CreateSnowDisplacement(ACharacter* Character);
};
