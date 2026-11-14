#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WandererCharacterAudioProfile.generated.h"

class UDataTable;

/**
 * Parent provides defaults. This profile overrides matching keys.
 * Tables within one profile must not define duplicate keys.
 */
UCLASS(BlueprintType)
class WANDERER_API UWandererCharacterAudioProfile : public UDataAsset
{
    GENERATED_BODY()

  public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inheritance")
    TObjectPtr<UWandererCharacterAudioProfile> ParentProfile = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Configuration")
    TArray<TObjectPtr<UDataTable>> ActionTables;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Configuration")
    TArray<TObjectPtr<UDataTable>> SwitchTables;
};