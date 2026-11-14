#include "Core/WandererEventBusSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/StrongObjectPtr.h"

#include "Core/Diagnostics/DiagnosticRecorder.h"
#include "Core/Diagnostics/DiagnosticTraceScope.h"

namespace
{
    constexpr int32 MaximumDispatchDepth = 8;

    void ReportMessage(const UObject* InContext, const FWandererGameEventMessage& InMessage)
    {
        if (InMessage.Type == EWandererGameEvent::AudioDiagnostic)
        {
            if (InMessage.AudioChannel != EWandererAudioLogChannel::Footsteps
                || FDiagnosticRecorder::IsFootstepsEnabled())
            {
                WANDERER_DIAGNOSTIC(InContext,
                    EDiagnosticDomain::Audio,
                    TEXT("legacy-bus-detail"),
                    InMessage.AudioLevel == TEXT("Error")
                        || InMessage.AudioLevel == TEXT("Warning"),
                    InMessage.Text);
            }
            return;
        }
        WANDERER_DIAGNOSTIC(InContext,
            EDiagnosticDomain::Events,
            TEXT("bus-broadcast"),
            false,
            FString::Printf(TEXT("type=%d source=%s scene=%s text=%s"),
                static_cast<int32>(InMessage.Type),
                *InMessage.Source.ToString(),
                *InMessage.Scene.ToString(),
                *InMessage.Text));
    }
} // namespace

void UWandererEventBusSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    bStopped = false;
}

void UWandererEventBusSubsystem::Deinitialize()
{
    bStopped = true;
    OnGameEvent.Clear();
    Super::Deinitialize();
}

UWandererEventBusSubsystem* UWandererEventBusSubsystem::GetEventBus(
    const UObject* WorldContextObject)
{
    if (!IsInGameThread() || !IsValid(WorldContextObject))
    {
        return nullptr;
    }
    UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(
                                  WorldContextObject, EGetWorldErrorMode::ReturnNull)
                            : nullptr;
    UGameInstance* Instance = IsValid(World) ? World->GetGameInstance() : nullptr;
    UWandererEventBusSubsystem* Bus =
        IsValid(Instance) ? Instance->GetSubsystem<UWandererEventBusSubsystem>() : nullptr;
    return Bus && Bus->CanPublish(World) ? Bus : nullptr;
}

void UWandererEventBusSubsystem::Publish(const FWandererGameEventMessage& Message)
{
    if (!IsInGameThread())
    {
        return;
    }
    const TWeakObjectPtr<UWorld> WorldWeakPtr = GetWorld();
    if (!CanPublish(WorldWeakPtr.Get()))
    {
        return;
    }
    if (DispatchDepth >= MaximumDispatchDepth)
    {
        WANDERER_DIAGNOSTIC(this,
            EDiagnosticDomain::Events,
            TEXT("bus-depth-rejected"),
            true,
            TEXT("Recursive publication limit reached; inspect cyclic event handlers."));
        return;
    }
    // Hold only for this call: a synchronous handler can release its game-instance owner.
    const TStrongObjectPtr<UWandererEventBusSubsystem> KeepAlive(this);
    TGuardValue<int32> DepthGuard(DispatchDepth, DispatchDepth + 1);
    FWandererGameEventMessage Snapshot = Message;
    if (Snapshot.Scene.IsNone())
    {
        Snapshot.Scene = FName(*UGameplayStatics::GetCurrentLevelName(WorldWeakPtr.Get(), true));
    }
    FDiagnosticTraceScope Trace;
    ReportMessage(this, Snapshot);
    OnEventObserved().Broadcast(WorldWeakPtr.Get(), Snapshot);
    if (!CanPublish(WorldWeakPtr.Get()) || Snapshot.Type == EWandererGameEvent::AudioDiagnostic)
    {
        return;
    }
    OnGameEvent.Broadcast(Snapshot);
    if (CanPublish(WorldWeakPtr.Get()))
    {
        WANDERER_DIAGNOSTIC(this,
            EDiagnosticDomain::Events,
            TEXT("bus-dispatch-returned"),
            false,
            TEXT(
                "Synchronous dispatch returned; this does not prove an audio handler received it."));
    }
}

FWandererGameEventObserved& UWandererEventBusSubsystem::OnEventObserved()
{
    static FWandererGameEventObserved Observed;
    return Observed;
}

bool UWandererEventBusSubsystem::CanPublish(const UWorld* InWorld) const
{
    return !bStopped && IsValid(InWorld) && InWorld == GetWorld() && InWorld->IsGameWorld()
           && !InWorld->bIsTearingDown;
}
