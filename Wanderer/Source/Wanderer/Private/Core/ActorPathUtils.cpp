#include "Core/ActorPathUtils.h"

#include "Engine/World.h"
#include "UObject/Package.h"

FSoftObjectPath ActorPathUtils::ForWorld(FSoftObjectPath InPath, const UWorld* InWorld)
{
#if WITH_EDITOR
    if (InPath.IsNull() || !IsValid(InWorld) || InWorld->WorldType != EWorldType::PIE)
    {
        return InPath;
    }
    const int32 Instance = InWorld->GetOutermost()->GetPIEInstanceID();
    if (Instance != INDEX_NONE)
    {
        InPath.FixupForPIE(Instance);
    }
#endif
    return InPath;
}
