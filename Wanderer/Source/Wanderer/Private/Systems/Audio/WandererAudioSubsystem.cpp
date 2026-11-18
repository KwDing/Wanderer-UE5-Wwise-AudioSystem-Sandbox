#include "Systems/Audio/WandererAudioSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

#include "AkComponent.h"
#include "AkRoomComponent.h"

#include "Core/Diagnostics/DiagnosticRecorder.h"
#include "Core/WandererEventBusSubsystem.h"
#include "Interfaces/Audio/AudioBudgetProvider.h"
#include "Interfaces/Audio/AudioSourceInterface.h"
#include "Interfaces/EnvironmentStateInterface.h"
#include "Systems/Audio/AudioEmitterPool.h"
#include "Systems/Audio/AudioEventLeaseManager.h"
#include "Systems/Audio/AudioPooledEmitterComponent.h"
#include "Systems/Audio/AudioSchedulingStateTypes.h"
#include "Systems/Audio/RadioPlaybackManager.h"
#include "Systems/Audio/WwiseAudioBackend.h"

UWandererAudioSubsystem::UWandererAudioSubsystem()
    : ResourceState(MakeShared<FAudioResourceStateMap>())
{
}

FString UWandererAudioSubsystem::GetSummary() const
{
    const FAudioEmitterPoolStats Stats = GetEmitterPoolStats();
    return FString::Printf(TEXT("Registered=%d Admitted=%d | Pool=%d Active=%d Retiring=%d ")
                               TEXT("Emergency=%d | ")
                               TEXT("Posted != physical voices; use Wwise Profiler"),
                           SourcesWeakPtrs.Num(),
                           AdmittedCount, Stats.Capacity, Stats.WorldActive, Stats.WorldRetiring,
                           Stats.EmergencyActive);
}

FAudioEmitterPoolStats UWandererAudioSubsystem::GetEmitterPoolStats() const
{
    return EmitterPool ? EmitterPool->GetStats() : FAudioEmitterPoolStats();
}

void UWandererAudioSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    ResolveBudget();
    if (!EmitterPool)
    {
        EmitterPool = NewObject<UAudioEmitterPool>(this);
    }
    const FAudioBudgetLimits Limits = ReadBudgetLimits();
    EmitterPool->ConfigureBudget(Limits);
    EmitterPool->Warmup(InWorld, FMath::Max(1, Limits.TotalEvents));
    UGameInstance* Instance = InWorld.GetGameInstance();
    UWandererEventBusSubsystem* Bus =
        Instance ? Instance->GetSubsystem<UWandererEventBusSubsystem>() : nullptr;
    if (!Bus)
    {
        return;
    }

    FWandererGameEventMessage Message;
    Message.Type = EWandererGameEvent::EnterScene;
    Message.Source = TEXT("AudioEnvironment");
    Message.Text = TEXT("Entered scene");
    Bus->Publish(Message);
}

void UWandererAudioSubsystem::Tick(float DeltaTime)
{
    UWorld* World = GetWorld();
    if (!World || !World->HasBegunPlay() || bDeinitializing)
    {
        return;
    }

    PruneRadioSessions();
    if (EmitterPool)
    {
        EmitterPool->RecycleReadyEmitters([this](UAkAudioEvent* Event)
        {
            ReleaseEventResource(Event);
        });
    }
    if (bDeinitializing)
    {
        return;
    }
    UpdateElapsedSec += DeltaTime;
    const FAudioBudgetLimits Limits = ReadBudgetLimits();
    if (EmitterPool)
    {
        EmitterPool->ConfigureBudget(Limits);
        EmitterPool->Warmup(*World, 0);
    }
    const float Interval = FMath::Clamp(Limits.UpdateIntervalSec,
                                        AudioSchedulingDefaults::UpdateIntervalMinSec,
                                        AudioSchedulingDefaults::UpdateIntervalMaxSec);
    if (UpdateElapsedSec < Interval && !bAdmissionDirty)
    {
        UpdateObjectEmitters(false);
        return;
    }

    UpdateElapsedSec = 0.0f;
    bAdmissionDirty = false;
    ResolveBudget();
    SourcesWeakPtrs.RemoveAll([](const TWeakObjectPtr<UObject>& Item) { return !Item.IsValid(); });
    APlayerController* Player = World->GetFirstPlayerController();
    if (!Player || !Player->GetPawn())
    {
        ResetBuildingListener();
        SuspendSources();
        UpdateObjectEmitters(false);
        return;
    }

    FVector Listener;
    FVector Front;
    FVector Right;
    Player->GetAudioListenerPosition(Listener, Front, Right);
    UpdateResourceGroups(Listener);
    if (bDeinitializing)
    {
        return;
    }
    UpdateBuildingListener(Listener);
    if (!bDeinitializing)
    {
        Schedule(Listener);
        if (!bDeinitializing)
        {
            UpdateObjectEmitters(true);
        }
    }
}

