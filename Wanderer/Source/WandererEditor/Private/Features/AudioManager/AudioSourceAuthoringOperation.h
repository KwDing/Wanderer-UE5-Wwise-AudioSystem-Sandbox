#pragma once

#include "Features/AudioManager/Authoring/Sources/AudioSourceAuthoringTypes.h"

/** Synchronous facade; normal source authoring owns one undoable batch transaction. */
class FAudioSourceAuthoringOperation
{
public:
    static FAudioSourceResult Apply(const FAudioSourceRequest& InRequest);
    static TArray<TWeakObjectPtr<UWandererAudioSourceComponent>> GatherSources(
        UWorld* InWorld, const TArray<TWeakObjectPtr<AActor>>& InTargets);
    static FDataTableRowHandle FindOrAddProfile(UDataTable* InTable,
        const FWandererWorldAudioProfileRow& InRow, UAkAudioEvent* InEvent,
        FName InRowName = NAME_None);
    static FWandererWorldAudioProfileRow MergeProfile(
        const FWandererWorldAudioProfileRow& InOriginal, const FAudioSourceRequest& InRequest);
};
