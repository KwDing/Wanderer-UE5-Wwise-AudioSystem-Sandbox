#include "UI/Characters/WandererCharacterSelectionWidget.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

#include "Core/PlayerFeatureBroadcaster.h"
#include "Framework/WandererPlayerController.h"

namespace
{
    class FCharacterWidgetRegistration
    {
      public:
        FCharacterWidgetRegistration()
        {
            StartedHandle = FPlayerFeatureBroadcaster::GetStartedEvent().AddRaw(
                this, &FCharacterWidgetRegistration::OnStarted);
            StoppedHandle = FPlayerFeatureBroadcaster::GetStoppedEvent().AddRaw(
                this, &FCharacterWidgetRegistration::OnStopped);
        }
        ~FCharacterWidgetRegistration()
        {
            FPlayerFeatureBroadcaster::GetStartedEvent().Remove(StartedHandle);
            FPlayerFeatureBroadcaster::GetStoppedEvent().Remove(StoppedHandle);
        }

      private:
        FDelegateHandle StartedHandle;
        FDelegateHandle StoppedHandle;
        TMap<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<UWandererCharacterSelectionWidget>>
            Widgets;

        void OnStarted(APlayerController& InPlayer)
        {
            AWandererPlayerController* Player = Cast<AWandererPlayerController>(&InPlayer);
            if (!Player || !Player->IsFrameworkActive() || !InPlayer.IsLocalController())
            {
                return;
            }
            if (Widgets.FindRef(&InPlayer).IsValid())
            {
                return;
            }
            UWandererCharacterSelectionWidget* Widget =
                CreateWidget<UWandererCharacterSelectionWidget>(&InPlayer);
            if (Widget && Player->IsFrameworkActive())
            {
                constexpr int32 SelectionOrder = 10;
                Widgets.Add(&InPlayer, Widget);
                Widget->AddToViewport(SelectionOrder);
                if (!Player->IsFrameworkActive())
                {
                    OnStopped(InPlayer);
                    Widget->RemoveFromParent();
                }
            }
        }
        void OnStopped(APlayerController& InPlayer)
        {
            TWeakObjectPtr<UWandererCharacterSelectionWidget> WidgetWeakPtr;
            if (Widgets.RemoveAndCopyValue(&InPlayer, WidgetWeakPtr) && WidgetWeakPtr.IsValid())
            {
                WidgetWeakPtr->RemoveFromParent();
            }
        }
    };
    FCharacterWidgetRegistration Registration;
} // namespace