TStatId UWandererAudioSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UWandererAudioSubsystem, STATGROUP_Tickables);
}

bool UWandererAudioSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
    return Type == EWorldType::Game || Type == EWorldType::PIE;
}

// 先封闭调度入口，避免停声时重入
void UWandererAudioSubsystem::Deinitialize()
{
    if (bDeinitializing)
    {
        return;
    }

    bDeinitializing = true;
    ShutdownRadioSessions();
    ShutdownObjectEmitters();
    const TArray<TWeakObjectPtr<UObject>> SourcesToStop = MoveTemp(SourcesWeakPtrs);
    RoomBuildingsWeakPtrs.Reset();
    ResetBuildingListener();
    for (const TWeakObjectPtr<UObject>& SourceWeakPtr : SourcesToStop)
    {
        IAudioSourceInterface* Source = Cast<IAudioSourceInterface>(SourceWeakPtr.Get());
        if (Source)
        {
            Source->ShutdownPlayback();
        }
    }

    if (EmitterPool)
    {
        EmitterPool->Shutdown([this](UAkAudioEvent* Event, uint32 PlayingID,
            double TailDeadlineSec, float TailSec)
        {
            if (EventLeaseManager)
            {
                EventLeaseManager->Retire(Event, PlayingID, TailDeadlineSec, TailSec);
            }
        });
        EmitterPool = nullptr;
    }
    ShutdownResources();
    BudgetWeakPtr.Reset();
    Super::Deinitialize();
}

// 只接收本世界声源，避免跨实例调度
void UWandererAudioSubsystem::RegisterSource(UObject* InSource)
{
    check(IsInGameThread());
    if (bDeinitializing || !IsValid(InSource) || InSource->GetWorld() != GetWorld()
        || !Cast<IAudioSourceInterface>(InSource))
    {
        return;
    }

    if (!RegisterRoomMembership(*InSource))
    {
        return;
    }

    SourcesWeakPtrs.AddUnique(InSource);
}

void UWandererAudioSubsystem::UnregisterSource(UObject* InSource)
{
    check(IsInGameThread());
    SourcesWeakPtrs.Remove(InSource);
    for (auto It = RoomBuildingsWeakPtrs.CreateIterator(); It; ++It)
    {
        if (It.Value().Get() == InSource || !It.Value().IsValid())
        {
            It.RemoveCurrent();
        }
    }

    if (ListenerBuildingWeakPtr.Get() == InSource)
    {
        ResetBuildingListener();
    }
}

bool UWandererAudioSubsystem::IsListenerInsideBuilding(const UObject* InSource) const
{
    return InSource && ListenerBuildingWeakPtr.Get() == InSource;
}

UAkComponent* UWandererAudioSubsystem::AcquireEmitter(
    const UObject& InSource, bool bInPreferPrevious)
{
    return EmitterPool && IsSourceCurrent(&InSource)
        ? EmitterPool->Acquire(InSource, bInPreferPrevious)
        : nullptr;
}

void UWandererAudioSubsystem::ApplyEmitterReverb(UAkComponent& InEmitter, bool bInUseVolumes)
{
    if (UAudioPooledEmitterComponent* Emitter = Cast<UAudioPooledEmitterComponent>(&InEmitter))
    {
        Emitter->ApplyEmitterReverb(bInUseVolumes);
    }
}

void UWandererAudioSubsystem::ReleaseEmitter(const UObject& InSource, UAkComponent* InComponent,
    UAkAudioEvent* InEvent, uint32 InPlayingID, int32 InFadeMs, float InTailSec,
    FName InGroup, bool bInOwnsMedia, bool bInPreferPrevious)
{
    check(EmitterPool);
    EmitterPool->BeginRelease(InSource, InComponent, InEvent, InPlayingID, InFadeMs,
        InTailSec, InGroup, bInOwnsMedia, bInPreferPrevious);
}

bool UWandererAudioSubsystem::HasPendingEmitterRelease(const UObject& InSource) const
{
    return EmitterPool && EmitterPool->HasPendingSource(InSource);
}

void UWandererAudioSubsystem::ConfigureResourceGroups(
    const TArray<FWandererAudioResourceGroup>& InGroups)
{
    check(IsInGameThread());
    if (!bResourcesInitialized && !bDeinitializing)
    {
        ResourceConfig = InGroups;
    }
}

