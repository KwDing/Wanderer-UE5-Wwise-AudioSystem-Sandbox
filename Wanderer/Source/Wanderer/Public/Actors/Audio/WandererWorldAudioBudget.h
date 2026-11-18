#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "Data/Audio/WorldAudioBudgetData.h"
#include "Interfaces/Audio/AudioBudgetProvider.h"
#include "Types/Audio/AudioSchedulingTypes.h"
#include "WandererWorldAudioBudget.generated.h"

/** One per persistent map. Soft UE admission limits, not Wwise physical voices. */
UCLASS(BlueprintType)
class WANDERER_API AWandererWorldAudioBudget : public AInfo, public IAudioBudgetProvider
{
    GENERATED_BODY()

  public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emitter Pool", meta = (ClampMin = "1"))
    int32 RegularSlots = AudioSchedulingDefaults::RegularSlots;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emitter Pool", meta = (ClampMin = "0"))
    int32 TransitionSlots = AudioSchedulingDefaults::TransitionSlots;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emitter Pool", meta = (ClampMin = "0"))
    int32 EmergencySlots = AudioSchedulingDefaults::EmergencySlots;

    UPROPERTY(EditAnywhere,
        BlueprintReadWrite,
        Category = "Audio Resources",
        meta = (TitleProperty = "Name"))
    TArray<FWandererAudioResourceGroup> ResourceGroups;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio Budget", meta = (ClampMin = "1"))
    int32 MaxAmbienceEvents = 24;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio Budget", meta = (ClampMin = "1"))
    int32 MaxDetailEvents = 16;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio Budget", meta = (ClampMin = "1"))
    int32 MaxTotalEvents = 48;

    UPROPERTY(EditAnywhere,
        BlueprintReadWrite,
        Category = "Audio Budget",
        meta = (ClampMin = "0.05", ClampMax = "1", Units = "s"))
    float UpdateInterval = .2f;

    virtual FAudioBudgetLimits GetAudioBudgetLimits() const override;
    virtual void SubmitAudioResources(UObject& InReceiver) const override;
};
