#pragma once

#include "Engine/DataAsset.h"

#include "Data/Audio/UIAudioData.h"

#include "WandererUIAudioProfile.generated.h"

/** Parent defaults are overridden by matching RowName in child ActionTables. */
UCLASS(BlueprintType)
class WANDERER_API UWandererUIAudioProfile : public UDataAsset
{
    GENERATED_BODY()

  public:
    bool FindAction(FName ActionID, FWandererUIAudioActionRow& OutRow, FString& OutError) const;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inheritance")
    TObjectPtr<UWandererUIAudioProfile> ParentProfile;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Configuration")
    TArray<TObjectPtr<UDataTable>> ActionTables;
};
