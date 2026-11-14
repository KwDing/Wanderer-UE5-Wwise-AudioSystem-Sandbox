#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/EngineTypes.h"
#include "Data/Audio/WandererAudioActionsAsset.h"
#include "WandererCharacterAudioTypes.generated.h"

class UAkComponent;
class UAkSwitchValue;

UENUM(BlueprintType)
enum class EWandererAudioSwitchGroup : uint8
{
    Surface,
    Gait,
    Wetness
};

/** Independent cooldown slots allow two real foot contacts at the same timestamp. */
UENUM(BlueprintType)
enum class EWandererAudioPlaybackScope : uint8
{
    Default,
    LeftFoot,
    RightFoot
};

USTRUCT(BlueprintType)
struct WANDERER_API FWandererCharacterAudioActionRow : public FTableRowBase
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
    TObjectPtr<UWandererAudioActionsAsset> AudioActionsAsset = nullptr;

    UPROPERTY(
        EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (ClampMin = "0.0", Units = "s"))
    float MinimumInterval = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Documentation")
    FString Notes;
};

USTRUCT(BlueprintType)
struct WANDERER_API FWandererCharacterAudioSwitchRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
    EWandererAudioSwitchGroup Group = EWandererAudioSwitchGroup::Surface;

    /** Art-owned native type. RowName is only an audio authoring label. */
    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Audio",
        meta = (EditCondition = "Group == EWandererAudioSwitchGroup::Surface", EditConditionHides))
    TEnumAsByte<EPhysicalSurface> PhysicalSurface = SurfaceType_Default;

    /** Legacy non-surface bindings only; native Surface rows use PhysicalSurface. */
    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Audio",
        meta = (EditCondition = "Group != EWandererAudioSwitchGroup::Surface", EditConditionHides))
    FName SemanticValue;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
    TObjectPtr<UAkSwitchValue> SwitchValue = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Documentation")
    FString Notes;
};

USTRUCT(BlueprintType)
struct WANDERER_API FWandererCharacterAudioContext
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    FName Surface = TEXT("ground");

    /** Native contact type consumed by ProfileSurface audio actions. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    TEnumAsByte<EPhysicalSurface> PhysicalSurface = SurfaceType_Default;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    FName Gait = TEXT("walk");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    FName Wetness = TEXT("dry");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    TMap<FName, FName> SemanticValues;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    TMap<FName, float> NumericValues;

    // Only needed when an audio action selects ContextTarget.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    TWeakObjectPtr<UAkComponent> TargetEmitter;

    // Optional, for ExistingPlayback parameter set/reset only.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    int64 ExistingPlayingID = 0;
};
