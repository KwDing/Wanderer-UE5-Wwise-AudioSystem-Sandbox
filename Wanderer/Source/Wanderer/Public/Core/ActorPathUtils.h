#pragma once

#include "UObject/SoftObjectPath.h"

class UWorld;

namespace ActorPathUtils
{
    /** Returns a world-local key without loading an actor; null/non-PIE worlds preserve input. */
    WANDERER_API FSoftObjectPath ForWorld(FSoftObjectPath InPath, const UWorld* InWorld);
} // namespace ActorPathUtils
