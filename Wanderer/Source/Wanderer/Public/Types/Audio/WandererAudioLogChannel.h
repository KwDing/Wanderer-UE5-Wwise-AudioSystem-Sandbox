#pragma once

#include "CoreMinimal.h"
#include "WandererAudioLogChannel.generated.h"

UENUM(BlueprintType)
enum class EWandererAudioLogChannel : uint8
{
	Configuration,
	Footsteps,
	Actions,
	Switches,
	Playback,
	Amb
};
