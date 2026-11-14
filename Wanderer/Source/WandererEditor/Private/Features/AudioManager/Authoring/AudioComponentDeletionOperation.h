#pragma once

#include "CoreMinimal.h"

class UWandererAudioSourceComponent;

/** Deletes native instance records on the game thread; never owns or saves their map. */
class FAudioComponentDeletionOperation
{
  public:
    /** Requires no active transaction. Failure restores only this batch's component identities.
     * Callbacks must not mutate the deleted components; unrelated owner edits are preserved.
     * A nonempty error may report incomplete recovery; the batch then retains its Undo record.
     */
    static bool Execute(
        const TArray<TWeakObjectPtr<UWandererAudioSourceComponent>>& InComponentsWeakPtr,
        UWorld* InWorld, FString& OutError, TFunction<bool()> InCanContinue = {});
};
