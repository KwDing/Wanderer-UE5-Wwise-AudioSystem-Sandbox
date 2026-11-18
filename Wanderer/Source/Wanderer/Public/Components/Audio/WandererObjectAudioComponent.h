#pragma once

#include "Components/ActorComponent.h"
#include "Types/Audio/AudioEmitterTypes.h"
#include "WandererObjectAudioComponent.generated.h"

class UWandererObjectAudioProfile;
class UWandererAudioSubsystem;

/** Inventory calls ActivateEmitter only after placement or equipment is confirmed. */
UCLASS(ClassGroup = Audio, meta = (BlueprintSpawnableComponent))
class WANDERER_API UWandererObjectAudioComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Audio")
    bool ActivateEmitter(FGuid InInstanceID, USceneComponent* InTarget, bool bInEquipped,
        FTransform InRelativeTransform);

    UFUNCTION(BlueprintCallable, Category = "Audio")
    bool UpdateAttachment(USceneComponent* InTarget, bool bInEquipped, FTransform InRelativeTransform);

    UFUNCTION(BlueprintCallable, Category = "Audio")
    EObjectAudioRequestResult RequestEvent(FName InEventID);

    UFUNCTION(BlueprintCallable, Category = "Audio")
    void StopEvent(FName InEventID);

    UFUNCTION(BlueprintCallable, Category = "Audio")
    void DeactivateEmitter();

    UFUNCTION(BlueprintPure, Category = "Audio")
    FObjectAudioEmitterStats GetEmitterStats() const;

    const FObjectAudioEmitterHandle& GetEmitterHandle() const { return EmitterHandle; }

protected:
    virtual void EndPlay(EEndPlayReason::Type InReason) override;

private:
    UPROPERTY(EditAnywhere, Category = "Audio")
    TObjectPtr<UWandererObjectAudioProfile> Profile;

    TWeakObjectPtr<UWandererAudioSubsystem> AudioWeakPtr;
    FObjectAudioEmitterHandle EmitterHandle;
    FObjectAudioAttachment MakeAttachment(USceneComponent* InTarget, bool bInEquipped,
        const FTransform& InRelativeTransform) const;
};
