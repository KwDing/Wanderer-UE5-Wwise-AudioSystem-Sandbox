#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WandererWwiseSyncTestReference.generated.h"

class UAkAudioEvent;

/** Isolated editor automation fixture; never used by gameplay. */
UCLASS()
class UWandererWwiseSyncTestReference : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY()
	TObjectPtr<UAkAudioEvent> HardEvent;
	UPROPERTY()
	TSoftObjectPtr<UAkAudioEvent> SoftEvent;
};
