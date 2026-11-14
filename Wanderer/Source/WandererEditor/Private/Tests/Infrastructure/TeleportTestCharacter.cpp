#include "Tests/Infrastructure/TeleportTestCharacter.h"

#include "GameFramework/CharacterMovementComponent.h"

bool ATeleportTestCharacter::TeleportTo(
    const FVector& DestLocation, const FRotator& DestRotation, bool bIsATest, bool bNoCheck)
{
    const bool bResult = Super::TeleportTo(DestLocation, DestRotation, bIsATest, bNoCheck);
    if (bResult && bDestroyMovementAfterTeleport)
    {
        GetCharacterMovement()->DestroyComponent();
    }
    if (bResult && bEndPlayAfterTeleport)
    {
        EndPlay(EEndPlayReason::RemovedFromWorld);
        GetCharacterMovement()->Velocity = FVector(123, 0, 0);
    }
    if (bResult && bDestroyAfterTeleport)
    {
        Destroy();
    }
    return bResult;
}
