#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "Types/Audio/AudioSchedulingTypes.h"
#include "Types/RadioControlTypes.h"

#include "RadioPlaybackManager.generated.h"

class AActor;
class FRadioChannelLayout;
class UAkComponent;
class UAkAudioEvent;
class UAudioEmitterPool;
class UAudioEventLeaseManager;
struct FStreamableHandle;
struct FAudioSourceCandidate;

USTRUCT()
struct FRadioSyncGroup
{
    GENERATED_BODY()
    FName ContentID;
    UPROPERTY(Transient)
    TObjectPtr<UAkComponent> Emitter;
    uint32 PlayingID = 0;
    bool bStereo = true;
    bool bPaused = false;
};

/** Private child of AudioSubsystem. Retrieved content retains value-only playback bookmarks. */
UCLASS()
class URadioPlaybackManager : public UObject
{
    GENERATED_BODY()
public:
    static TOptional<ERadioContent> ResolveRadioContent(FName InContentID);
    ERadioChannel GetChannel(
        int32 InNumber, FName InContentID, const TArray<FRadioEmitterSnapshot>& InSources) const;
    int32 GetPosition(int32 InNumber, FName InContentID) const;
    bool HasContent(FName InContentID) const;
    ERadioPlaybackPhase GetPlaybackPhase(FName InContentID) const;
    bool Initialize(AActor* InOwner, UAudioEmitterPool& InPool);
    void Shutdown();
    bool Start(
        FName InContentID, const TArray<FRadioEmitterSnapshot>& InSources, FString& OutStatus);
    bool Update(const TArray<FRadioEmitterSnapshot>& InSources,
        float InDeltaTime,
        TArray<FName>& OutFailed);
    void CollectCandidates(const FVector& InListener, TArray<FAudioSourceCandidate>& OutCandidates);
    bool PrepareAdmission(FName InContentID, float InPriority);
    void SetAdmission(FName InContentID, bool bInAdmitted, float InPriority);
    void SuspendAdmissions();
    int32 GetActiveEmitterCount() const;

private:
    friend struct FRadioPlaybackTestAccess;

    enum class EGroupStart : uint8
    {
        Started,
        WaitingForEmitters,
        Failed
    };

    UPROPERTY(Transient)
    TArray<FRadioSyncGroup> SyncGroups;
    UPROPERTY(Transient)
    TObjectPtr<UAkAudioEvent> PlayRadioEvent;
    UPROPERTY(Transient)
    TObjectPtr<UAkAudioEvent> StopRadioEvent;
    UPROPERTY(Transient)
    TObjectPtr<UAudioEventLeaseManager> EventLeases;
    TWeakObjectPtr<AActor> OwnerWeakPtr;
    TWeakObjectPtr<UAudioEmitterPool> EmitterPoolWeakPtr;
    TSoftObjectPtr<UAkAudioEvent> RequestedPlayEvent;
    TSoftObjectPtr<UAkAudioEvent> RequestedStopEvent;
    TSharedPtr<FStreamableHandle> MediaRequest;
    TSet<FName> PendingStarts;
    TMap<FName, int32> SavedPositionsMs;
    TSet<FName> AdmittedContents;
    float LoadPriority = AudioSchedulingDefaults::PreloadPriority;
    bool bWithinLoadRange = false;
    bool bHasSpatialSample = false;
    EAudioEventResourceStatus MediaStatus = EAudioEventResourceStatus::Pending;
    double MediaDeadlineSec = 0;
    TArray<FRadioEmitterSnapshot> Sources;
    FString Status;
    bool bOperationActive = false;
    bool bShuttingDown = false;
    bool bWaitingForListener = false;
    bool bResourcesRequested = false;

    void RequestMedia();
    void AdvanceMedia();
    void ReleaseMedia();
    void ParkPlayback();
    void UpdatePendingStarts(TArray<FName>& OutFailed);
    bool IsSyncGroupAlive(const FRadioSyncGroup& InGroup) const;
    void MuteSyncGroup(const FRadioSyncGroup& InGroup) const;
    EGroupStart CreateSyncGroup(FName InContentID);
    void DestroySyncGroup(FRadioSyncGroup& InOutGroup, int32 InFadeMs = 0);
    const FRadioSyncGroup* FindSyncGroup(FName InContentID) const;
    void FinishOperation();
    void ReleaseResources();
    void ApplyPendingAdmissions();
    bool StartSyncGroupPlayback(FRadioSyncGroup& InOutGroup, ERadioContent InContent);
    bool UpdateSyncGroup(FRadioSyncGroup& InOutGroup, bool bInHasListener);
    bool UpdateSyncPositions(UAkComponent& InOutEmitter, const FRadioChannelLayout& InLayout);
};
