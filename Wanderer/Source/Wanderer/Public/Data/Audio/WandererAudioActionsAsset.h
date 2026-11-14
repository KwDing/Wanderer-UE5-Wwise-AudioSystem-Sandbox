#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/Audio/WandererAudioValueMapping.h"
#include "WandererAudioActionsAsset.generated.h"

class UAkAudioEvent;
class UAkRtpc;
class UAkSwitchValue;
class UAkStateValue;
class UAkTrigger;

UENUM(BlueprintType)
enum class EWandererAudioActionSource : uint8
{
    Inline = 0,
    Asset = 1
};

UENUM(BlueprintType)
enum class EWandererAudioActionType : uint8
{
    PostEvent = 0,
    SetParameter = 1,
    SetSwitch = 2,
    SetState = 3,
    PostTrigger = 4,
    ResetParameter = 5
};

UENUM(BlueprintType)
enum class EWandererAudioTarget : uint8
{
    Owner = 0,
    ContextTarget = 1
};

UENUM(BlueprintType)
enum class EWandererAudioValueSource : uint8
{
    Constant = 0,
    NumericContext = 1,
    SemanticMapping = 2,
    ProfileSurface = 3 UMETA(DisplayName = "Native Physical Surface Table")
};

UENUM(BlueprintType)
enum class EWandererAudioParameterScope : uint8
{
    Emitter = 0,
    ThisExecution = 1,
    Global = 2,
    ExistingPlayback = 3
};

UENUM(BlueprintType)
enum class EWandererAudioExecutionStatus : uint8
{
    Accepted = 0,
    Skipped = 1,
    Failed = 2,
    PartialFailure = 3
};

USTRUCT(BlueprintType)
struct WANDERER_API FWandererAudioExecutionResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    EWandererAudioExecutionStatus Status = EWandererAudioExecutionStatus::Failed;

    // int64 preserves the unsigned 32-bit Wwise ID in Blueprint.
    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    int64 PlayingID = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    int32 AcceptedAudioActions = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Audio")
    FString Message;
};

USTRUCT(BlueprintType)
struct WANDERER_API FWandererAudioAction
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
    FName AudioActionID;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
    EWandererAudioActionType AudioActionType = EWandererAudioActionType::PostEvent;

    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Audio",
        meta = (EditCondition =
                    "AudioActionType != EWandererAudioActionType::SetState && ((AudioActionType != EWandererAudioActionType::SetParameter && AudioActionType != EWandererAudioActionType::ResetParameter) || Scope != EWandererAudioParameterScope::Global)",
            EditConditionHides))
    EWandererAudioTarget Target = EWandererAudioTarget::Owner;

    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Audio",
        meta = (EditCondition = "AudioActionType == EWandererAudioActionType::PostEvent",
            EditConditionHides))
    TObjectPtr<UAkAudioEvent> Event = nullptr;

    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Audio",
        meta = (EditCondition =
                    "AudioActionType == EWandererAudioActionType::SetParameter || AudioActionType == EWandererAudioActionType::ResetParameter",
            EditConditionHides))
    TObjectPtr<UAkRtpc> Parameter = nullptr;

    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Audio",
        meta = (EditCondition =
                    "AudioActionType == EWandererAudioActionType::SetParameter || AudioActionType == EWandererAudioActionType::ResetParameter",
            EditConditionHides))
    EWandererAudioParameterScope Scope = EWandererAudioParameterScope::Emitter;

    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Audio",
        meta = (EditCondition =
                    "AudioActionType == EWandererAudioActionType::SetParameter || AudioActionType == EWandererAudioActionType::SetSwitch || AudioActionType == EWandererAudioActionType::SetState",
            EditConditionHides))
    EWandererAudioValueSource ValueSource = EWandererAudioValueSource::Constant;

    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Audio",
        meta = (EditCondition =
                    "(AudioActionType == EWandererAudioActionType::SetParameter || AudioActionType == EWandererAudioActionType::SetSwitch || AudioActionType == EWandererAudioActionType::SetState) && ValueSource != EWandererAudioValueSource::Constant && ValueSource != EWandererAudioValueSource::ProfileSurface",
            EditConditionHides))
    FName ContextKey;

    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Audio",
        meta = (EditCondition =
                    "AudioActionType == EWandererAudioActionType::SetParameter && ValueSource == EWandererAudioValueSource::Constant",
            EditConditionHides))
    float ConstantValue = 0.0f;

    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Audio",
        meta = (EditCondition =
                    "AudioActionType == EWandererAudioActionType::SetParameter && ValueSource == EWandererAudioValueSource::SemanticMapping",
            EditConditionHides))
    TObjectPtr<UWandererAudioNumberMapping> NumberMapping = nullptr;

    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Audio",
        meta = (EditCondition =
                    "AudioActionType == EWandererAudioActionType::SetSwitch && ValueSource == EWandererAudioValueSource::Constant",
            EditConditionHides))
    TObjectPtr<UAkSwitchValue> SwitchValue = nullptr;

    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Audio",
        meta = (EditCondition =
                    "AudioActionType == EWandererAudioActionType::SetSwitch && ValueSource == EWandererAudioValueSource::SemanticMapping",
            EditConditionHides))
    TObjectPtr<UWandererAudioSwitchMapping> SwitchMapping = nullptr;

    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Audio",
        meta = (EditCondition =
                    "AudioActionType == EWandererAudioActionType::SetState && ValueSource == EWandererAudioValueSource::Constant",
            EditConditionHides))
    TObjectPtr<UAkStateValue> StateValue = nullptr;

    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Audio",
        meta = (EditCondition =
                    "AudioActionType == EWandererAudioActionType::SetState && ValueSource == EWandererAudioValueSource::SemanticMapping",
            EditConditionHides))
    TObjectPtr<UWandererAudioStateMapping> StateMapping = nullptr;

    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Audio",
        meta = (EditCondition =
                    "(AudioActionType == EWandererAudioActionType::SetParameter || AudioActionType == EWandererAudioActionType::SetSwitch || AudioActionType == EWandererAudioActionType::SetState) && ValueSource != EWandererAudioValueSource::Constant",
            EditConditionHides))
    bool bUseFallback = false;

    // For mapped values, select another semantic key, not a second copy of the value.
    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Audio",
        meta = (EditCondition =
                    "(AudioActionType == EWandererAudioActionType::SetParameter || AudioActionType == EWandererAudioActionType::SetSwitch || AudioActionType == EWandererAudioActionType::SetState) && bUseFallback && ValueSource == EWandererAudioValueSource::SemanticMapping",
            EditConditionHides))
    FName FallbackKey = TEXT("default");

    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Audio",
        meta = (EditCondition =
                    "AudioActionType == EWandererAudioActionType::SetParameter && ValueSource == EWandererAudioValueSource::NumericContext && bUseFallback",
            EditConditionHides))
    float FallbackNumber = 0.0f;

    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Audio",
        meta = (EditCondition = "AudioActionType == EWandererAudioActionType::PostTrigger",
            EditConditionHides))
    TObjectPtr<UAkTrigger> Trigger = nullptr;
};

UCLASS(BlueprintType)
class WANDERER_API UWandererAudioActionsAsset : public UDataAsset
{
    GENERATED_BODY()
  public:
    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Audio",
        meta = (TitleProperty = "AudioActionID"))
    TArray<FWandererAudioAction> AudioActions;
};