bool UWandererAudioSubsystem::RegisterRoomMembership(UObject& InSource)
{
    IAudioSourceInterface* Source = Cast<IAudioSourceInterface>(&InSource);
    TArray<FSoftObjectPath> Paths;
    if (!Source || !Source->CollectAudioRoomPaths(Paths))
    {
        return false;
    }

    for (const FSoftObjectPath& Path : Paths)
    {
        const TWeakObjectPtr<UObject>* Existing = RoomBuildingsWeakPtrs.Find(Path);
        if (Existing && Existing->IsValid() && Existing->Get() != &InSource)
        {
            UE_LOG(LogWandererAudio,
                   Error,
                   TEXT("Building ambience Room membership conflict: %s"),
                   *Path.ToString());
            return false;
        }
    }

    for (const FSoftObjectPath& Path : Paths)
    {
        RoomBuildingsWeakPtrs.Add(Path, &InSource);
    }

    return true;
}

void UWandererAudioSubsystem::UpdateBuildingListener(const FVector& InListener)
{
    UAkRoomComponent* Room = nullptr;
    const UWorld* World = GetWorld();
    if (RoomBuildingsWeakPtrs.IsEmpty()
        || !WandererAudioBackend::TryQueryPriorityRoom(World, InListener, Room))
    {
        ResetBuildingListener();
        return;
    }

    UAkRoomComponent* PreviousRoom = ListenerRoomWeakPtr.Get();
    if (ListenerRoomWeakPtr.IsStale()
        || (PreviousRoom
            && (!PreviousRoom->IsRegistered() || !PreviousRoom->bEnable || PreviousRoom->AutoPost)))
    {
        ResetBuildingListener();
    }

    if (!IsValid(Room) || Room->GetWorld() != World || !Room->IsRegistered() || !Room->bEnable || Room->AutoPost)
    {
        Room = nullptr;
    }
    const TWeakObjectPtr<UAkRoomComponent> Sample(Room);
    const bool bConfirmed = ListenerRoomWeakPtr.HasSameIndexAndSerialNumber(Sample)
        || (bHasPendingListenerRoom && PendingListenerRoomWeakPtr.HasSameIndexAndSerialNumber(Sample));
    if (!bConfirmed)
    {
        PendingListenerRoomWeakPtr = Sample;
        bHasPendingListenerRoom = true;
        return;
    }
    ListenerRoomWeakPtr = Sample;
    PendingListenerRoomWeakPtr.Reset();
    bHasPendingListenerRoom = false;
    ListenerBuildingWeakPtr.Reset();
    if (Room)
    {
        ListenerBuildingWeakPtr =
            RoomBuildingsWeakPtrs.FindRef(FSoftObjectPath(Room->GetOwner()));
    }
}

void UWandererAudioSubsystem::ResetBuildingListener()
{
    ListenerBuildingWeakPtr.Reset();
    ListenerRoomWeakPtr.Reset();
    PendingListenerRoomWeakPtr.Reset();
    bHasPendingListenerRoom = false;
}

void UWandererAudioSubsystem::ResolveBudget()
{
    if (BudgetWeakPtr.IsValid())
    {
        return;
    }

    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        if (Cast<IAudioBudgetProvider>(*It))
        {
            BudgetWeakPtr = *It;
            return;
        }
    }
}

FAudioBudgetLimits UWandererAudioSubsystem::ReadBudgetLimits() const
{
    const IAudioBudgetProvider* Provider = Cast<IAudioBudgetProvider>(BudgetWeakPtr.Get());
    return Provider ? Provider->GetAudioBudgetLimits() : FAudioBudgetLimits();
}

bool UWandererAudioSubsystem::ReadNightState() const
{
    bool bNight = false;
    bool bFound = false;
    GetWorld()->ForEachSubsystem<UWorldSubsystem>(
        [&bNight, &bFound](UWorldSubsystem* System)
        {
            const IEnvironmentStateInterface* State = Cast<IEnvironmentStateInterface>(System);
            if (!bFound && State)
            {
                bNight = State->IsNight();
                bFound = true;
            }
        });
    return bNight;
}

void UWandererAudioSubsystem::SuspendSources()
{
    SuspendRadioAdmissions();
    TArray<FGuid> ObjectIDs;
    ObjectEmitters.GetKeys(ObjectIDs);
    for (const FGuid& ID : ObjectIDs)
    {
        SetObjectAdmission(ID, false, false);
    }
    const TArray<TWeakObjectPtr<UObject>> Snapshot = SourcesWeakPtrs;
    for (const TWeakObjectPtr<UObject>& SourceWeakPtr : Snapshot)
    {
        if (bDeinitializing)
        {
            return;
        }
        IAudioSourceInterface* Source = Cast<IAudioSourceInterface>(SourceWeakPtr.Get());
        if (Source && IsSourceCurrent(SourceWeakPtr.Get()))
        {
            Source->UpdatePlayback(false, TEXT("No listener"), false);
        }
    }

    GroupAdmitted.Reset();
    AdmittedCount = 0;
    ProtectedOverflow = 0;
}
