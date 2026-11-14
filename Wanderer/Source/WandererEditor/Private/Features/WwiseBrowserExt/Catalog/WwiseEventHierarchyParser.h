#pragma once

#include "CoreMinimal.h"

/** Saved authoring snapshot; never interprets bank exclusion as Event deletion. */
class FWwiseEventHierarchyParser
{
public:
    static bool Parse(
        const FString& InEventsDirectory, TMap<FGuid, FString>& OutPaths, FString& OutError);
};
