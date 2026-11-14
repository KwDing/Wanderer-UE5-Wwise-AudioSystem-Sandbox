#pragma once
#include "UObject/Interface.h"
#include "Types/CharacterSurfaceTypes.h"
#include "CharacterSurfaceProvider.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UCharacterSurfaceProvider : public UInterface
{
    GENERATED_BODY()
};
/** Stable owner contract; callers must query on the game thread without retaining a Pawn. */
class WANDERER_API ICharacterSurfaceProvider
{
    GENERATED_BODY()
  public:
    /** Rebuild scene caches after streaming or scene edits; may scan the world synchronously. */
    virtual void RefreshContactRegions() = 0;

    virtual FName ResolveSurfaceName(const FHitResult& InHit) const = 0;
    /** A null/nonblocking hit reads the cached snapshot; InFeet is used only for a valid hit. */
    virtual FCharacterSurfaceSnapshot QueryContactSurface(
        const FHitResult* InHit, const FVector& InFeet) const = 0;
};
