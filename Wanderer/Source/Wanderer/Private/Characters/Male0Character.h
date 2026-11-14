#pragma once

#include "CoreMinimal.h"
#include "Characters/WandererPlayableCharacter.h"
#include "Male0Character.generated.h"

/** Retains the existing Male0 Blueprint parent while sharing playable movement. */
UCLASS()
class WANDERER_API AMale0Character : public AWandererPlayableCharacter
{
    GENERATED_BODY()
};
