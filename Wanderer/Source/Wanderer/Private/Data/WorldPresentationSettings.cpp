#include "Data/WorldPresentationSettings.h"

bool UWorldPresentationSettings::MatchesMap(const FString& InMapName, const TArray<FName>& InMaps)
{
    for (FName Map : InMaps)
    {
        const FString Name = Map.ToString();
        if (InMapName == Name
            || (InMapName.StartsWith(TEXT("UEDPIE_")) && InMapName.EndsWith(TEXT("_") + Name)))
        {
            return true;
        }
    }
    return false;
}
