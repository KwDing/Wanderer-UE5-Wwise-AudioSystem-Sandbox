#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Types/Audio/UIAudioTypes.h"
#include "WandererUIAudio.generated.h"

class UUserWidget;

/**
 * Legacy Blueprint adapter: validates the local view and forwards a semantic request.
 * The audio subsystem owns profiles and playback; no Wwise work belongs in this view layer.
 * The reflected name is retained to preserve existing Blueprint call sites.
 */
UCLASS()
class WANDERER_API UWandererUIAudio : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

  public:
    UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Wanderer|UI Audio")
    static int32 PlayUIAction(UUserWidget* Widget, EWandererUIAudioAction Action);
};
