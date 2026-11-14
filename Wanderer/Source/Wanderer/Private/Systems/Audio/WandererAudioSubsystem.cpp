#include "Systems/Audio/WandererAudioSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

#include "AkAudioDevice.h"
#include "AkRoomComponent.h"

#include "Core/Diagnostics/DiagnosticRecorder.h"
#include "Core/WandererEventBusSubsystem.h"
#include "Interfaces/Audio/AudioBudgetProvider.h"
#include "Interfaces/Audio/AudioSourceInterface.h"
#include "Interfaces/EnvironmentStateInterface.h"
#include "Systems/Audio/AudioSchedulingStateTypes.h"

UWandererAudioSubsystem::UWandererAudioSubsystem()
    : ResourceState(MakeShared<FAudioResourceStateMap>())
{
}

FString UWandererAudioSubsystem::GetSummary() const
{
    return FString::Printf(TEXT("Registered=%d  Admitted=%d  Protected overflow=%d  | ")
                               TEXT("Posted != physical voices; use Wwise Profiler"),
                           SourcesWeakPtrs.Num(),
                           AdmittedCount,
                           ProtectedOverflow);
}

void UWandererAudioSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
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
    UpdateElapsedSec += DeltaTime;
    const FAudioBudgetLimits Limits = ReadBudgetLimits();
    const float Interval = FMath::Clamp(Limits.UpdateIntervalSec,
                                        AudioSchedulingDefaults::UpdateIntervalMinSec,
                                        AudioSchedulingDefaults::UpdateIntervalMaxSec);
    if (UpdateElapsedSec < Interval)
    {
        return;
    }

    UpdateElapsedSec = 0.0f;
    ResolveBudget();
    SourcesWeakPtrs.RemoveAll([](const TWeakObjectPtr<UObject>& Item) { return !Item.IsValid(); });
    APlayerController* Player = World->GetFirstPlayerController();
    if (!Player || !Player->GetPawn())
    {
        SuspendSources();
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

void UWandererAudioSubsystem::Deinitialize()
{
    if (bDeinitializing)
    {
        return;
    }

    bDeinitializing = true;
    ShutdownRadioSessions();
    const TArray<TWeakObjectPtr<UObject>> SourcesToStop = MoveTemp(SourcesWeakPtrs);
    RoomBuildingsWeakPtrs.Reset();
    ListenerBuildingWeakPtr.Reset();
    for (const TWeakObjectPtr<UObject>& SourceWeakPtr : SourcesToStop)
    {
        IAudioSourceInterface* Source = Cast<IAudioSourceInterface>(SourceWeakPtr.Get());
        if (Source)
        {
            Source->ShutdownPlayback();
        }
    }

    ShutdownResources();
    BudgetWeakPtr.Reset();
    Super::Deinitialize();
}

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
        ListenerBuildingWeakPtr.Reset();
    }
}

bool UWandererAudioSubsystem::IsListenerInsideBuilding(const UObject* InSource) const
{
    return InSource && ListenerBuildingWeakPtr.Get() == InSource;
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
    ListenerBuildingWeakPtr.Reset();
    FAkAudioDevice* Device = FAkAudioDevice::Get();
    if (RoomBuildingsWeakPtrs.IsEmpty() || !Device)
    {
        return;
    }

    const TArray<UAkRoomComponent*> Rooms =
        Device->FindRoomComponentsAtLocation(InListener, GetWorld());
    if (Rooms.IsEmpty() || !Rooms[0] || Rooms[0]->AutoPost)
    {
        return;
    }

    const TWeakObjectPtr<UObject>* Building =
        RoomBuildingsWeakPtrs.Find(FSoftObjectPath(Rooms[0]->GetOwner()));
    if (Building)
    {
        ListenerBuildingWeakPtr = *Building;
    }
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
