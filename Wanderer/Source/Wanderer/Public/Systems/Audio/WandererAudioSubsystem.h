#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "Data/Audio/WorldAudioBudgetData.h"
#include "Types/Audio/AudioSchedulingTypes.h"
#include "Types/GameEventTypes.h"
#include "Types/RadioControlTypes.h"

#include "WandererAudioSubsystem.generated.h"

class APawn;
class UAudioEventLeaseManager;
class URadioPlaybackManager;
class UActorComponent;
class UAkAudioEvent;
struct FStreamableHandle;

struct FWandererAudioResourceState;
struct FAudioSourceCandidate;
struct FAudioResourceStateMap;

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
    bool AcquireEventResource(UAkAudioEvent* InEvent);
    void ReleaseEventResource(UAkAudioEvent* InEvent);

    /** Called synchronously by the selected budget adapter before the session snapshot is sealed.
     */
    void ConfigureResourceGroups(const TArray<FWandererAudioResourceGroup>& InGroups);

    static bool IsRadioContentKnown(FName InContentID);
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

  private:
    UPROPERTY(Transient)
    TMap<TWeakObjectPtr<AActor>, TObjectPtr<URadioPlaybackManager>> RadioSessions;
    void PruneRadioSessions();
    void ShutdownRadioSessions();

    friend class FBuildingAmbienceAuthoringTest;
    friend struct FAudioSchedulingTestAccess;

    UPROPERTY(Transient)
    TArray<FWandererAudioResourceGroup> ResourceConfig;

    UPROPERTY(Transient)
    TObjectPtr<UAudioEventLeaseManager> EventLeaseManager;

    TArray<TWeakObjectPtr<UObject>> SourcesWeakPtrs;
    TWeakObjectPtr<UObject> BudgetWeakPtr;
    TMap<FSoftObjectPath, TWeakObjectPtr<UObject>> RoomBuildingsWeakPtrs;
    TWeakObjectPtr<UObject> ListenerBuildingWeakPtr;
    TSharedPtr<FAudioResourceStateMap> ResourceState;
    TSet<TWeakObjectPtr<UActorComponent>> SuspendedComponents;
    TMap<FName, int32> GroupAdmitted;
    FString ResourceConfigError;
    float UpdateElapsedSec = 0.0f;
    int32 AdmittedCount = 0;
    int32 ProtectedOverflow = 0;
    bool bResourcesInitialized = false;
    bool bDeinitializing = false;

    bool IsSourceCurrent(const UObject* InSource) const;
    bool RegisterRoomMembership(UObject& InSource);
    void UpdateBuildingListener(const FVector& InListener);
    void ResolveBudget();
    FAudioBudgetLimits ReadBudgetLimits() const;
    bool ReadNightState() const;
    void SuspendSources();
    void CollectCandidates(const FVector& InListener,
                           bool bInNight,
                           TArray<FAudioSourceCandidate>& OutCandidates) const;
    void Schedule(const FVector& InListener);
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
