#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"

#include "Data/Audio/WandererAudioActionsAsset.h"

#include "UIAudioData.generated.h"

namespace UIAudioDefaults
{
    constexpr int32 MaximumProfileDepth = 8;
}

/** Same Inline/Asset AudioActions authoring as character actions; UI has no spatial emitter. */
USTRUCT(BlueprintType)
struct WANDERER_API FWandererUIAudioActionRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
    bool bEnabled = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
    EWandererAudioActionSource AudioActionSource = EWandererAudioActionSource::Inline;

    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Audio",
        meta = (EditCondition = "AudioActionSource == EWandererAudioActionSource::Inline",
            EditConditionHides,
            TitleProperty = "AudioActionID"))
    TArray<FWandererAudioAction> InlineAudioActions;

    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Audio",
        meta = (EditCondition = "AudioActionSource == EWandererAudioActionSource::Asset",
            EditConditionHides))
    TObjectPtr<UWandererAudioActionsAsset> AudioActionsAsset;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Documentation")
    FString Notes;
};
