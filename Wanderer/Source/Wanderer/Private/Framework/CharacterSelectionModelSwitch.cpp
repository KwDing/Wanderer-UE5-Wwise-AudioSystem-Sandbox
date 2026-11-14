#include "Framework/CharacterSelectionModel.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

#include "Interfaces/CharacterPreviewProvider.h"

bool UCharacterSelectionModel::SwitchPawn(UClass* InClass)
{
    const TWeakObjectPtr<APlayerController> OriginalPlayerWeakPtr = PlayerWeakPtr;
    const TWeakObjectPtr<APawn> PreviousWeakPtr = OriginalPlayerWeakPtr->GetPawn();
    const TWeakObjectPtr<UWorld> WorldWeakPtr = OriginalPlayerWeakPtr->GetWorld();
    if (!PreviousWeakPtr.IsValid())
    {
        return false;
    }
    if (PreviousWeakPtr->GetClass() == InClass)
    {
        return Close();
    }
    const FRotator View = OriginalPlayerWeakPtr->GetControlRotation();
    ICharacterPreviewProvider* Provider = Cast<ICharacterPreviewProvider>(ProviderWeakPtr.Get());
    const TWeakObjectPtr<APawn> CandidateWeakPtr =
        Provider ? Provider->SpawnCandidate(InClass, PreviousWeakPtr->GetActorTransform())
                 : nullptr;
    if (!CandidateWeakPtr.IsValid())
    {
        return false;
    }
    if (CandidateWeakPtr.Get() == PreviousWeakPtr.Get() || CandidateWeakPtr->GetController()
        || CandidateWeakPtr->GetWorld() != WorldWeakPtr.Get() || !CanContinue()
        || !PreviousWeakPtr.IsValid() || !OriginalPlayerWeakPtr.IsValid()
        || OriginalPlayerWeakPtr->GetWorld() != WorldWeakPtr.Get()
        || OriginalPlayerWeakPtr->GetPawn() != PreviousWeakPtr.Get())
    {
        if (CandidateWeakPtr.Get() != PreviousWeakPtr.Get()
            && CandidateWeakPtr->GetWorld() == WorldWeakPtr.Get()
            && !CandidateWeakPtr->GetController())
        {
            CandidateWeakPtr->Destroy();
        }
        return false;
    }
    OriginalPlayerWeakPtr->Possess(CandidateWeakPtr.Get());
    if (!CanContinue() || !OriginalPlayerWeakPtr.IsValid() || !CandidateWeakPtr.IsValid()
        || OriginalPlayerWeakPtr->GetPawn() != CandidateWeakPtr.Get())
    {
        RollbackPawn(OriginalPlayerWeakPtr, WorldWeakPtr, PreviousWeakPtr, CandidateWeakPtr, View);
        return false;
    }
    OriginalPlayerWeakPtr->SetControlRotation(View);
    const bool bClosed = Close();
    if (!bClosed || bStopped || !OriginalPlayerWeakPtr.IsValid() || !CandidateWeakPtr.IsValid()
        || OriginalPlayerWeakPtr->GetPawn() != CandidateWeakPtr.Get())
    {
        RollbackPawn(OriginalPlayerWeakPtr, WorldWeakPtr, PreviousWeakPtr, CandidateWeakPtr, View);
        return false;
    }
    if (PreviousWeakPtr.IsValid() && PreviousWeakPtr->GetWorld() == WorldWeakPtr.Get()
        && !PreviousWeakPtr->GetController())
    {
        PreviousWeakPtr->Destroy();
    }
    return true;
}

void UCharacterSelectionModel::RollbackPawn(const TWeakObjectPtr<APlayerController>& InPlayer,
    const TWeakObjectPtr<UWorld>& InWorld,
    const TWeakObjectPtr<APawn>& InPrevious,
    const TWeakObjectPtr<APawn>& InCandidate,
    const FRotator& InView)
{
    APlayerController* Player = InPlayer.Get();
    const bool bCanRestore =
        Player && InWorld.IsValid() && Player->GetWorld() == InWorld.Get() && InPrevious.IsValid()
        && InPrevious->GetWorld() == InWorld.Get()
        && (!InPrevious->GetController() || InPrevious->GetController() == Player)
        && (!Player->GetPawn() || Player->GetPawn() == InCandidate.Get());
    if (bCanRestore)
    {
        Player->Possess(InPrevious.Get());
        if (InPlayer.IsValid())
        {
            InPlayer->SetControlRotation(InView);
        }
    }
    if (!InCandidate.IsValid() || InCandidate->GetWorld() != InWorld.Get())
    {
        return;
    }
    if (!InCandidate->GetController())
    {
        InCandidate->Destroy();
        return;
    }
    if (InCandidate->GetController() == InPlayer.Get())
    {
        UE_LOG(LogTemp,
            Error,
            TEXT(
                "Character rollback failed; retaining the original controller's "
                "possessed candidate."));
    }
    // Another controller taking ownership is not permission to steal or destroy its pawn.
}
