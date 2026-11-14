#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "Types/RadioControlTypes.h"

#include "RadioPlaybackManager.generated.h"

class AActor;
class FRadioChannelLayout;
class UAkComponent;
class UAkAudioEvent;

USTRUCT()
struct FRadioSyncGroup
{
    GENERATED_BODY()
    FName ContentID;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UAkComponent>> Emitters;
    TArray<int32> PlayingIDs;
    bool bStereo = true;
    bool bPaused = false;
};

/** Private child of AudioSubsystem. One owner session retains its paused content clocks. */
UCLASS()
class URadioPlaybackManager : public UObject
{
    GENERATED_BODY()
public:
    static const ANSICHAR* ResolveRadioSwitch(FName InContentID);
    int32 GetChannelMask(
        int32 InNumber, FName InContentID, const TArray<FRadioEmitterSnapshot>& InSources) const;
    int32 GetPosition(int32 InNumber, FName InContentID) const;
    bool HasContent(FName InContentID) const;
    bool Initialize(AActor* InOwner);
    void Shutdown();
    bool Start(
        FName InContentID, const TArray<FRadioEmitterSnapshot>& InSources, FString& OutStatus);
    bool Update(const TArray<FRadioEmitterSnapshot>& InSources,
        float InDeltaTime,
        TArray<FName>& OutFailed);

private:
    UPROPERTY(Transient)
    TArray<FRadioSyncGroup> SyncGroups;
    UPROPERTY(Transient)
    TObjectPtr<UAkAudioEvent> PlayRadioEvent;
    UPROPERTY(Transient)
    TObjectPtr<UAkAudioEvent> StopRadioEvent;
    TWeakObjectPtr<AActor> OwnerWeakPtr;
    TArray<FRadioEmitterSnapshot> Sources;
    FString Status;
    bool bOperationActive = false;
    bool bShuttingDown = false;

    bool CreateSyncGroup(FName InContentID);
    void DestroySyncGroup(FRadioSyncGroup& InOutGroup);
    const FRadioSyncGroup* FindSyncGroup(FName InContentID) const;
    void FinishOperation();
    void ReleaseResources();
    bool StartSyncGroupSlots(FRadioSyncGroup& InOutGroup, const ANSICHAR* InSwitchValue);
    void StopSyncGroup(FRadioSyncGroup& InOutGroup);
    bool UpdateDeviceEmitter(UAkComponent& InOutEmitter,
        const FRadioEmitterSnapshot& InSource,
        int32 InChannelMask,
        float InGain);
    bool UpdateSyncGroup(FRadioSyncGroup& InOutGroup, float InDeltaTime);
    bool UpdateSyncSlot(FRadioSyncGroup& InOutGroup,
        const FRadioChannelLayout& InLayout,
        int32 InSlot,
        float InDeltaTime);
};
