#pragma once

#include "CoreMinimal.h"

/** Explicit PIE snapshot, not a polling service. Never creates emitters or changes room routing. */
class FGameLogSnapshotOperation
{
  public:
    static TArray<FString> Capture();
};
