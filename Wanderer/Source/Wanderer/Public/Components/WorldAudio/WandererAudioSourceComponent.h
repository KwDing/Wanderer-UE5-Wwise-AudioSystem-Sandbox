#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"

#include "Data/Audio/WandererWorldAudioTypes.h"
#include "Interfaces/Audio/AudioSourceInterface.h"

#include "WandererAudioSourceComponent.generated.h"

class UAkComponent;
class UAkGameObject;
class UAkRoomComponent;
class AAkSpatialAudioVolume;
struct FStreamableHandle;

/** Attach to a moving object or use WorldAudioEmitter. Owns only its posted ID. */
UCLASS(ClassGroup = (Audio), BlueprintType, meta = (BlueprintSpawnableComponent))
class WANDERER_API UWandererAudioSourceComponent : public UActorComponent,
                                                   public IAudioSourceInterface
{
    GENERATED_BODY()
  public:
    UWandererAudioSourceComponent();
    UPROPERTY(EditAnywhere,
              BlueprintReadWrite,
              Category = "World Audio",
              meta = (RowType = "/Script/Wanderer.WandererWorldAudioProfileRow"))
    FDataTableRowHandle Profile;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Audio")
    FName Area = TEXT("Unassigned");
    /** None selects independent source scheduling without a resource group. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Audio")
    FName ResourceGroup;
    /** Soft cross-level reference. Only required for Room profiles. */
    UPROPERTY(EditInstanceOnly,
              BlueprintReadWrite,
              Category = "World Audio",
              meta = (AllowedClasses = "/Script/AkAudio.AkSpatialAudioVolume"))
    TSoftObjectPtr<AActor> AcousticRoom;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Audio")
    bool bEnabled = true;
    UPROPERTY(EditAnywhere,
              BlueprintReadWrite,
              Category = "World Audio",
              meta = (ClampMin = "-100", ClampMax = "100"))
    float PriorityBias = 0;

#if WITH_EDITORONLY_DATA
    /** Last authoring source path; never a loadable/cooked reference. */
    UPROPERTY(VisibleInstanceOnly, Category = "World Audio|Geometry Import")
    FString GeometryImportSource;
    UPROPERTY(VisibleInstanceOnly, Category = "World Audio|Geometry Import")
    FGuid GeometryImportBatch;
    UPROPERTY(VisibleInstanceOnly, Category = "World Audio|Geometry Import")
    FString GeometryImportDigest;
    UPROPERTY(VisibleInstanceOnly, Category = "World Audio|Geometry Import")
    FString GeometryImportGroup;
    UPROPERTY(VisibleInstanceOnly, Category = "World Audio|Geometry Import")
    FString GeometrySyncStatus;
    /** Disable to keep an independent, manually edited copy. */
    UPROPERTY(EditInstanceOnly, Category = "World Audio|Geometry Import")
    bool bGeometryAutoSync = true;
    UPROPERTY()
    bool bGeometrySyncSuspended = false;
    UPROPERTY()
    bool bGeometrySyncWasEnabled = true;
#endif

    UFUNCTION(BlueprintPure, Category = "World Audio")
    FString GetStatus() const
    {
        return Status;
    }
    UFUNCTION(BlueprintPure, Category = "World Audio")
    FString GetActiveEventName() const;
    UFUNCTION(BlueprintPure, Category = "World Audio")
    virtual int32 GetPlayingID() const override
    {
        return static_cast<int32>(PlayingID);
    }
    UFUNCTION(BlueprintPure, Category = "World Audio")
    float GetDistanceM() const
    {
        return DistanceM;
    }
    UFUNCTION(BlueprintPure, Category = "World Audio")
    float GetInsideBlend() const
    {
        return InsideBlend;
    }
    UFUNCTION(BlueprintPure, Category = "World Audio")
    float GetEffectivePriority() const
    {
        return EffectivePriority;
    }
    UFUNCTION(BlueprintPure, Category = "World Audio")
    bool IsProtected() const
    {
        return bProtected;
    }
    UFUNCTION(BlueprintPure, Category = "World Audio")
    bool IsPlaying() const;
    /** Read-only actual posted component location; never creates a game object or queries geometry.
     */
    UFUNCTION(BlueprintPure, Category = "World Audio|Visualization")
    bool TryGetPlaybackPosition(FVector& OutPosition) const;

    UFUNCTION(BlueprintPure,
              Category = "World Audio|Visualization",
              meta = (DeprecatedFunction, DeprecationMessage = "Use TryGetPlaybackPosition"))
    bool GetDebugPlaybackPosition(FVector& OutPosition) const;
    UFUNCTION(BlueprintPure, Category = "World Audio")
    virtual FString ValidateConfiguration() const;

    virtual const FWandererWorldAudioProfileRow* GetProfile() const;
    AAkSpatialAudioVolume* ResolveRoom() const;
    virtual bool Evaluate(const FVector& InListener) override;
    virtual bool PrepareForAdmission(bool bInNight) override;
    virtual void UpdatePlayback(bool bInAdmitted, FName InReason, bool bInNight) override;
    virtual void ShutdownPlayback() override;

    virtual FAudioSchedulingInfo GetSchedulingInfo() const override;
    virtual FName GetAudioResourceGroup() const override;
    virtual const UObject* GetPlaybackIdentity() const override;
    virtual bool CollectAudioRoomPaths(TArray<FSoftObjectPath>& OutPaths) const override;

  protected:
    virtual UAkGameObject* GetPlaybackObject() const;
    virtual UAkGameObject* AcquirePlaybackObject();
    UAkComponent* GetOwnedEmitter() const { return OwnedEmitter; }
    UAkComponent* AcquireWorldEmitter(bool bInPreferPrevious);
    void SetEvaluationState(float InPriority, bool bInProtected, const FString& InStatus);
    virtual void PreparePlaybackObject(UAkGameObject* Object)
    {
    }
    virtual void PlaybackStarted(UAkGameObject* Object)
    {
    }
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

  private:
    bool EvaluateGeometry(const FVector& Listener,
                          const FWandererWorldAudioProfileRow& Row,
                          bool& bInsideRoom);
    UAkRoomComponent* ResolvePlaybackRoom() const;
    void UpdateOwnedEmitter();
    void UpdateEmitterGeometry(const FWandererWorldAudioProfileRow& Row);
    void UpdateSplineBlend(const FWandererWorldAudioProfileRow& Row);
    void ReleaseEvent();
    void RequestStop(int32 InFadeMs);
    bool TryStopForPlaybackChange(const TSoftObjectPtr<UAkAudioEvent>& InDesired,
                                  int32 InFadeMs);
    void UpdatePlayingEvent();
    bool PrepareEvent(const TSoftObjectPtr<UAkAudioEvent>& InDesired);
    bool TryStartEventAssetLoad(const TSoftObjectPtr<UAkAudioEvent>& InDesired);
    bool TryAcquireEventLease(const TSoftObjectPtr<UAkAudioEvent>& InDesired);
    void StartPreparedEvent();
    void UpdateAreaPresence(bool bArea);

    UPROPERTY(Transient)
    TObjectPtr<UAkComponent> OwnedEmitter;
    UPROPERTY(Transient)
    TObjectPtr<UAkAudioEvent> LoadedEvent;
    TSharedPtr<FStreamableHandle> LoadHandle;
    FSoftObjectPath RequestedEvent;
    TWeakObjectPtr<UAkGameObject> PostedObjectWeakPtr;
    double LoadStartedAtSec = 0;
    uint32 PlayingID = 0;
    bool bPreferPreviousEmitter = false;
    bool bOwnsResourceLease = false;
    FName PostedResourceGroup;
    float PostedTailSec = 0;
    bool bProtected = false;
    float DistanceM = 0;
    float EffectivePriority = 0;
    FVector AreaEmitterPosition = FVector::ZeroVector;
    float AppliedAreaRadius = -1;
    float InsideBlend = 0;
    float AppliedInsideBlend = -1;
    float AppliedAreaPresence = -1;
    double RetryAtSec = 0;
    FString Status = TEXT("Editor / 未运行");
};
