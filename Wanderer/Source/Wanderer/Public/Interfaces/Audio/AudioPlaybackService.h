#pragma once

#include "CoreMinimal.h"

class UAkAudioType;
class UAkAudioEvent;
class UAkComponent;
class UAkGameObject;
class UWorld;
enum class EWandererAudioActionType : uint8;
enum class EWandererAudioParameterScope : uint8;
struct FWandererAudioExecutionResult;

enum class EWorldAudioEmitterParameter : uint8
{
    Priority,
    AreaPresence,
    SplineInside,
    BuildingMode,
    BuildingGain
};

/** A resolved semantic action for synchronous, game-thread submission. */
struct FPreparedAudioAction
{
    FName ActionID;
    EWandererAudioActionType Type{};
    EWandererAudioParameterScope Scope{};
    TWeakObjectPtr<UAkAudioType> AssetWeakPtr;
    TWeakObjectPtr<UAkComponent> TargetWeakPtr;
    uint32 ExistingPlayingID = 0;
    float Value = 0;
};

namespace WandererAudioPlayback
{
    WANDERER_API bool IsReady(const UWorld& InWorld);
    WANDERER_API bool IsPlaybackActive(const UAkAudioEvent* InEvent, uint32 InPlayingID);
    WANDERER_API void SetWorldEmitterParameter(UAkGameObject& InTarget,
        EWorldAudioEmitterParameter InParameter, float InValue, int32 InTransitionMs);
    WANDERER_API void SetEmitterRadius(UAkComponent& InEmitter, float InOuterRadiusCm, float InInnerRadiusCm);
    WANDERER_API FWandererAudioExecutionResult ApplyPreparedActions(const UWorld& InWorld,
        TConstArrayView<FPreparedAudioAction> InActions, bool& bOutAttempted);
}
