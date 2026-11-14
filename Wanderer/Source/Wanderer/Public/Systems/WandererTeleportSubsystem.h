#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "WandererTeleportSubsystem.generated.h"

class ACharacter;

/** Deprecated serialized Blueprint adapter; all placement logic lives in CharacterTeleportLibrary.
 */
UCLASS()
class WANDERER_API UWandererTeleportSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

  public:
    UFUNCTION(BlueprintPure,
        Category = "Wanderer|Teleport",
        meta = (WorldContext = "WorldContextObject",
            DeprecatedFunction,
            DeprecationMessage = "Use CharacterTeleportLibrary.TeleportCharacter."))
    static UWandererTeleportSubsystem* GetTeleportSubsystem(const UObject* WorldContextObject);

    /** Legacy pin names retained. Preflight failure never moves the character. */
    UFUNCTION(BlueprintCallable,
        Category = "Wanderer|Teleport",
        meta = (DeprecatedFunction,
            DeprecationMessage = "Use CharacterTeleportLibrary.TeleportCharacter."))
    bool TeleportCharacter(
        ACharacter* Character, FVector GroundLocation, FRotator Facing, FText& OutFailure);
};
