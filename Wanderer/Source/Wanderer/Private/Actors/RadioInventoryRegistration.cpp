#include "Actors/RadioInventoryActor.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

#include "Core/PlayerFeatureBroadcaster.h"
#include "Framework/WandererPlayerController.h"

namespace
{
    class FRadioInventoryRegistration
    {
      public:
        FRadioInventoryRegistration()
        {
            StartedHandle = FPlayerFeatureBroadcaster::GetStartedEvent().AddRaw(
                this, &FRadioInventoryRegistration::OnStarted);
            StoppedHandle = FPlayerFeatureBroadcaster::GetStoppedEvent().AddRaw(
                this, &FRadioInventoryRegistration::OnStopped);
        }
        ~FRadioInventoryRegistration()
        {
            FPlayerFeatureBroadcaster::GetStartedEvent().Remove(StartedHandle);
            FPlayerFeatureBroadcaster::GetStoppedEvent().Remove(StoppedHandle);
        }

      private:
        FDelegateHandle StartedHandle;
        FDelegateHandle StoppedHandle;
        TMap<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<ARadioInventoryActor>> Inventories;

        void OnStarted(APlayerController& InPlayer)
        {
            AWandererPlayerController* Player = Cast<AWandererPlayerController>(&InPlayer);
            if (!Player || !Player->IsFrameworkActive() || !InPlayer.IsLocalController())
            {
                return;
            }
            if (Inventories.FindRef(&InPlayer).IsValid())
            {
                return;
            }
            FActorSpawnParameters Parameters;
            Parameters.Owner = &InPlayer;
            Parameters.ObjectFlags |= RF_Transient;
            ARadioInventoryActor* Inventory =
                InPlayer.GetWorld()->SpawnActor<ARadioInventoryActor>(Parameters);
            if (!IsValid(Inventory))
            {
                return;
            }
            if (!Player->IsFrameworkActive())
            {
                Inventory->Destroy();
                return;
            }
            Inventories.Add(&InPlayer, Inventory);
        }
        void OnStopped(APlayerController& InPlayer)
        {
            TWeakObjectPtr<ARadioInventoryActor> InventoryWeakPtr;
            if (Inventories.RemoveAndCopyValue(&InPlayer, InventoryWeakPtr)
                && InventoryWeakPtr.IsValid())
            {
                InventoryWeakPtr->Destroy();
            }
        }
    };
    FRadioInventoryRegistration Registration;
} // namespace
