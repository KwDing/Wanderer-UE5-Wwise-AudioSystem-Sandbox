#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CharacterTeleportLibrary.generated.h"

class ACharacter;

/** Stateless grounded placement. The caller owns gameplay admission and network authority. */
UCLASS()
class WANDERER_API UCharacterTeleportLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
  public:
    /** Game thread only; context and character must share a live world.
     * GroundLocation is a floor hint, not capsule center. Preflight failure never moves the actor.
     * Success means TeleportTo accepted placement, even if synchronous callbacks destroy the actor.
     */
    UFUNCTION(
        BlueprintCallable, Category = "Wanderer|Teleport", meta = (WorldContext = "InWorldContext"))
    static bool TeleportCharacter(const UObject* InWorldContext,
        ACharacter* InCharacter,
        FVector InGroundLocation,
        FRotator InFacing,
        FText& OutFailure);
};
