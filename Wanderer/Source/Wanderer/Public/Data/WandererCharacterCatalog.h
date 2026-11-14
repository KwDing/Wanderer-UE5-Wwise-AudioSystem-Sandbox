#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WandererCharacterCatalog.generated.h"

class APawn;
class UAnimationAsset;

USTRUCT(BlueprintType)
struct FWandererCharacterEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character")
	FName ID;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character")
	TSubclassOf<APawn> CharacterClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character")
	TObjectPtr<UAnimationAsset> PortraitAnimation;
};

/** Add a unique ID, display name and playable Blueprint class for each character. */
UCLASS(BlueprintType)
class WANDERER_API UWandererCharacterCatalog : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Characters")
	TArray<FWandererCharacterEntry> Characters;
};
