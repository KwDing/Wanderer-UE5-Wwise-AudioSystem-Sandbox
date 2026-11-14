#pragma once

#include "CoreMinimal.h"

#include "GameStateTypes.generated.h"

/** Game flow modes shared by loading, menus and player input; not audio playback state. */
UENUM(BlueprintType)
enum class EWandererPlayState : uint8
{
    Normal,
    UI,
    LoadMap UMETA(DisplayName = "loadMap"),
    PauseMenu
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FWandererPlayStateChanged, EWandererPlayState, PreviousState, EWandererPlayState, NewState);
