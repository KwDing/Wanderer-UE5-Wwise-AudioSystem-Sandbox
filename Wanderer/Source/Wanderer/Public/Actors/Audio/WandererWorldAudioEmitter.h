#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WandererWorldAudioEmitter.generated.h"

class USceneComponent;
class UWandererAudioSourceComponent;

/** Placeable source with no boundary or listener-following geometry. */
UCLASS(BlueprintType, Blueprintable)
class WANDERER_API AWandererWorldAudioEmitter : public AActor
{
    GENERATED_BODY()
  public:
    AWandererWorldAudioEmitter();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Audio")
    TObjectPtr<UWandererAudioSourceComponent> AudioSource;

  private:
    UPROPERTY()
    TObjectPtr<USceneComponent> SceneRoot;
};
