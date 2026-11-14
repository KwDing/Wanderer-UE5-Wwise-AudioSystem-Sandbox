#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TeleportTestCharacter.generated.h"

UCLASS()
class ATeleportTestCharacter : public ACharacter
{
    GENERATED_BODY()
  public:
    bool bEndPlayAfterTeleport = false;
    bool bDestroyAfterTeleport = false;
    bool bDestroyMovementAfterTeleport = false;
    virtual bool TeleportTo(const FVector& DestLocation,
        const FRotator& DestRotation,
        bool bIsATest = false,
        bool bNoCheck = false) override;
};
