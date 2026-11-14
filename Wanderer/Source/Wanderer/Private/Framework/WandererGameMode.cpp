#include "Framework/WandererGameMode.h"

#include "GameFramework/Pawn.h"

#include "Core/Diagnostics/DiagnosticRecorder.h"
#include "Data/GameStartupSettings.h"
#include "Framework/WandererGameState.h"
#include "Framework/WandererPlayerController.h"

AWandererGameMode::AWandererGameMode()
{
    GameStateClass = AWandererGameState::StaticClass();
    PlayerControllerClass = AWandererPlayerController::StaticClass();
    UClass* ConfiguredPawnClass =
        GetDefault<UGameStartupSettings>()->DefaultPawnClassSoftPtr.LoadSynchronous();
    if (ConfiguredPawnClass && !ConfiguredPawnClass->HasAnyClassFlags(CLASS_Abstract))
    {
        DefaultPawnClass = ConfiguredPawnClass;
        return;
    }
    UE_LOG(LogWandererEvents, Warning,
        TEXT("Game Startup default pawn is unavailable; retaining the engine fallback."));
}
