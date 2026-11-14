#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Types/RadioControlTypes.h"

#include "WandererSpeakerActor.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class USceneComponent;
class UStaticMesh;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FWandererSpeakerPlaybackRequested, FName, OptionID, bool, bRequested);

/** One placed device: geometry and playback intent, without owning Wwise resources. */
UCLASS()
class AWandererSpeakerActor : public AActor
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable, Category = "Speaker")
    FWandererSpeakerPlaybackRequested OnPlaybackRequested;

    UFUNCTION(BlueprintPure, Category = "Speaker")
    int32 GetDeviceNumber() const
    {
        return DeviceNumber;
    }

    UFUNCTION(BlueprintPure, Category = "Speaker")
    USceneComponent* GetEmitterAnchor() const
    {
        return EmitterAnchor;
    }

    UFUNCTION(BlueprintPure, Category = "Speaker")
    EWandererSpeakerItem GetItemType() const
    {
        return ItemType;
    }

    UFUNCTION(BlueprintPure, Category = "Speaker")
    FName GetOptionID() const
    {
        return OptionID;
    }

    UFUNCTION(BlueprintPure, Category = "Speaker")
    bool IsPlaybackRequested() const
    {
        return bPlaybackRequested;
    }

    UFUNCTION(BlueprintCallable, Category = "Speaker")
    void RequestPlayback(FName NewOptionID, bool bRequested);

    AWandererSpeakerActor();
    bool ConfigureVisual(UStaticMesh* InMesh, float InLongestEdge);
    const FGuid& GetDeviceID() const
    {
        return DeviceID;
    }
    FVector GetPlacementExtent() const;
    void SetDeviceNumber(int32 InNumber)
    {
        DeviceNumber = InNumber;
    }
    void SetItemType(EWandererSpeakerItem InItem)
    {
        ItemType = InItem;
    }
    void SetPreview(bool bInPreview);

protected:
    virtual void EndPlay(const EEndPlayReason::Type InEndPlayReason) override;

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> EmitterAnchor;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UBoxComponent> PlacementBox;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> Visual;

    UPROPERTY(Transient)
    int32 DeviceNumber = 0;

    UPROPERTY(Transient)
    EWandererSpeakerItem ItemType = EWandererSpeakerItem::Radio;

    UPROPERTY(Transient)
    FName OptionID;

    UPROPERTY(Transient)
    bool bPlaybackRequested = false;

    FGuid DeviceID = FGuid::NewGuid();
    bool bEndingPlay = false;
    bool bPublishingPlayback = false;

    void BroadcastStopped();
};
