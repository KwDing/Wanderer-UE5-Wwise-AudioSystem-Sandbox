#pragma once
#include "UObject/Interface.h"
#include "Types/CharacterAppearanceTypes.h"
#include "CharacterPresentationProvider.generated.h"
class UCameraComponent;
class USkeletalMeshComponent;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UCharacterPresentationProvider : public UInterface
{
    GENERATED_BODY()
};
class WANDERER_API ICharacterPresentationProvider
{
    GENERATED_BODY()
  public:
    virtual UCameraComponent* GetCharacterCamera() const = 0;
    virtual USkeletalMeshComponent* GetPresentationMesh() const = 0;
    virtual const TArray<FWandererAppearancePart>& GetAppearanceParts() const = 0;
    virtual void PrepareForMenu() = 0;
};
