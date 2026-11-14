#pragma once

#include "CoreMinimal.h"

#include "Types/Audio/WandererAudioLogChannel.h"

/** Game-thread diagnostic gates. They never control gameplay or critical Output Log errors. */
struct WANDERER_API FWandererAudioDiagnostics
{
	static bool IsMasterEnabled();
	static void SetMasterEnabled(bool bEnabled);
	static bool IsChannelEnabled(EWandererAudioLogChannel Channel);
	static void SetChannelEnabled(EWandererAudioLogChannel Channel, bool bEnabled);
	static bool IsEnabled(EWandererAudioLogChannel Channel);
	static void Publish(const UObject* Context, EWandererAudioLogChannel Channel,
		FName Level, const FString& Text, FName Source = TEXT("CharacterAudio"));

private:
    static const TCHAR* GetChannelName(EWandererAudioLogChannel InChannel);
};
