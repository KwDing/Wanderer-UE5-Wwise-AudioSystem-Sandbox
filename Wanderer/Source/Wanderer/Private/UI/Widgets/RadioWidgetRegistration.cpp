#include "UI/Widgets/WandererSpeakerWidget.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

#include "Core/PlayerFeatureBroadcaster.h"
#include "Framework/WandererPlayerController.h"

namespace
{
    class FRadioWidgetRegistration
    {
      public:
        FRadioWidgetRegistration()
        {
            StartedHandle = FPlayerFeatureBroadcaster::GetStartedEvent().AddRaw(
                this, &FRadioWidgetRegistration::OnStarted);
            StoppedHandle = FPlayerFeatureBroadcaster::GetStoppedEvent().AddRaw(
                this, &FRadioWidgetRegistration::OnStopped);
        }
        ~FRadioWidgetRegistration()
        {
            FPlayerFeatureBroadcaster::GetStartedEvent().Remove(StartedHandle);
            FPlayerFeatureBroadcaster::GetStoppedEvent().Remove(StoppedHandle);
        }

      private:
        FDelegateHandle StartedHandle;
        FDelegateHandle StoppedHandle;
        TMap<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<UWandererSpeakerWidget>> Widgets;

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
            UWandererSpeakerWidget* Widget = CreateWidget<UWandererSpeakerWidget>(&InPlayer);
            if (Widget && Player->IsFrameworkActive())
            {
                constexpr int32 InventoryOrder = 20;
                Widgets.Add(&InPlayer, Widget);
                Widget->AddToViewport(InventoryOrder);
                if (!Player->IsFrameworkActive())
                {
                    OnStopped(InPlayer);
                    Widget->RemoveFromParent();
                }
            }
        }
        void OnStopped(APlayerController& InPlayer)
        {
            TWeakObjectPtr<UWandererSpeakerWidget> WidgetWeakPtr;
            if (Widgets.RemoveAndCopyValue(&InPlayer, WidgetWeakPtr) && WidgetWeakPtr.IsValid())
            {
                WidgetWeakPtr->RemoveFromParent();
            }
        }
    };
    FRadioWidgetRegistration Registration;
} // namespace
