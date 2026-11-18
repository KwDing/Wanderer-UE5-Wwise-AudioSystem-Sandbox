#include "UI/Mapping/WandererMapWidget.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

#include "Core/PlayerFeatureBroadcaster.h"
#include "Framework/WandererPlayerController.h"

namespace
{
    class FMapWidgetRegistration
    {
      public:
        FMapWidgetRegistration()
        {
            StartedHandle = FPlayerFeatureBroadcaster::GetStartedEvent().AddRaw(
                this, &FMapWidgetRegistration::OnStarted);
            StoppedHandle = FPlayerFeatureBroadcaster::GetStoppedEvent().AddRaw(
                this, &FMapWidgetRegistration::OnStopped);
        }
        ~FMapWidgetRegistration()
        {
            FPlayerFeatureBroadcaster::GetStartedEvent().Remove(StartedHandle);
            FPlayerFeatureBroadcaster::GetStoppedEvent().Remove(StoppedHandle);
        }

      private:
        FDelegateHandle StartedHandle;
        FDelegateHandle StoppedHandle;
        TMap<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<UWandererMapWidget>> Widgets;

        void OnStarted(APlayerController& InPlayer)
        {
            AWandererPlayerController* Player = Cast<AWandererPlayerController>(&InPlayer);
            if (!IsValid(Player) || !Player->IsMapPresentationAvailable()
                || !Player->IsLocalController())
            {
                return;
            }
            if (Widgets.FindRef(&InPlayer).IsValid())
            {
                return;
            }
            UWandererMapWidget* Widget = CreateWidget<UWandererMapWidget>(&InPlayer);
            if (!IsValid(Widget))
            {
                return;
            }
            // Track before mounting: native/Blueprint construction can synchronously stop the host.
            Widgets.Add(&InPlayer, Widget);
            if (!IsValid(Player) || !Player->IsMapPresentationAvailable())
            {
                OnStopped(InPlayer);
                return;
            }
            constexpr int32 MapOrder = 100;
            Widget->AddToViewport(MapOrder);
            if (!IsValid(Player) || !Player->IsMapPresentationAvailable())
            {
                Widgets.Remove(&InPlayer);
                Widget->RemoveFromParent();
            }
        }
        void OnStopped(APlayerController& InPlayer)
        {
            TWeakObjectPtr<UWandererMapWidget> WidgetWeakPtr;
            if (Widgets.RemoveAndCopyValue(&InPlayer, WidgetWeakPtr) && WidgetWeakPtr.IsValid())
            {
                WidgetWeakPtr->RemoveFromParent();
            }
        }
    };
    FMapWidgetRegistration MapWidgetRegistration;
} // namespace
