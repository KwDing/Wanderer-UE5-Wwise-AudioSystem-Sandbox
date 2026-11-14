#include "Framework/WandererGameState.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/StrongObjectPtr.h"

#include "Interfaces/SceneEntryProvider.h"
#include "Core/Diagnostics/DiagnosticRecorder.h"

bool AWandererGameState::SetPlayState(EWandererPlayState NewState)
{
    if (!IsInGameThread() || bStopped || !HasAuthority() || bChangingState || !IsValid(GetWorld())
        || GetWorld()->bIsTearingDown)
    {
        return false;
    }
    if (NewState != EWandererPlayState::Normal && NewState != EWandererPlayState::UI
        && NewState != EWandererPlayState::LoadMap && NewState != EWandererPlayState::PauseMenu)
    {
        return false;
    }
    if (PlayState == NewState)
    {
        return true;
    }
    TStrongObjectPtr<AWandererGameState> KeepAlive(this);
    TGuardValue<bool> TransitionGuard(bChangingState, true);
    const ISceneEntryProvider* Player =
        Cast<ISceneEntryProvider>(UGameplayStatics::GetPlayerController(this, 0));
    if (NewState != EWandererPlayState::LoadMap && Player && Player->IsEntryLoading())
    {
        return false;
    }
    if (bStopped
        || !ApplyPauseOwnership(
            NewState == EWandererPlayState::UI || NewState == EWandererPlayState::PauseMenu))
    {
        return false;
    }
    const EWandererPlayState PreviousState = PlayState;
    PlayState = NewState;
    OnPlayStateChanged.Broadcast(PreviousState, PlayState);
    CompletePlayerExitReset();
    return !bStopped && PlayState == NewState;
}

void AWandererGameState::RequestPlayerExitReset()
{
    if (!IsInGameThread() || bStopped || !HasAuthority())
    {
        return;
    }
    if (bChangingState)
    {
        bResetAfterTransition = true;
        return;
    }
    SetPlayState(EWandererPlayState::Normal);
}

void AWandererGameState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    bStopped = true;
    OnPlayStateChanged.Clear();
    const bool bReleasePause = bOwnsPause;
    bOwnsPause = false;
    if (bReleasePause && IsValid(GetWorld()) && !GetWorld()->bIsTearingDown)
    {
        UGameplayStatics::SetGamePaused(this, false);
    }
    Super::EndPlay(EndPlayReason);
}

bool AWandererGameState::ApplyPauseOwnership(bool bInRequiresPause)
{
    const bool bAcquirePause = bInRequiresPause && !UGameplayStatics::IsGamePaused(this);
    const bool bReleasePause = !bInRequiresPause && bOwnsPause;
    if (!bAcquirePause && !bReleasePause)
    {
        return !bStopped;
    }
    const TWeakObjectPtr<UWorld> WorldWeakPtr = GetWorld();
    if (!UGameplayStatics::SetGamePaused(this, bAcquirePause))
    {
        return false;
    }
    // GameMode pause callbacks may end this state before SetGamePaused returns.
    if (bStopped || !WorldWeakPtr.IsValid() || WorldWeakPtr.Get() != GetWorld()
        || WorldWeakPtr->bIsTearingDown)
    {
        if (bAcquirePause && WorldWeakPtr.IsValid() && !WorldWeakPtr->bIsTearingDown)
        {
            UGameplayStatics::SetGamePaused(WorldWeakPtr.Get(), false);
        }
        return false;
    }
    bOwnsPause = bAcquirePause;
    return true;
}

void AWandererGameState::CompletePlayerExitReset()
{
    if (bStopped || !bResetAfterTransition)
    {
        return;
    }
    bResetAfterTransition = false;
    if (!ApplyPauseOwnership(false))
    {
        UE_LOG(LogWandererEvents,
            Warning,
            TEXT("Cannot release the local session pause during player teardown."));
        return;
    }
    const EWandererPlayState PreviousState = PlayState;
    PlayState = EWandererPlayState::Normal;
    if (PreviousState != PlayState)
    {
        OnPlayStateChanged.Broadcast(PreviousState, PlayState);
    }
    // Repeated reset requests during the Normal notification are already satisfied.
    bResetAfterTransition = false;
}
