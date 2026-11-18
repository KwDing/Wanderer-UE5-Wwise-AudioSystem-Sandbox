#pragma once

#include "CoreMinimal.h"

class UAkAudioEvent;
class UAkComponent;
class UAkGameObject;
class UAkRoomComponent;
class UWorld;
enum class ERadioChannel : uint8;
enum class ERadioContent : uint8;
struct FWwiseLoadedEventInfo;

/** Game-thread Wwise translation for the audio system. Neither worlds nor emitters are retained. */
namespace WandererAudioBackend
{
    bool IsInitialized();
    bool IsDeviceReady();
    bool IsSoundEngineReady();

    // An unset result means the playback registry is unavailable.
    TOptional<bool> QueryPlaybackActive(const UAkAudioEvent* InEvent, uint32 InPlayingID);
    int32 GetPlaybackPositionMs(uint32 InPlayingID);
    bool RestoreRadioPosition(const UAkAudioEvent& InEvent, const UAkComponent& InEmitter,
        uint32 InPlayingID, int32 InPositionMs);
    void StopPlayback(uint32 InPlayingID, int32 InFadeMs);
    void SetPlaybackPaused(uint32 InPlayingID, bool bInPaused, int32 InTransitionMs);

    // A successful sample may return null when the location has no Room.
    bool TryQueryPriorityRoom(const UWorld* InWorld, const FVector& InLocation, UAkRoomComponent*& OutRoom);
    bool HasSpatialListener(const UWorld* InWorld);
    void UpdateEmitterRoom(UAkComponent& InEmitter, const FVector& InLocation);
    bool FadeInPlayback(uint32 InPlayingID, int32 InFadeMs);

    const ANSICHAR* GetRadioSwitchName(ERadioContent InContent);
    int32 GetRadioChannelMask(ERadioChannel InChannel);
    bool ConfigureRadioEmitter(UAkComponent& InEmitter, ERadioContent InContent);
    bool SetRadioEmitterGain(const UAkComponent& InEmitter, float InGain, int32 InTransitionMs);
    bool UpdateRadioEmitter(UAkComponent& InEmitter, const TArray<FTransform>& InPositions,
        TConstArrayView<ERadioChannel> InChannels, float InGain, int32 InTransitionMs);
    bool ResetPooledEmitterParameters(UAkComponent& InEmitter, bool bInRadio);

    bool IsFiniteEvent(const UAkAudioEvent& InEvent);
    uint32 PostIntegratedEvent(UAkAudioEvent& InEvent, UAkGameObject* InTarget);
    uint32 PostManagedEvent(const FWwiseLoadedEventInfo& InMedia, UAkGameObject* InTarget);
}
