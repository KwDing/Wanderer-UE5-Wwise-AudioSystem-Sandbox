#include "Framework/WandererPlayerController.h"

#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Framework/CharacterSelectionModel.h"
#include "Framework/WandererGameState.h"
#include "Framework/WandererLoadingComponent.h"
#include "Core/CharacterTeleportLibrary.h"
#include "Interfaces/CharacterPresentationProvider.h"

bool AWandererPlayerController::TeleportToLocation(
    FVector GroundLocation, FRotator Facing, FText& OutFailure)
{
    if (!IsFrameworkActive() || !IsLocalController() || LoadingComponent->IsLoading()
        || !ObservedGameStateWeakPtr.IsValid()
        || ObservedGameStateWeakPtr->GetPlayState() != EWandererPlayState::Normal)
    {
        OutFailure = FText::FromString(TEXT("Wait until the scene is ready before teleporting."));
        return false;
    }
    return UCharacterTeleportLibrary::TeleportCharacter(
        this, Cast<ACharacter>(GetPawn()), GroundLocation, Facing, OutFailure);
}

UCameraComponent* AWandererPlayerController::GetPlayerFollowCamera(
    const UObject* WorldContextObject)
{
    const ICharacterPresentationProvider* Character = Cast<ICharacterPresentationProvider>(
        UGameplayStatics::GetPlayerPawn(WorldContextObject, 0));
    return Character ? Character->GetCharacterCamera() : nullptr;
}

bool AWandererPlayerController::OpenCharacterSelection()
{
    return IsFrameworkActive() && IsValid(CharacterSelectionModel)
           && CharacterSelectionModel->Open();
}

bool AWandererPlayerController::CloseCharacterSelection()
{
    return IsFrameworkActive() && IsValid(CharacterSelectionModel)
           && CharacterSelectionModel->Close();
}

bool AWandererPlayerController::ChooseCharacter(int32 Index)
{
    return IsFrameworkActive() && IsValid(CharacterSelectionModel)
           && CharacterSelectionModel->Choose(Index);
}

UTextureRenderTarget2D* AWandererPlayerController::PreviewCharacter(int32 Index)
{
    if (!IsFrameworkActive() || !IsValid(CharacterSelectionModel))
    {
        return nullptr;
    }
    CharacterSelectionModel->Preview(Index);
    return IsFrameworkActive() ? CharacterSelectionModel->GetPortrait() : nullptr;
}

int32 AWandererPlayerController::GetCurrentCharacterIndex() const
{
    return CharacterSelectionModel->GetCurrentIndex();
}

bool AWandererPlayerController::IsCharacterSelectionOpen() const
{
    return IsFrameworkActive() && IsValid(CharacterSelectionModel)
           && CharacterSelectionModel->IsOpen();
}
