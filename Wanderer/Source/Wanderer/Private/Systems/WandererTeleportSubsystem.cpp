#include "Systems/WandererTeleportSubsystem.h"

#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"

#include "Core/CharacterTeleportLibrary.h"

UWandererTeleportSubsystem* UWandererTeleportSubsystem::GetTeleportSubsystem(
    const UObject* WorldContextObject)
{
    if (!IsInGameThread())
    {
        return nullptr;
    }
    UGameInstance* Instance = IsValid(WorldContextObject)
                                  ? UGameplayStatics::GetGameInstance(WorldContextObject)
                                  : nullptr;
    return Instance ? Instance->GetSubsystem<UWandererTeleportSubsystem>() : nullptr;
}

bool UWandererTeleportSubsystem::TeleportCharacter(
    ACharacter* Character, FVector GroundLocation, FRotator Facing, FText& OutFailure)
{
    return UCharacterTeleportLibrary::TeleportCharacter(
        this, Character, GroundLocation, Facing, OutFailure);
}
