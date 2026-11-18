#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectKey.h"
#include "UObject/StrongObjectPtr.h"

#include "Types/Audio/AudioEmitterTypes.h"
#include "Types/Audio/AudioSchedulingTypes.h"

#include "AudioEmitterPool.generated.h"

class AActor;
class UAkAudioEvent;
class UAkComponent;
class UAudioPooledEmitterComponent;

struct FObjectEmitterPlayingEvent
{
    TStrongObjectPtr<UAkAudioEvent> Event;
    uint32 PlayingID = 0;
};

/** Game-thread pool. Slot references belong to a single world; releases own their media. */
UCLASS()
class UAudioEmitterPool : public UObject
{
    GENERATED_BODY()

public:
    void ConfigureBudget(const FAudioBudgetLimits& InLimits);
    FAudioEmitterPoolStats GetStats() const;
    void Warmup(UWorld& InWorld, int32 InCapacity);
    UAkComponent* Acquire(const UObject& InSource, bool bInPreferPrevious,
        EAudioEmitterLane InLane = EAudioEmitterLane::World);
    bool AcquireBatch(const UObject& InSource, int32 InCount, TArray<UAkComponent*>& OutComponents);
    void BeginRelease(const UObject& InSource,
                      UAkComponent* InComponent,
                      UAkAudioEvent* InEvent,
                      uint32 InPlayingID,
                      int32 InFadeMs,
                      float InTailSec,
                      FName InGroup,
                      bool bInOwnsMedia,
                      bool bInPreferPrevious);
    void BeginRadioRelease(const UObject& InSource, UAkComponent* InComponent,
        UAkAudioEvent* InPlayEvent, uint32 InPlayID, UAkAudioEvent* InStopEvent, uint32 InStopID);
    void BeginObjectRelease(const UObject& InSource, UAkComponent* InComponent,
        const FGuid& InInstanceID, TArray<FObjectEmitterPlayingEvent>&& InPlayingEvents,
        FName InGroup = NAME_None);
    void RecycleReadyEmitters(TFunctionRef<void(UAkAudioEvent*)> InReleaseMedia);
    void SuspendIdleGroup(FName InGroup);
    void Shutdown(TFunctionRef<void(UAkAudioEvent*, uint32, double, float)> InRetireMedia);
    bool HasPendingSource(const UObject& InSource) const;
    bool HasPendingGroup(FName InGroup) const;
    bool HasPendingObjectEmitter(const FGuid& InInstanceID) const;

private:
    enum class ESlotState : uint8
    {
        Free,
        Active,
        PendingRelease
    };

    struct FSlot
    {
        TWeakObjectPtr<UAudioPooledEmitterComponent> ComponentWeakPtr;
        FObjectKey Source;
        FName ParkedGroup;
        ESlotState State = ESlotState::Free;
        EAudioEmitterLane Lane = EAudioEmitterLane::World;
    };

    struct FRelease
    {
        FObjectKey Source;
        TWeakObjectPtr<UAkComponent> ComponentWeakPtr;
        TStrongObjectPtr<UAkAudioEvent> Event;
        TStrongObjectPtr<UAkAudioEvent> StopEvent;
        TArray<FObjectEmitterPlayingEvent> ObjectEvents;
        FGuid ObjectInstanceID;
        FName Group;
        uint32 PlayingID = 0;
        uint32 StopPlayingID = 0;
        double TailDeadlineSec = -1;
        float TailSec = 0;
        bool bOwnsMedia = false;
        bool bPreferPrevious = false;
        bool bRadio = false;
    };

    UPROPERTY(Transient)
    TObjectPtr<AActor> Host;

    TArray<FSlot> Slots;
    TArray<FRelease> Pending;
    int32 RequestedSlots = 0;
    bool bStopped = false;
    bool bBudgetConfigured = false;
    int32 RegularLimit = 0;
    int32 WorldCapacity = 0;
    int32 EmergencyCapacity = 0;

    bool RegisterBatch(TConstArrayView<FSlot*> InSlots);
    void AddSlot(EAudioEmitterLane InLane);
    FRelease& QueueRelease(const UObject& InSource, UAkComponent* InComponent);
    void FinishRelease(const FRelease& InRelease);
};
