#include "Framework/WandererPlayerController.h"

#include "Engine/World.h"
#include "GameFramework/PlayerInput.h"
#include "UObject/StrongObjectPtr.h"

#include "Core/Diagnostics/DiagnosticRecorder.h"
#include "Core/PlayerFeatureBroadcaster.h"
#include "Data/WandererCharacterCatalog.h"
#include "Framework/CharacterSelectionModel.h"
#include "Framework/Mapping/WandererMapComponent.h"
#include "Framework/RadioControlModel.h"
#include "Framework/Surface/WandererSurfaceComponent.h"
#include "Framework/WandererGameState.h"
#include "Framework/WandererLoadingComponent.h"
#include "Interfaces/CharacterPresentationProvider.h"
#include "Systems/Audio/ListenerAudioManager.h"

AWandererPlayerController::AWandererPlayerController()
{
    SurfaceComponent = CreateDefaultSubobject<UWandererSurfaceComponent>(TEXT("SurfaceListener"));
    MapComponent = CreateDefaultSubobject<UWandererMapComponent>(TEXT("WalkableMap"));
    LoadingComponent = CreateDefaultSubobject<UWandererLoadingComponent>(TEXT("SceneLoading"));
    CharacterSelectionModel =
        CreateDefaultSubobject<UCharacterSelectionModel>(TEXT("CharacterSelectionModel"));
    RadioControlModel = CreateDefaultSubobject<URadioControlModel>(TEXT("RadioControlModel"));
    ListenerAudioManager =
        CreateDefaultSubobject<UListenerAudioManager>(TEXT("ListenerAudioManager"));
    InitializeAudioDefaults();
}

void AWandererPlayerController::OnUnPossess()
{
    if (IsFrameworkActive())
    {
        MapComponent->Rescan();
    }
    SelectedMapDoor = NAME_None;
    MapTeleportMessage = FText::GetEmpty();
    ResetUnderwaterAudio(false);
    Super::OnUnPossess();
}

void AWandererPlayerController::BeginPlay()
{
    TStrongObjectPtr<AWandererPlayerController> KeepAlive(this);
    Super::BeginPlay();
    if (!IsFrameworkActive() || !IsLocalController())
    {
        return;
    }
    ObservedGameStateWeakPtr = GetWorld()->GetGameState<AWandererGameState>();
    CharacterSelectionModel->Initialize(this, CharacterCatalog);
    if (!IsFrameworkActive())
    {
        return;
    }
    LoadingComponent->StartLoading();
    if (!IsFrameworkActive())
    {
        return;
    }
    bFeaturesStarted = true;
    FPlayerFeatureBroadcaster::BroadcastStarted(*this);
    if (!IsFrameworkActive())
    {
        return;
    }
    BindPlayState();
}

void AWandererPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (!bFrameworkStopped)
    {
        StopFramework(EndPlayReason);
    }
    Super::EndPlay(EndPlayReason);
}

bool AWandererPlayerController::IsFrameworkActive() const
{
    return !bFrameworkStopped && !IsActorBeingDestroyed() && IsValid(GetWorld())
           && !GetWorld()->bIsTearingDown;
}

bool AWandererPlayerController::CanApplyPlayState(EWandererPlayState InState) const
{
    return IsFrameworkActive() && ObservedGameStateWeakPtr.IsValid()
           && ObservedGameStateWeakPtr->GetWorld() == GetWorld()
           && ObservedGameStateWeakPtr->GetPlayState() == InState;
}

void AWandererPlayerController::BindPlayState()
{
    if (!ObservedGameStateWeakPtr.IsValid() || !CharacterCatalog)
    {
        UE_LOG(LogWandererEvents,
            Error,
            TEXT("Character selection requires a GameState and catalog on %s."),
            *GetNameSafe(this));
        return;
    }
    ObservedGameStateWeakPtr->OnPlayStateChanged.AddUniqueDynamic(
        this, &AWandererPlayerController::ApplyPlayState);
    ApplyPlayState(EWandererPlayState::Normal, ObservedGameStateWeakPtr->GetPlayState());
}

void AWandererPlayerController::StopFramework(EEndPlayReason::Type InReason)
{
    bFrameworkStopped = true;
    bMapStopped = true;
    bMapOpen = false;
    SelectedMapDoor = NAME_None;
    MapTeleportMessage = FText::GetEmpty();
    const TWeakObjectPtr<AWandererGameState> StateWeakPtr = ObservedGameStateWeakPtr;
    if (StateWeakPtr.IsValid())
    {
        StateWeakPtr->OnPlayStateChanged.RemoveDynamic(
            this, &AWandererPlayerController::ApplyPlayState);
    }
    ObservedGameStateWeakPtr.Reset();
    LoadingComponent->Stop();
    MapComponent->SetComponentTickEnabled(false);
    RadioControlModel->Stop();
    CharacterSelectionModel->Stop();
    ListenerAudioManager->Stop(MenuResumeEvent);
    MapAudioEvent.Clear();
    NotifyFeaturesStopped();
    if (StateWeakPtr.IsValid()
        && (InReason == EEndPlayReason::Destroyed || InReason == EEndPlayReason::RemovedFromWorld))
    {
        StateWeakPtr->RequestPlayerExitReset();
    }
}

void AWandererPlayerController::NotifyFeaturesStopped()
{
    if (!bFeaturesStarted)
    {
        return;
    }
    bFeaturesStarted = false;
    FPlayerFeatureBroadcaster::BroadcastStopped(*this);
}

void AWandererPlayerController::PrepareInputTransition()
{
    if (PlayerInput)
    {
        PlayerInput->FlushPressedKeys();
    }
    if (!IsFrameworkActive())
    {
        return;
    }
    if (ICharacterPresentationProvider* Presentation =
            Cast<ICharacterPresentationProvider>(GetPawn()))
    {
        Presentation->PrepareForMenu();
    }
}

void AWandererPlayerController::ApplyInputMode(EWandererPlayState InState)
{
    bShowMouseCursor = true;
    if (InState == EWandererPlayState::UI || InState == EWandererPlayState::LoadMap)
    {
        FInputModeUIOnly Mode;
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        SetInputMode(Mode);
        return;
    }
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(true);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);
}

void AWandererPlayerController::ApplyPlayState(
    EWandererPlayState PreviousState, EWandererPlayState NewState)
{
    if (!CanApplyPlayState(NewState))
    {
        return;
    }
    TStrongObjectPtr<AWandererPlayerController> KeepAlive(this);
    SetMenuAudioDucking(NewState);
    if (!CanApplyPlayState(NewState))
    {
        return;
    }
    const bool bPauseMenu = NewState == EWandererPlayState::PauseMenu;
    if (IsLocalController() && (bPauseMenu || PreviousState == EWandererPlayState::PauseMenu))
    {
        MapAudioEvent.Broadcast(
            bPauseMenu ? EWandererUIAudioAction::Open : EWandererUIAudioAction::Cancel);
    }
    if (!CanApplyPlayState(NewState))
    {
        return;
    }
    PrepareInputTransition();
    if (!CanApplyPlayState(NewState))
    {
        return;
    }
    ApplyInputMode(NewState);
    if (CanApplyPlayState(NewState))
    {
        CharacterSelectionModel->SetOpen(NewState == EWandererPlayState::UI);
    }
}
