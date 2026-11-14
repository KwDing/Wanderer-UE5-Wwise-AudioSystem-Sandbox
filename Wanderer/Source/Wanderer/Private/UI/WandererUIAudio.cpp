#include "UI/WandererUIAudio.h"

#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

#include "Systems/Audio/WandererUIAudioSubsystem.h"

int32 UWandererUIAudio::PlayUIAction(UUserWidget* Widget, EWandererUIAudioAction Action)
{
    if (!IsInGameThread() || !IsValid(Widget) || Action == EWandererUIAudioAction::Silent)
    {
        return 0;
    }
    UWorld* World = Widget->GetWorld();
    const APlayerController* Player = Widget->GetOwningPlayer();
    if (!World || !IsValid(Player) || Player->GetWorld() != World || !Player->IsLocalController())
    {
        return 0;
    }
    UWandererUIAudioSubsystem* Audio =
        UGameInstance::GetSubsystem<UWandererUIAudioSubsystem>(Widget->GetGameInstance());
    return Audio ? Audio->PlayAction(World, Widget->GetClass(), Action) : 0;
}
