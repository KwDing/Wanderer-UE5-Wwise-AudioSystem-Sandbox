#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "WandererSpeakerComponent.generated.h"

/** Serialization compatibility only for existing DeployableSpeakers CDO templates.
 * New controllers do not create this component. Remove after the old Blueprint templates
 * have been migrated and saved through Unreal Editor; never add feature behavior here.
 */
UCLASS(NotBlueprintable, HideDropdown)
class UWandererSpeakerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UWandererSpeakerComponent();
};
