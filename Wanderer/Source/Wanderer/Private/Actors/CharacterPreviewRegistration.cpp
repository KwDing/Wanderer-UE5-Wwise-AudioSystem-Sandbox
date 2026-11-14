#include "Actors/CharacterSelectionActor.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

#include "Core/PlayerFeatureBroadcaster.h"
#include "Framework/WandererPlayerController.h"
#include "Framework/CharacterSelectionModel.h"

namespace
{
    class FCharacterPreviewRegistration
    {
      public:
        FCharacterPreviewRegistration()
        {
            StartedHandle = FPlayerFeatureBroadcaster::GetStartedEvent().AddRaw(
                this, &FCharacterPreviewRegistration::OnStarted);
            StoppedHandle = FPlayerFeatureBroadcaster::GetStoppedEvent().AddRaw(
                this, &FCharacterPreviewRegistration::OnStopped);
        }
        ~FCharacterPreviewRegistration()
        {
            FPlayerFeatureBroadcaster::GetStartedEvent().Remove(StartedHandle);
            FPlayerFeatureBroadcaster::GetStoppedEvent().Remove(StoppedHandle);
        }

      private:
        FDelegateHandle StartedHandle;
        FDelegateHandle StoppedHandle;
        TMap<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<ACharacterSelectionActor>> Previews;

        void OnStarted(APlayerController& InPlayer)
        {
            AWandererPlayerController* Player = Cast<AWandererPlayerController>(&InPlayer);
            if (!Player || !Player->IsFrameworkActive() || !InPlayer.IsLocalController())
            {
                return;
            }
            if (Previews.FindRef(&InPlayer).IsValid())
            {
                return;
            }
            FActorSpawnParameters Parameters;
            Parameters.Owner = &InPlayer;
            Parameters.ObjectFlags |= RF_Transient;
            ACharacterSelectionActor* Preview =
                InPlayer.GetWorld()->SpawnActor<ACharacterSelectionActor>(
                    FVector::ZeroVector, FRotator::ZeroRotator, Parameters);
            if (IsValid(Preview) && Player->IsFrameworkActive()
                && Preview->BindSelectionModel(Player->GetCharacterSelectionModel())
                && IsValid(Preview) && Player->IsFrameworkActive())
            {
                Previews.Add(&InPlayer, Preview);
            }
            else if (Preview)
            {
                Preview->Destroy();
            }
        }
        void OnStopped(APlayerController& InPlayer)
        {
            TWeakObjectPtr<ACharacterSelectionActor> PreviewWeakPtr;
            if (Previews.RemoveAndCopyValue(&InPlayer, PreviewWeakPtr) && PreviewWeakPtr.IsValid())
            {
                PreviewWeakPtr->Destroy();
            }
        }
    };
    FCharacterPreviewRegistration Registration;
} // namespace
