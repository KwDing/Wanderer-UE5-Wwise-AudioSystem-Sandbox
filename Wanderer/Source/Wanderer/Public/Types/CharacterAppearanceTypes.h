#pragma once

#include "CoreMinimal.h"
#include "CharacterAppearanceTypes.generated.h"

class USkeletalMesh;
class UMaterialInterface;

USTRUCT(BlueprintType)
struct FWandererAppearancePart
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Appearance")
    TObjectPtr<USkeletalMesh> Mesh;

    UPROPERTY(EditAnywhere, Category = "Appearance")
    TArray<TObjectPtr<UMaterialInterface>> Materials;

    UPROPERTY(EditAnywhere, Category = "Appearance")
    FTransform RelativeTransform = FTransform::Identity;
};
