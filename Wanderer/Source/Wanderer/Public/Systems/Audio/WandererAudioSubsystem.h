#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "Data/Audio/WorldAudioBudgetData.h"
#include "Types/Audio/AudioEmitterTypes.h"
#include "Types/Audio/AudioSchedulingTypes.h"
#include "Types/GameEventTypes.h"
#include "Types/RadioControlTypes.h"

#include "WandererAudioSubsystem.generated.h"

class APawn;
class UAudioEventLeaseManager;
class URadioPlaybackManager;
class UActorComponent;
class UAkAudioEvent;
class UAkComponent;
class UAkGameObject;
class UAkRoomComponent;
class UAudioEmitterPool;
class UWandererObjectAudioProfile;
struct FStreamableHandle;
struct FObjectAudioEmitterState;
struct FObjectAudioPlayback;

struct FWandererAudioResourceState;
struct FAudioSourceCandidate;
struct FAudioResourceStateMap;
struct FWorldAudioAdmissionContext;

/** World-owned scheduling/resources. All source calls and resource operations are game-thread only.
 */
UCLASS()
class WANDERER_API UWandererAudioSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

  public:
    UWandererAudioSubsystem();

    UFUNCTION(BlueprintPure,
              Category = "Wanderer|Audio",
              meta = (WorldContext = "WorldContextObject"))
    static UWandererAudioSubsystem* GetAudioSubsystem(const UObject* WorldContextObject);

    /** Pawn yaw defines front/right; up/down use world Z. Distances are from pawn center. */
    UFUNCTION(BlueprintCallable, Category = "Wanderer|Audio|Space")
    bool CaptureSpaceInfo(APawn* Pawn,
                          float MaximumDistanceMeters,
                          TArray<FWandererSpaceDirection>& OutDirections);

    UFUNCTION(BlueprintPure, Category = "Audio")
    int32 GetRegisteredCount() const
    {
        return SourcesWeakPtrs.Num();
    }

    UFUNCTION(BlueprintPure, Category = "Audio")
    int32 GetAdmittedCount() const
    {
        return AdmittedCount;
    }

    UFUNCTION(BlueprintPure, Category = "Audio")
    int32 GetProtectedOverflow() const
    {
        return ProtectedOverflow;
    }

    UFUNCTION(BlueprintPure, Category = "Audio")
    FString GetSummary() const;

    UFUNCTION(BlueprintPure, Category = "Audio|Resources")
    FString GetResourceSummary() const;

    UFUNCTION(BlueprintPure, Category = "Audio|Resources")
    bool IsResourceGroupActive(FName Group) const;

    /** Actual Integration readiness, not merely the local resource lease count. */
    UFUNCTION(BlueprintPure, Category = "Audio|Resources")
    bool IsEventDataResident(UAkAudioEvent* Event) const;

    UFUNCTION(BlueprintPure, Category = "Audio|Resources")
    FString GetResourceGroupStatus(FName Group) const;

    UFUNCTION(BlueprintPure, Category = "Audio|Resources")
    int32 GetResourceGroupAdmitted(FName Group) const
    {
        return GroupAdmitted.FindRef(Group);
    }

    UFUNCTION(BlueprintPure, Category = "Audio|Resources")
    int32 GetResourceGroupLimit(FName Group) const;

    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
    virtual void Deinitialize() override;

    void RegisterSource(UObject* InSource);
    void UnregisterSource(UObject* InSource);
    bool IsListenerInsideBuilding(const UObject* InSource) const;
    UAkComponent* AcquireEmitter(const UObject& InSource, bool bInPreferPrevious);
    void ApplyEmitterReverb(UAkComponent& InEmitter, bool bInUseVolumes);
    void ReleaseEmitter(const UObject& InSource, UAkComponent* InComponent,
        UAkAudioEvent* InEvent, uint32 InPlayingID, int32 InFadeMs, float InTailSec,
        FName InGroup, bool bInOwnsMedia, bool bInPreferPrevious);
    bool HasPendingEmitterRelease(const UObject& InSource) const;
    bool AcquireEventResource(UAkAudioEvent* InEvent);
    EAudioEventResourceStatus AdvanceEventResource(UAkAudioEvent* InEvent, float InPriority = 100.0f);
    uint32 PostPreparedEvent(UAkAudioEvent& InEvent, UAkGameObject* InObject, float InFadeInSec = 0);
    FAudioEmitterPoolStats GetEmitterPoolStats() const;
    bool CanStartWorldPlayback() const;
    void ReleaseEventResource(UAkAudioEvent* InEvent);

    FObjectAudioEmitterHandle RegisterObjectEmitter(FGuid InInstanceID, AActor& InOwner,
        const FObjectAudioAttachment& InAttachment, const UWandererObjectAudioProfile* InProfile);
    bool UpdateObjectEmitter(const FObjectAudioEmitterHandle& InHandle,
        const FObjectAudioAttachment& InAttachment);
    bool ResolveObjectEmitter(const FObjectAudioEmitterHandle& InHandle, FTransform& OutTransform,
        const AActor* InOwnerScope = nullptr) const;
    EObjectAudioRequestResult PostObjectEvent(const FObjectAudioEmitterHandle& InHandle, FName InEventID);
    void StopObjectEvent(const FObjectAudioEmitterHandle& InHandle, FName InEventID);
    void UnregisterObjectEmitter(const FObjectAudioEmitterHandle& InHandle);
    FObjectAudioEmitterStats GetObjectEmitterStats(const FObjectAudioEmitterHandle& InHandle) const;

    /** Called synchronously by the selected budget adapter before the session snapshot is sealed.
     */
    void ConfigureResourceGroups(const TArray<FWandererAudioResourceGroup>& InGroups);

    static bool IsRadioContentKnown(FName InContentID);
    bool PreloadRadioPlayback(AActor* InOwner);
    bool StartRadioPlayback(AActor* InOwner,
                            FName InContentID,
                            const TArray<FRadioEmitterSnapshot>& InSources,
                            FString& OutStatus);
    bool UpdateRadioPlayback(AActor* InOwner,
                             const TArray<FRadioEmitterSnapshot>& InSources,
                             float InDeltaTime,
                             TArray<FName>& OutFailed);
    void ReleaseRadioPlayback(AActor* InOwner);
    int32 GetRadioPosition(AActor* InOwner, int32 InNumber, FName InContentID) const;
    int32 GetRadioChannelMask(AActor* InOwner,
                              int32 InNumber,
                              FName InContentID,
                              const TArray<FRadioEmitterSnapshot>& InSources) const;
    bool HasRadioContent(AActor* InOwner, FName InContentID) const;
    ERadioPlaybackPhase GetRadioPlaybackPhase(AActor* InOwner, FName InContentID) const;

  private:
    UPROPERTY(Transient)
    TMap<TWeakObjectPtr<AActor>, TObjectPtr<URadioPlaybackManager>> RadioSessions;
    void PruneRadioSessions();
    void ShutdownRadioSessions();
    void CollectRadioCandidates(const FVector& InListener, TArray<FAudioSourceCandidate>& OutCandidates);
    int32 GetRadioActiveCount() const;
    void SuspendRadioAdmissions();
    TArray<FRadioEmitterSnapshot> ResolveRadioEmitters(const AActor& InOwner,
        const TArray<FRadioEmitterSnapshot>& InSources) const;

    TMap<FGuid, TSharedPtr<FObjectAudioEmitterState>> ObjectEmitters;
    uint64 ObjectEmitterGeneration = 0;
    int32 ObjectPlaybackCount = 0;
    FObjectAudioEmitterState* FindObjectEmitter(const FObjectAudioEmitterHandle& InHandle) const;
    void UpdateObjectEmitters(bool bInAllowNewPosts);
    void CollectObjectCandidates(const FVector& InListener, TArray<FAudioSourceCandidate>& OutCandidates);
    void SetObjectAdmission(const FGuid& InInstanceID, bool bInAdmitted, bool bInEmergency);
    bool PrepareObjectAdmission(const FGuid& InInstanceID, bool bInEmergency);
    void YieldObjectEmitter(FObjectAudioEmitterState& InOutEmitter);
    bool IsObjectGroupPlaying(FName InGroup) const;
    int32 GetObjectActiveCount() const;
    void ShutdownObjectEmitters();
    void AdvanceObjectPlayback(FObjectAudioEmitterState& InOutEmitter,
        const FTransform& InTransform, bool bInAllowNewPosts);
    enum class EObjectPlaybackStart : uint8
    {
        Waiting,
        Posted,
        Failed,
        Aborted
    };
    EObjectPlaybackStart StartObjectPlayback(FObjectAudioEmitterState& InOutEmitter,
        FObjectAudioPlayback& InOutPlayback, const FTransform& InTransform);
    EAudioEventResourceStatus PrepareObjectPlayback(FObjectAudioPlayback& InOutPlayback,
        float InPriority, bool bInEmergency);

    friend class FBuildingAmbienceAuthoringTest;
    friend struct FAudioSchedulingTestAccess;
    friend struct FRadioPlaybackTestAccess;
    friend struct FAudioPriorityTestAccess;

    UPROPERTY(Transient)
    TArray<FWandererAudioResourceGroup> ResourceConfig;

    UPROPERTY(Transient)
    TObjectPtr<UAudioEventLeaseManager> EventLeaseManager;
    UPROPERTY(Transient)
    TObjectPtr<UAudioEmitterPool> EmitterPool;

    TArray<TWeakObjectPtr<UObject>> SourcesWeakPtrs;
    TWeakObjectPtr<UObject> BudgetWeakPtr;
    TMap<FSoftObjectPath, TWeakObjectPtr<UObject>> RoomBuildingsWeakPtrs;
    TWeakObjectPtr<UObject> ListenerBuildingWeakPtr;
    TWeakObjectPtr<UAkRoomComponent> ListenerRoomWeakPtr;
    TWeakObjectPtr<UAkRoomComponent> PendingListenerRoomWeakPtr;
    TSharedPtr<FAudioResourceStateMap> ResourceState;
    TSet<TWeakObjectPtr<UActorComponent>> SuspendedComponents;
    TMap<FName, int32> GroupAdmitted;
    FString ResourceConfigError;
    float UpdateElapsedSec = 0.0f;
    int32 AdmittedCount = 0;
    int32 ProtectedOverflow = 0;
    bool bResourcesInitialized = false;
    bool bDeinitializing = false;
    bool bHasPendingListenerRoom = false;
    bool bAdmissionDirty = false;


    bool IsSourceCurrent(const UObject* InSource) const;
    bool RegisterRoomMembership(UObject& InSource);
    void UpdateBuildingListener(const FVector& InListener);
    void ResetBuildingListener();
    void ResolveBudget();
    FAudioBudgetLimits ReadBudgetLimits() const;
    bool ReadNightState() const;
    void SuspendSources();
    void CollectCandidates(const FVector& InListener,
                           bool bInNight,
                           TArray<FAudioSourceCandidate>& OutCandidates) const;
    void Schedule(const FVector& InListener);
    void SelectAdmissions(TArray<FAudioSourceCandidate>& InOutCandidates,
        bool bInNight, FWorldAudioAdmissionContext& InOutContext);
    bool PrepareCandidate(FAudioSourceCandidate& InOutCandidate,
        bool bInNight, FWorldAudioAdmissionContext& InOutContext);
    void RetainPlayingCandidates(TArray<FAudioSourceCandidate>& InOutCandidates,
        FWorldAudioAdmissionContext& InOutContext);
    void ApplyAdmissions(const TArray<FAudioSourceCandidate>& InCandidates, bool bInNight);
    int32 GetWorldPlaybackCount() const;
    bool InitializeResourceGroups();
    TSet<FName> FindWantedGroups(const FVector& InListener);
    void IncludeGroupDependencies(TSet<FName>& InOutWanted) const;
    void UpdateResourceGroups(const FVector& InListener);
    void UpdateGroup(const FWandererAudioResourceGroup& InGroup, bool bInWanted);
    void AdvanceGroupLoad(const FWandererAudioResourceGroup& InGroup,
                          FWandererAudioResourceState& InOutState);
    bool AcquirePreloadEvents(const FWandererAudioResourceGroup& InGroup,
                              FWandererAudioResourceState& InOutState);
    void SetSpatialResources(const FWandererAudioResourceGroup& InGroup, bool bInActive);
    void ReleaseGroup(FWandererAudioResourceState& InOutState);
    void ShutdownResources();
    bool IsResourceGroupPlaying(FName InGroup) const;
    void BeginGroupLoad(const FWandererAudioResourceGroup& InGroup,
                        FWandererAudioResourceState& InOutState);

    FString ValidateResourceGroups() const;
};
